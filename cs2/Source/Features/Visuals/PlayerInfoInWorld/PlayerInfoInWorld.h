#pragma once

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Common/InWorldPanels.h>
#include <Features/Game/PlayerAnalyzer/CheatOMeterState.h>
#include <Features/Game/PlayerAnalyzer/PlayerAnalyzerConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <Hooks/ViewRenderHook.h>

#include "PlayerInfoInWorldContext.h"
#include "PlayerInfoInWorldState.h"
#include "PlayerPositionArrow/PlayerPositionArrowColorType.h"
#include "PlayerStateIcons/PlayerStateIconsToShow.h"

template <typename HookContext>
class PlayerInfoInWorld {
public:
    PlayerInfoInWorld(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void drawPlayerInformation(auto&& playerPawn) noexcept
    {
        // Deathmatch leaves old pawns behind during respawns. Only render the
        // controller's current, confirmed living pawn.
        if (!playerPawn.isAlive().value_or(false) || !playerPawn.health().greaterThan(0).valueOr(false))
            return;
        if (static_cast<cs2::C_BaseEntity*>(playerPawn.playerController().pawn()) != playerPawn.rawPawn())
            return;
        const bool infoRun = shouldRun();
        const bool analyzerTag = shouldDrawAnalyzerTag(playerPawn);
        if (!infoRun && !analyzerTag)
            return;
        if (infoRun && !shouldDrawInfoOnPawn(playerPawn) && !analyzerTag)
            return;

        const auto absOrigin = playerPawn.absOrigin();
        if (!absOrigin.hasValue())
            return;

        const auto positionInClipSpace = hookContext.template make<WorldToClipSpaceConverter>().toClipSpace(absOrigin.value());
        if (!positionInClipSpace.onScreen())
            return;

        auto&& playerInformationPanel = hookContext.template make<InWorldPanels>().getNextPlayerInfoPanel();
        
        
        playerInformationPanel.drawPlayerInfo(playerPawn, !infoRun || !shouldDrawInfoOnPawn(playerPawn));
        playerInformationPanel.updatePosition(absOrigin.value());
    }

private:
    [[nodiscard]] decltype(auto) context() const noexcept
    {
        return hookContext.template make<PlayerInfoInWorldContext>();
    }

    [[nodiscard]] bool shouldRun() const noexcept
    {
        return context().config().template getVariable<player_info_vars::Enabled>();
    }

    [[nodiscard]] bool shouldDrawInfoOnPawn(auto&& playerPawn) const noexcept
    {
        return playerPawn.isAlive().value_or(true)
            && playerPawn.health().greaterThan(0).valueOr(true)
            && !playerPawn.isControlledByLocalPlayer()
            && playerPawn.isTTorCT()
            && (!context().config().template getVariable<player_info_vars::OnlyEnemies>() || playerPawn.isEnemy().value_or(true));
    }

    
    
    
    [[nodiscard]] bool shouldDrawAnalyzerTag(auto&& playerPawn) const noexcept
    {
        if (!context().config().template getVariable<analyzer_vars::Enabled>() || !context().config().template getVariable<analyzer_vars::EspTag>())
            return false;
        if (playerPawn.isControlledByLocalPlayer() || !playerPawn.isTTorCT())
            return false;
        const auto health = playerPawn.health();
        if (health.hasValue() && health.value() <= 0)
            return false;
        auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(playerPawn.playerController().baseEntity());
        if (!controllerEntity)
            return false;
        
        const auto slot = static_cast<int>(hookContext.template make<BaseEntity>(controllerEntity).handle().index().value) - 1;
        return cheat_ometer::tagForSlot(slot).active;
    }

    HookContext& hookContext;
};
