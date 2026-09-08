#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Hud/Watermark/WatermarkConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkPanelParams.h>
#include <UI/ImGui/Neverlose/MenuThemeConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkState.h>
#include <GameClient/Panorama/PanoramaLabel.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Lvalue.h>
#include <Utils/Optional.h>
#include <Utils/StringBuilder.h>
#include <Utils/Trig.h>

// The watermark: "Neversnooze | 84 | <fps> fps | <speed> u/s | <ping> ms | <dmg> td | HH:MM"
// plus three feature chips (BT / EXP / COMP - backtracking, extrapolation, triggerbot spread
// compensation - bright when enabled, dim gray when not). Purely cosmetic; the data behind every
// segment is already read elsewhere in the codebase.
//
// The panel lives top-right of the HUD ROOT (full-screen parent - see Hud::rootPanel()). The box
// is created lazily on the first rendered frame; text refreshes at ~7 Hz so the speed readout
// feels alive, while the FPS number itself is measured over >= 0.5 s windows.
template <typename HookContext>
class Watermark {
public:
    explicit Watermark(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(watermark_vars::Enabled))
            return;

        const double now = monotonicSeconds();

        ++frameCount;
        const double elapsed = now - windowStart;
        if (elapsed >= 0.5 && frameCount > 0) {
            // Round to nearest fps; keep the last computed value between windows so the text
            // does not flicker stale/empty mid-window.
            framesPerSecond = static_cast<int>(static_cast<double>(frameCount) / elapsed + 0.5);
            frameCount = 0;
            windowStart = now;
        }

        auto&& panel = uiEngine().getPanelFromHandle(state().textPanelHandle);
        if (!panel) {
            if (now - lastCreateAttempt < 1.0)
                return;   // throttle creation attempts (no HUD in the main menu etc.)
            lastCreateAttempt = now;
            createPanel();
            return;
        }

        if (now - lastTextUpdate < 0.15)
            return;
        lastTextUpdate = now;

        updateText(panel);
        updateChips();
        applyBoxOffset();
        // keep the text in sync with the menu accent without restyling Panorama every frame:
        // only write the style property when the color actually changed
        if (const auto accent = GET_CONFIG_VAR(MenuAccentColor); static_cast<std::uint32_t>(accent) != state().lastAccentColor) {
            state().lastAccentColor = accent;
            panel.setColor(accentColor());
        }
    }

    void onUnload() const noexcept
    {
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().textPanelHandle);
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().boxPanelHandle);
    }

private:
    void updateText(auto&& panel) const noexcept
    {
        using namespace watermark_panel_params;

        // "Neversnooze | 84" is the constant prefix; every segment after it is individually
        // toggleable from the Hud page (Hud > Watermark).
        StringBuilderStorage<96> storage;
        auto builder = storage.builder();
        builder.put("Neversnooze", ' ', '|', ' ', 84);
        if (GET_CONFIG_VAR(watermark_vars::ShowFps))
            builder.put(' ', '|', ' ', framesPerSecond, ' ', 'f', 'p', 's');
        if (GET_CONFIG_VAR(watermark_vars::ShowSpeed))
            appendSpeed(builder);
        if (GET_CONFIG_VAR(watermark_vars::ShowPing))
            appendPing(builder);
        if (GET_CONFIG_VAR(watermark_vars::ShowTeamDamage))
            appendTeamDamage(builder);
        if (GET_CONFIG_VAR(watermark_vars::ShowClock))
            appendClock(builder);
        panel.clientPanel().template as<PanoramaLabel>().setText(builder.cstring());
    }

    void appendSpeed(StringBuilder& builder) const noexcept
    {
        const auto speed = localSpeed();
        if (!speed.hasValue())
            return;
        builder.put(" ", '|', ' ', static_cast<int>(speed.value() + 0.5f), ' ', 'u', '/', 's');
    }

    // 2D magnitude of the local pawn's m_vecAbsVelocity (schema-resolved - no hardcoded offset).
    [[nodiscard]] Optional<float> localSpeed() const noexcept
    {
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return {};
        auto* const entity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        if (!entity)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!offset.has_value() || *offset <= 0)
            return {};
        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(velocity));
        return trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
    }

    void appendPing(StringBuilder& builder) const noexcept
    {
        const auto ping = hookContext.localPlayerController().ping();
        if (!ping.hasValue())
            return;
        builder.put(" ", '|', ' ', ping.value(), ' ', 'm', 's');
    }

    void appendTeamDamage(StringBuilder& builder) const noexcept
    {
        builder.put(" ", '|', ' ', myTeamDamageDealt(), ' ', 't', 'd');
    }

    // The local controller occupies entity index slot+1 (see PlayerSlotLookup) - its handle's
    // index therefore IS our slot + 1.
    [[nodiscard]] int myTeamDamageDealt() const noexcept
    {
        const auto handle = hookContext.localPlayerController().baseEntity().handle();
        const auto entityIndex = handle.index().value;
        if (entityIndex == 0 || entityIndex > 64)
            return 0;
        return hookContext.template make<TeamDamageTracker>().totalTeamDamageForSlot(static_cast<std::int64_t>(entityIndex) - 1);
    }

    void appendClock(StringBuilder& builder) const noexcept
    {
        const std::time_t now = std::time(nullptr);
        std::tm localTime{};
        if (!localtime_r(&now, &localTime))
            return;
        builder.put(" ", '|', ' ',
            localTime.tm_hour / 10, localTime.tm_hour % 10, ':',
            localTime.tm_min / 10, localTime.tm_min % 10);
    }

    // The watermark text follows the menu theme's accent color (Menu > Style / Accent), so
    // switching the menu theme recolors the HUD watermark too.
    [[nodiscard]] cs2::Color accentColor() const noexcept
    {
        const auto accent = GET_CONFIG_VAR(MenuAccentColor);
        return cs2::Color{accent.r(), accent.g(), accent.b(), accent.a()};
    }

    void updateChips() const noexcept
    {
        using namespace watermark_panel_params;

        const struct {
            const cs2::PanelHandle& handle;
            bool enabled;
            const char* text;
        } chips[3]{
            {state().chipPanelHandles[0], GET_CONFIG_VAR(aimbot_vars::Backtrack), "BT"},
            {state().chipPanelHandles[1], GET_CONFIG_VAR(aimbot_vars::Extrapolate), "EXP"},
            {state().chipPanelHandles[2], GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation), "COMP"},
        };

        for (const auto& chip : chips) {
            auto&& panel = uiEngine().getPanelFromHandle(chip.handle);
            if (!panel)
                continue;
            panel.clientPanel().template as<PanoramaLabel>().setText(chip.text);
            panel.setColor(chip.enabled ? accentColor() : kChipOffColor);
        }
    }

    // The watermark box hangs off the top-right corner; the offset is user-configurable
    // (Hud > Watermark > X/Y Offset). Only written to Panorama when the value changes.
    void applyBoxOffset() const noexcept
    {
        const int offsetX = GET_CONFIG_VAR(watermark_vars::OffsetX);
        const int offsetY = GET_CONFIG_VAR(watermark_vars::OffsetY);
        if (offsetX == state().lastMarginX && offsetY == state().lastMarginY)
            return;
        state().lastMarginX = offsetX;
        state().lastMarginY = offsetY;
        if (auto&& box = uiEngine().getPanelFromHandle(state().boxPanelHandle))
            box.setMargin(PanelMarginParams{
                .marginTop = cs2::CUILength::pixels(offsetY),
                .marginRight = cs2::CUILength::pixels(offsetX)});
    }

    void createPanel() const noexcept
    {
        using namespace watermark_panel_params;

        auto&& panel = hookContext.panelFactory().createPanel(hookContext.hud().rootPanel()).uiPanel();
        if (!panel)
            return;

        // The box: translucent dark rounded rectangle anchored to the top-right of the HUD root.
        panel.setFlowChildren(cs2::k_EFlowRight);
        panel.setBackgroundColor(kBoxColor);
        panel.setBorderRadius(kBoxBorderRadius);
        panel.setAlign(kAlignment);
        panel.setMargin(PanelMarginParams{
            .marginTop = cs2::CUILength::pixels(GET_CONFIG_VAR(watermark_vars::OffsetY)),
            .marginRight = cs2::CUILength::pixels(GET_CONFIG_VAR(watermark_vars::OffsetX))});
        state().boxPanelHandle = panel.getHandle();

        auto&& text = hookContext.panelFactory().createLabelPanel(panel).uiPanel();
        if (!text)
            return;
        text.setFont(kFont);
        text.setColor(accentColor());
        text.setMargin(kTextMargin);
        state().textPanelHandle = text.getHandle();

        // Chip texts are set per-frame by the update path (with live values); the panels stay
        // empty until its first tick.
        for (std::size_t i = 0; i < 3; ++i) {
            auto&& chip = hookContext.panelFactory().createLabelPanel(panel).uiPanel();
            if (!chip)
                continue;
            chip.setFont(kChipFont);
            chip.setColor(kChipOffColor);
            chip.setMargin(kChipMargin);
            state().chipPanelHandles[i] = chip.getHandle();
        }
    }

    [[nodiscard]] auto& state() const noexcept
    {
        return hookContext.featuresStates().hudFeaturesStates.watermarkState;
    }

    [[nodiscard]] decltype(auto) uiEngine() const noexcept
    {
        return hookContext.template make<PanoramaUiEngine>();
    }

    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
    }

    // Measurement accumulators. Static for the same reason the other per-frame feature counters
    // are: feature objects are rebuilt per call, the stream of frames is not.
    inline static double windowStart{0.0};
    inline static std::uint64_t frameCount{0};
    inline static int framesPerSecond{0};
    inline static double lastTextUpdate{-1.0e9};
    inline static double lastCreateAttempt{-1.0e9};

    HookContext& hookContext;
};
