#pragma once

#include <cstddef>
#include <cstdint>

#include <Features/Combat/LegitAimbot/LegitAimbotConfigVariables.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <Features/Game/BlockbotConfigVariables.h>
#include <Features/Hud/StatusPanel/StatusPanelParams.h>
#include <Features/Hud/StatusPanel/StatusPanelState.h>
#include <GameClient/Bind.h>
#include <GameClient/KeyboardState.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>

// Bottom-left status chips, above the money/chat HUD: AIM (legit aim assist), TRIG (triggerbot),
// BLOCK (blockbot). Three states each - bright green when the feature is on AND its key is held
// (actually working right now), dim green when on but idle, gray when disabled. The point is
// answering "why is nothing happening?" at a glance instead of digging through the menu.
template <typename HookContext>
class StatusPanel {
public:
    explicit StatusPanel(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        const double now = monotonicSeconds();

        auto&& panel = uiEngine().getPanelFromHandle(state().boxPanelHandle);
        if (!panel) {
            if (now - lastCreateAttempt < 1.0)
                return;   // throttle creation attempts (no HUD in the main menu etc.)
            lastCreateAttempt = now;
            createPanel();
            return;
        }

        if (now - lastChipUpdate < 0.1)
            return;
        lastChipUpdate = now;

        updateChips();
    }

    void onUnload() const noexcept
    {
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().boxPanelHandle);
    }

private:
    void updateChips() const noexcept
    {
        using namespace status_panel_params;

        const struct {
            const cs2::PanelHandle& handle;
            bool enabled;
            bool held;
            const char* text;
        } chips[3]{
            {state().chipPanelHandles[0], GET_CONFIG_VAR(legit_aimbot_vars::Enabled), aimKeyHeld(), "AIM"},
            {state().chipPanelHandles[1], GET_CONFIG_VAR(triggerbot_vars::Enabled), triggerKeyHeld(), "TRIG"},
            {state().chipPanelHandles[2], GET_CONFIG_VAR(BlockbotEnabled), KeyboardState::isKeyDown(sdl3::scancode::kE), "BLOCK"},
        };

        for (const auto& chip : chips) {
            auto&& panel = uiEngine().getPanelFromHandle(chip.handle);
            if (!panel)
                continue;
            panel.clientPanel().template as<PanoramaLabel>().setText(chip.text);
            panel.setColor(chip.enabled ? (chip.held ? kHeldColor : kIdleColor) : kOffColor);
        }
    }

    [[nodiscard]] bool aimKeyHeld() const noexcept
    {
        return Bind::isDown(GET_CONFIG_VAR(legit_aimbot_vars::AimKey));
    }

    [[nodiscard]] bool triggerKeyHeld() const noexcept
    {
        return Bind::isDown(GET_CONFIG_VAR(triggerbot_vars::HoldKey));
    }

    void createPanel() const noexcept
    {
        using namespace status_panel_params;

        auto&& panel = hookContext.panelFactory().createPanel(hookContext.hud().rootPanel()).uiPanel();
        if (!panel)
            return;

        // The box: translucent dark rounded rectangle above the money/chat HUD, bottom-left.
        panel.setFlowChildren(cs2::k_EFlowRight);
        panel.setBackgroundColor(kBoxColor);
        panel.setBorderRadius(kBoxBorderRadius);
        panel.setAlign(kAlignment);
        panel.setMargin(kBoxMargin);
        state().boxPanelHandle = panel.getHandle();

        // Chip texts are set per-frame by updateChips() (with live enabled/held state); the
        // panels stay empty until its first tick.
        for (std::size_t i = 0; i < 3; ++i) {
            auto&& chip = hookContext.panelFactory().createLabelPanel(panel).uiPanel();
            if (!chip)
                continue;
            chip.setFont(kFont);
            chip.setColor(kOffColor);
            chip.setMargin(kChipMargin);
            state().chipPanelHandles[i] = chip.getHandle();
        }
    }

    [[nodiscard]] auto& state() const noexcept
    {
        return hookContext.featuresStates().hudFeaturesStates.statusPanelState;
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

    inline static double lastChipUpdate{-1.0e9};
    inline static double lastCreateAttempt{-1.0e9};

    HookContext& hookContext;
};
