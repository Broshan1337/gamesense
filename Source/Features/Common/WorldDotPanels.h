#pragma once

#include <cstddef>
#include <span>

#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/PanelHandle.h>
#include <CS2/Panorama/Transform3D.h>
#include <GameClient/Panorama/PanelHandle.h>
#include <GameClient/Panorama/PanoramaTransformations.h>

// Persistent state for one pooled-dot overlay (container panel handle + pool high-water mark).
struct WorldDotPanelsState {
    cs2::PanelHandle containerPanelHandle;
    std::size_t panelsCreated{0};

    void reset() noexcept
    {
        containerPanelHandle = cs2::PanelHandle{};
        panelsCreated = 0;
    }
};

// Pooled small colored round panels for world-positioned markers (bullet impact dots, tracer
// segments, grenade trajectory points). Modeled on InWorldPanels + SoundVisualizationFeature:
// one container panel under the HUD reticle, a lazily-grown pool, each frame positioning the
// active dots with the same percent-translate projection path the sound visualizers use.
template <typename HookContext>
class WorldDotPanels {
public:
    WorldDotPanels(HookContext& hookContext, WorldDotPanelsState& state) noexcept
        : hookContext{hookContext}
        , state{state}
    {
    }

    // Makes sure the container and at least maxPanels dots exist. Dots are created visible and
    // hidden immediately (a panel created hidden may never reveal - see the panorama notes).
    void prepare(float dotSizePx, cs2::Color color, std::size_t maxPanels) noexcept
    {
        auto&& container = containerPanel();
        if (!container)
            return;

        while (state.panelsCreated < maxPanels) {
            auto dot = hookContext.panelFactory().createPanel(static_cast<cs2::CUIPanel*>(container)).uiPanel();
            dot.setWidth(cs2::CUILength::pixels(dotSizePx));
            dot.setHeight(cs2::CUILength::pixels(dotSizePx));
            dot.setPosition(cs2::CUILength::pixels(-dotSizePx * 0.5f), cs2::CUILength::pixels(-dotSizePx * 0.5f));
            dot.setBackgroundColor(color);
            dot.setBorderRadius(cs2::CUILength::pixels(dotSizePx * 0.5f));
            dot.setVisible(false);
            ++state.panelsCreated;
        }
    }

    void showDot(std::size_t index, float deviceXPercent, float deviceYPercent, float clipZ, float opacity) noexcept
    {
        auto&& container = containerPanel();
        if (!container || index >= state.panelsCreated)
            return;

        auto&& dot = container.children()[index];
        dot.setVisible(true);
        dot.setOpacity(opacity);
        dot.setZIndex(-clipZ);

        auto&& transformFactory = hookContext.panoramaTransformFactory();
        PanoramaTransformations{
            transformFactory.translate(cs2::CUILength{deviceXPercent, cs2::CUILength::k_EUILengthPercent}, cs2::CUILength{deviceYPercent, cs2::CUILength::k_EUILengthPercent})
        }.applyTo(dot);
    }

    void hideRest(std::size_t firstUnusedIndex) noexcept
    {
        auto&& container = containerPanel();
        if (!container)
            return;
        for (auto i = firstUnusedIndex; i < state.panelsCreated; ++i)
            container.children()[i].setVisible(false);
    }

private:
    [[nodiscard]] decltype(auto) containerPanel() noexcept
    {
        return hookContext.template make<PanelHandle>(state.containerPanelHandle).getOrInit([this] {
            auto panel = hookContext.panelFactory().createPanel(hookContext.hud().getHudReticle()).uiPanel();
            panel.fitParent();
            return panel;
        });
    }

    HookContext& hookContext;
    WorldDotPanelsState& state;
};