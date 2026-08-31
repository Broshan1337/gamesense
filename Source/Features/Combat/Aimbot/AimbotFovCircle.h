#pragma once

#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CUILength.h>
#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelHandle.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <GameClient/WorldToScreen/ViewToProjectionMatrix.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/ColorUtils.h>
#include <Utils/Trig.h>
#include <Features/Combat/LegitAimbot/LegitAimbotConfigVariables.h>

// Draws the legit aimbot's FOV limit as a circle centred on the crosshair, using the same round-panel
// technique as the no-scope inaccuracy visual (a HUD-reticle child, 50% border radius, height sized
// through the view->projection matrix). The size is the FOV angle projected to screen: a target on
// the circle edge is exactly the FOV away from the crosshair, so the circle shows what the assist will
// pull onto. This is velocity's legit visualize_fov - the class kept its old AimbotFovCircle name from
// when it hung off the rage aimbot (rage has no FOV visual in velocity; its max_fov is an invisible
// per-point gate). The fill is near-transparent (alpha ~20) so it does not obscure the view.
template <typename HookContext>
class AimbotFovCircle {
public:
    AimbotFovCircle(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void update() const
    {
        if (!enabled()) {
            onDisable();
            return;
        }

        const auto visible = shouldShow();
        auto&& panel = getPanel();
        panel.setVisible(visible);
        if (visible) {
            panel.setHeight(computeHeightFromFov());
            const auto configuredColor = GET_CONFIG_VAR(legit_aimbot_vars::FovCircleColor);
            // the border uses the picked color including its alpha; the fill stays near-transparent
            // so it never obscures the view
            const cs2::Color color{configuredColor.r(), configuredColor.g(), configuredColor.b(), configuredColor.a()};
            panel.setBorder(kBorderWidth, color);
            panel.setBackgroundColor(color.setAlpha(kBackgroundAlpha));
        }
    }

    void onDisable() const
    {
        hookContext.template make<PanelHandle>(state().panelHandle).get().hide();
    }

    void onUnload() const
    {
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().panelHandle);
    }

private:
    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(legit_aimbot_vars::Enabled) && GET_CONFIG_VAR(legit_aimbot_vars::DrawFov);
    }

    [[nodiscard]] bool shouldShow() const
    {
        auto&& localPlayerPawn = hookContext.activeLocalPlayerPawn();
        return localPlayerPawn && localPlayerPawn.isAlive() == true;
    }

    // Diameter of the circle as a percentage of screen height. The FOV angle's tangent is the
    // view-space slope a point at that angle sits on; transforming it through the projection matrix
    // (exactly as the no-scope visual does with the inaccuracy slope) gives its normalized screen
    // extent. Clamped below 90 degrees for drawing so the tangent cannot blow up (the hit gate still
    // uses the full slider value; only the on-screen circle is clamped).
    [[nodiscard]] cs2::CUILength computeHeightFromFov() const
    {
        auto fovDegrees = static_cast<float>(GET_CONFIG_VAR(legit_aimbot_vars::Fov));
        if (fovDegrees > kMaxDrawableFovDegrees)
            fovDegrees = kMaxDrawableFovDegrees;

        const auto radians = fovDegrees * trig::kDegreesToRadians;
        const auto cosine = trig::cosine(radians);
        const auto tangent = cosine != 0.0f ? trig::sine(radians) / cosine : 0.0f;

        const auto height = hookContext.template make<ViewToProjectionMatrix>().transformY(tangent);
        return cs2::CUILength::percent(height.value_or(0.0f) * 100.0f);
    }

    [[nodiscard]] decltype(auto) getPanel() const
    {
        return hookContext.template make<PanelHandle>(state().panelHandle).getOrInit(createPanel());
    }

    [[nodiscard]] auto createPanel() const noexcept
    {
        return [this] () -> decltype(auto) {
            auto&& panel = hookContext.panelFactory().createPanel(hookContext.hud().getHudReticle()).uiPanel();
            panel.setWidth(cs2::CUILength::heightPercentage(100));
            panel.setBorderRadius(cs2::CUILength::percent(50));
            panel.setAlign(PanelAlignmentParams{
                .horizontalAlignment = cs2::k_EHorizontalAlignmentCenter,
                .verticalAlignment = cs2::k_EVerticalAlignmentCenter});
            return utils::lvalue<decltype(panel)>(panel);
        };
    }

    [[nodiscard]] auto& state() const
    {
        return hookContext.featuresStates().aimbotFovCircleState;
    }

    static constexpr std::uint8_t kBackgroundAlpha{20};
    static constexpr auto kBorderWidth{cs2::CUILength::pixels(1)};
    static constexpr float kMaxDrawableFovDegrees{89.0f};

    HookContext& hookContext;
};
