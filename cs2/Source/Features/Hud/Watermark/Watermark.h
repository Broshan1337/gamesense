#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <CS2/Panorama/StyleEnums.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Hud/ChipPanel.h>
#include <Features/Hud/Watermark/WatermarkConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkPanelParams.h>
#include <UI/ImGui/Neverlose/MenuThemeConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkState.h>
#include <GameClient/Panorama/ImagePanel.h>
#include <GameClient/Panorama/PanoramaLabel.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/NsStr.h>
#include <Utils/Lvalue.h>
#include <Utils/Optional.h>
#include <Utils/StringBuilder.h>
#include <Utils/Trig.h>











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
            
            
            const int measured = static_cast<int>(static_cast<double>(frameCount) / elapsed + 0.5);
            frameCount = 0;
            windowStart = now;
            fpsDisplay = fpsDisplay <= 0 ? measured : static_cast<int>(fpsDisplay + (measured - fpsDisplay) * 0.35f);
        }

        auto&& panel = uiEngine().getPanelFromHandle(state().boxPanelHandle);
        if (!panel) {
            if (now - lastCreateAttempt < 1.0)
                return;   
            lastCreateAttempt = now;
            createPanel();
            return;
        }

        if (now - lastTextUpdate < 0.15)
            return;
        lastTextUpdate = now;

        updateSegments();
        updateChips();
        applyBoxOffset();
        
        
        if (const auto accent = GET_CONFIG_VAR(MenuAccentColor); static_cast<std::uint32_t>(accent) != state().lastAccentColor) {
            state().lastAccentColor = accent;
            applyAccentColor();
        }
    }

    void onUnload() const noexcept
    {
        
        
        for (auto& segment : state().segments) {
            uiEngine().deletePanelByHandle(segment.labelHandle);
            uiEngine().deletePanelByHandle(segment.iconHandle);
            uiEngine().deletePanelByHandle(segment.boxHandle);
        }
        for (const auto& chipLabel : state().chipPanelHandles)
            uiEngine().deletePanelByHandle(chipLabel);
        for (const auto& chipBox : state().chipBoxHandles)
            uiEngine().deletePanelByHandle(chipBox);
        uiEngine().deletePanelByHandle(state().brandLabelHandle);
        uiEngine().deletePanelByHandle(state().brandBoxHandle);
        uiEngine().deletePanelByHandle(state().boxPanelHandle);
    }

private:
    
    enum Segment { SegFps, SegSpeed, SegPing, SegTeamDamage, SegClock, SegCount };

    [[nodiscard]] bool segmentEnabled(Segment segment) const noexcept
    {
        switch (segment) {
        case SegFps: return GET_CONFIG_VAR(watermark_vars::ShowFps);
        case SegSpeed: return GET_CONFIG_VAR(watermark_vars::ShowSpeed);
        case SegPing: return GET_CONFIG_VAR(watermark_vars::ShowPing);
        case SegTeamDamage: return GET_CONFIG_VAR(watermark_vars::ShowTeamDamage);
        default: return GET_CONFIG_VAR(watermark_vars::ShowClock);
        }
    }

    void updateSegments() const noexcept
    {
        for (int i = 0; i < SegCount; ++i) {
            const auto segment = static_cast<Segment>(i);
            auto&& box = uiEngine().getPanelFromHandle(state().segments[i].boxHandle);
            if (!box)
                continue;
            auto&& label = uiEngine().getPanelFromHandle(state().segments[i].labelHandle);
            if (!label)
                continue;

            StringBuilderStorage<32> storage;
            auto builder = storage.builder();
            bool hasText = segmentEnabled(segment);
            if (hasText) {
                switch (segment) {
                case SegFps: builder.put(fpsDisplay, ' ', 'f', 'p', 's'); break;
                case SegSpeed: hasText = appendSpeed(builder); break;
                case SegPing: hasText = appendPing(builder); break;
                case SegTeamDamage: builder.put(myTeamDamageDealt(), ' ', 't', 'd'); break;
                case SegClock: hasText = appendClock(builder); break;
                default: break;
                }
            }

            
            
            
            if (!hasText) {
                if (segmentShown[i]) {
                    box.setBackgroundColor(chip_panel::kFillColor.setAlpha(0));
                    label.clientPanel().template as<PanoramaLabel>().setText("");
                    segmentShown[i] = false;
                }
                continue;
            }

            if (!segmentShown[i]) {
                box.setBackgroundColor(chip_panel::kFillColor); 
                segmentShown[i] = true;
            }
            label.clientPanel().template as<PanoramaLabel>().setText(builder.cstring());
        }
    }

    [[nodiscard]] bool appendSpeed(StringBuilder& builder) const noexcept
    {
        const auto speed = localSpeed();
        if (!speed.hasValue())
            return false;
        builder.put(static_cast<int>(speed.value() + 0.5f), ' ', 'u', '/', 's');
        return true;
    }

    
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

    [[nodiscard]] bool appendPing(StringBuilder& builder) const noexcept
    {
        const auto ping = hookContext.localPlayerController().ping();
        if (!ping.hasValue())
            return false;
        builder.put(ping.value(), ' ', 'm', 's');
        return true;
    }

    
    
    [[nodiscard]] int myTeamDamageDealt() const noexcept
    {
        const auto handle = hookContext.localPlayerController().baseEntity().handle();
        const auto entityIndex = handle.index().value;
        if (entityIndex == 0 || entityIndex > 64)
            return 0;
        return hookContext.template make<TeamDamageTracker>().totalTeamDamageForSlot(static_cast<std::int64_t>(entityIndex) - 1);
    }

    [[nodiscard]] bool appendClock(StringBuilder& builder) const noexcept
    {
        const std::time_t now = std::time(nullptr);
        std::tm localTime{};
        if (!localtime_r(&now, &localTime))
            return false;
        builder.put(
            localTime.tm_hour / 10, localTime.tm_hour % 10, ':',
            localTime.tm_min / 10, localTime.tm_min % 10);
        return true;
    }

    
    void applyAccentColor() const noexcept
    {
        using namespace watermark_panel_params;

        if (auto&& brandLabel = uiEngine().getPanelFromHandle(state().brandLabelHandle))
            brandLabel.setColor(accentColor());

        static constexpr const char* iconUrls[SegCount]{
            kFpsIconUrl, kSpeedIconUrl, kPingIconUrl, kTeamDamageIconUrl, kClockIconUrl};
        for (int i = 0; i < SegCount; ++i) {
            auto&& icon = uiEngine().getPanelFromHandle(state().segments[i].iconHandle);
            if (!icon)
                continue;
            icon.clientPanel().template as<ImagePanel>()
                .setImageSvg(SvgImageParams{.imageUrl = iconUrls[i], .textureHeight = kIconTextureHeight, .fillColor = accentColor()});
        }
    }

    
    
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

        
        panel.setFlowChildren(cs2::k_EFlowRight);
        panel.setAlign(kAlignment);
        panel.setMargin(PanelMarginParams{
            .marginTop = cs2::CUILength::pixels(GET_CONFIG_VAR(watermark_vars::OffsetY)),
            .marginRight = cs2::CUILength::pixels(GET_CONFIG_VAR(watermark_vars::OffsetX))});
        state().boxPanelHandle = panel.getHandle();

        
        auto&& brandBox = chip_panel::createChipPanel(hookContext, panel, kChipGap, 100.0f);
        if (brandBox) {
            state().brandBoxHandle = brandBox.getHandle();
            auto&& brandLabel = hookContext.panelFactory().createLabelPanel(brandBox).uiPanel();
            if (brandLabel) {
                brandLabel.setFont(kBrandFont);
                brandLabel.setColor(accentColor());
                brandLabel.setMargin(kBrandTextMargin);
                NS_STR(brandText, "Neversnooze");
                brandLabel.clientPanel().template as<PanoramaLabel>().setText(brandText);
                state().brandLabelHandle = brandLabel.getHandle();
            }
        }

        static constexpr struct {
            const char* iconUrl;
        } segmentDefs[SegCount]{
            {kFpsIconUrl}, {kSpeedIconUrl}, {kPingIconUrl}, {kTeamDamageIconUrl}, {kClockIconUrl}};

        for (int i = 0; i < SegCount; ++i) {
            auto&& box = chip_panel::createChipPanel(hookContext, panel, kChipGap, 100.0f);
            if (!box)
                continue;
            state().segments[i].boxHandle = box.getHandle();

            auto&& iconImage = hookContext.panelFactory().createImagePanel(box);
            auto&& icon = iconImage.uiPanel();
            if (icon) {
                icon.setAlign(PanelAlignmentParams{
                    .horizontalAlignment = cs2::k_EHorizontalAlignmentLeft,
                    .verticalAlignment = cs2::k_EVerticalAlignmentCenter});
                icon.setMargin(kIconMargin);
                iconImage.setImageSvg(SvgImageParams{
                    .imageUrl = segmentDefs[i].iconUrl,
                    .textureHeight = kIconTextureHeight,
                    .fillColor = accentColor()});
                state().segments[i].iconHandle = icon.getHandle();
            }

            auto&& label = hookContext.panelFactory().createLabelPanel(box).uiPanel();
            if (label) {
                label.setFont(kValueFont);
                label.setColor(kValueColor);
                label.setMargin(kSegmentTextMargin);
                state().segments[i].labelHandle = label.getHandle();
            }
        }

        
        
        for (std::size_t i = 0; i < 3; ++i) {
            auto&& box = chip_panel::createChipPanel(hookContext, panel, kChipGap, 100.0f);
            if (!box)
                continue;
            box.setAlign(PanelAlignmentParams{
                .horizontalAlignment = cs2::k_EHorizontalAlignmentLeft,
                .verticalAlignment = cs2::k_EVerticalAlignmentCenter});
            state().chipBoxHandles[i] = box.getHandle();

            auto&& chip = hookContext.panelFactory().createLabelPanel(box).uiPanel();
            if (!chip)
                continue;
            chip.setFont(kChipFont);
            chip.setColor(kChipOffColor);
            chip.setMargin(kFeatureChipTextMargin);
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

    
    
    inline static double windowStart{0.0};
    inline static std::uint64_t frameCount{0};
    inline static int fpsDisplay{0};
    inline static bool segmentShown[SegCount]{};
    inline static double lastTextUpdate{-1.0e9};
    inline static double lastCreateAttempt{-1.0e9};

    HookContext& hookContext;
};
