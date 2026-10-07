#pragma once

#include <algorithm>
#include <tuple>
#include <type_traits>

#include <CS2/Classes/Vector.h>
#include <CS2/Panorama/CUIPanel.h>
#include <GameClient/WorldToScreen/ViewToProjectionMatrix.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <GameClient/Panorama/PanoramaTransformations.h>
#include <GameClient/Panorama/PanoramaUiPanel.h>
#include <Utils/TypeIndex.h>
#include "PlayerInfoPanelCacheEntry.h"
#include "PlayerInfoPanelTypes.h"

template <typename HookContext>
class PlayerInfoPanel {
public:
    explicit PlayerInfoPanel(HookContext& hookContext, cs2::CUIPanel* panel, PlayerInfoPanelCacheEntry& cache) noexcept
        : hookContext{hookContext}
        , panel{panel}
        , cache{cache}
    {
    }

    void drawPlayerInfo(auto&& playerPawn, bool analyzerOnly = false) const noexcept
    {
        updatePanels(std::type_identity<PlayerInfoPanelTypes<HookContext>>{}, playerPawn, analyzerOnly);
        if (const auto opacity = getOpacity(playerPawn); cache.opacity(opacity))
            getPanel().setOpacity(opacity);
    }

    void updatePosition(const cs2::Vector& origin) const noexcept
    {
        const auto positionInClipSpace = hookContext.template make<WorldToClipSpaceConverter>().toClipSpace(origin);
        getPanel().setZIndex(-positionInClipSpace.z);

        constexpr auto kMaxScale{1.0f};
        const auto scale = std::clamp(500.0f / (positionInClipSpace.z / getFovScale() + 400.0f), 0.4f, kMaxScale);

        const auto deviceCoordinates = positionInClipSpace.toNormalizedDeviceCoordinates();
        PanoramaTransformations{
            hookContext.panoramaTransformFactory().scale(scale),
            hookContext.panoramaTransformFactory().translate(deviceCoordinates.getX(), deviceCoordinates.getY())
        }.applyTo(getPanel());
    }

private:
    [[nodiscard]] float getOpacity(auto&& playerPawn) const noexcept
    {
        return playerPawn.hasImmunity().valueOr(false) ? 0.5f : 1.0f;
    }

    [[nodiscard]] float getFovScale() const noexcept
    {
        return ViewToProjectionMatrix{hookContext}.getFovScale();
    }

    template <typename... PanelTypes>
    void updatePanels(std::type_identity<std::tuple<PanelTypes...>>, auto&& playerPawn, bool analyzerOnly) const noexcept
    {
        auto&& childPanels = getPanel().children();
        (updateOnePanel<PanelTypes>(childPanels[utils::typeIndex<PanelTypes, std::tuple<PanelTypes...>>()], playerPawn, analyzerOnly), ...);
    }

    // analyzerOnly = the info-container is being driven ONLY by the CHEAT O METER tag (the
    // Player Info visual itself is off, or this pawn is filtered out by its OnlyEnemies rule):
    // every regular child stays untouched (its own config var keeps it hidden), only the tag
    // panel updates.
    template <typename PanelType>
    void updateOnePanel(auto&& panel, auto&& playerPawn, bool analyzerOnly) const noexcept
    {
        if constexpr (std::is_same_v<PanelType, PlayerAnalyzerTagPanel<HookContext>>) {
            hookContext.template make<PanelType>(panel, cache).update(playerPawn);
        } else if (!analyzerOnly) {
            // Restore a child hidden when this pooled panel was analyzer-only.
            // Its update can then hide it again according to its own setting.
            panel.setVisible(true);
            hookContext.template make<PanelType>(panel, cache).update(playerPawn);
        } else {
            panel.setVisible(false);
        }
    }

    [[nodiscard]] decltype(auto) getPanel() const noexcept
    {
        return hookContext.template make<PanoramaUiPanel>(panel);
    }

    HookContext& hookContext;
    cs2::CUIPanel* panel;
    PlayerInfoPanelCacheEntry& cache;
};
