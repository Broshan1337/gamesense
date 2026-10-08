#pragma once

#include <GameClient/Crosshair.h>
#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelHandle.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <GameClient/WorldToScreen/ViewToProjectionMatrix.h>
#include <HookContext/HookContextMacros.h>
#include "SpreadCircleVisConfigVariables.h"
#include "SpreadCircleVisState.h"








template <typename HookContext>
class SpreadCircleVis {
public:
    SpreadCircleVis(HookContext& hookContext) noexcept
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
            panel.setHeight(computeHeightFromCone());
            
            const auto custom = GET_CONFIG_VAR(spread_circle_vars::SpreadCircleColor);
            const auto color = custom.a() > 0
                ? cs2::Color{custom.r(), custom.g(), custom.b()}
                : hookContext.template make<Crosshair>().getColor().valueOr(kFallbackColor);
            panel.setBorder(kBorderWidth, color.setAlpha(kBorderAlpha));
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
        return GET_CONFIG_VAR(spread_circle_vars::Enabled);
    }

    
    
    [[nodiscard]] bool shouldShow() const
    {
        auto&& localPlayerPawn = hookContext.activeLocalPlayerPawn();
        if (!localPlayerPawn || localPlayerPawn.isAlive() != true)
            return false;
        const auto cone = hookContext.localPlayerBulletInaccuracy();
        return cone.hasValue() && cone.value() > 0.0f;
    }

    [[nodiscard]] cs2::CUILength computeHeightFromCone() const
    {
        const auto y = hookContext.template make<ViewToProjectionMatrix>().transformY(getCone());
        return cs2::CUILength::percent(y.value_or(0.0f) * 100.0f);
    }

    [[nodiscard]] float getCone() const
    {
        return hookContext.localPlayerBulletInaccuracy().valueOr(0.0f);
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
        return hookContext.featuresStates().spreadCircleVisState;
    }

    static constexpr cs2::Color kFallbackColor{255, 255, 255};
    static constexpr int kBorderAlpha{255};
    static constexpr int kBackgroundAlpha{30};
    static constexpr auto kBorderWidth{cs2::CUILength::pixels(1)};

    HookContext& hookContext;
};
