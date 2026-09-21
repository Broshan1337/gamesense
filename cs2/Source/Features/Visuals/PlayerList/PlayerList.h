#pragma once

#include <algorithm>
#include <cstring>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Visuals/PlayerList/PlayerListConfigVariables.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <GameClient/Entities/PlayerController.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/FieldOffset.h>
#include <Utils/StringBuilder.h>

// FrameworkCS2 port (Source/Features/PlayerList), trimmed to the columns whose data this
// project can already reach safely: name, team, health, money, ping, competitive rank, team
// damage (shared with TeamDamageTracker's records) and observer mode. Rendering happens on the
// present thread (Neverlose render -> ImGui table); this feature only builds the snapshot.
//
// The walk is CONTROLLER-ANCHORED: controllers exist for every connected player for the whole
// session - dead or alive - and every controller-side field (name, ping, rank, money, kills) is
// read directly from the controller. The pawn is resolved through the controller's m_hPlayerPawn
// handle, never by walking pawns and reading their controller back. The pawn-first walk of the
// original port dropped players whenever their pawn went missing while dead, which emptied them
// out of this list AND out of the CHEAT O METER picker (both feed from this snapshot).
template <typename HookContext>
class PlayerList {
public:
    explicit PlayerList(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        // ALWAYS gathers: the snapshot also feeds the Discord RPC's match state (alive/dead,
        // team sizes), which must work whether or not the Player List visual is toggled on.
        // The visual itself is gated by PlayerListEnabled in drawPlayerListWindow.
        const auto nameOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iszPlayerName");
        const auto moneyServicesOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_pInGameMoneyServices");
        const auto accountOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController_InGameMoneyServices", "m_iAccount");
        const auto pingOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iPing");
        const auto rankOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRanking");
        const auto rankTypeOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRankType");
        const auto maxHealthOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_iMaxHealth");
        const auto observerServicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pObserverServices");
        const auto observerModeOffset = hookContext.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_iObserverMode");
        const auto actionTrackingOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_pActionTrackingServices");
        const auto matchStatsOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController_ActionTrackingServices", "m_matchStats");
        const auto killsOffset = hookContext.schemaSystem().getFieldOffset("CSPerRoundStats_t", "m_iKills");
        if (!nameOffset.has_value() || !maxHealthOffset.has_value())
            return;

        player_list::Row rows[player_list::kMaxRows];
        int rowSlot = 0;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (rowSlot >= player_list::kMaxRows)
                return;

            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::CCSPlayerController>())
                return;

            auto&& controller = hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(entityIdentity.entity));
            auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(entityIdentity.entity);
            const auto controllerBytes = reinterpret_cast<const std::byte*>(controllerEntity);

            auto& row = rows[rowSlot];
            // The picker key: the CONTROLLER's slot (controller entity index - 1), the same
            // 0-based numbering game events carry. NOT the pawn's entity index - pawns do
            // NOT occupy 1..maxplayers, their indices land anywhere in the entity list and
            // overflow a slot-sized key space (the CHEAT O METER picker bug of 2026-09-13).
            row.slot = static_cast<int>(hookContext.template make<BaseEntity>(controllerEntity).handle().index().value) - 1;

            // Controller-side fields: name, ping, rank, money, kills. All of these exist for the
            // whole session, dead or alive - a dead player keeps their full row with alive=false.
            copyName(row.name, controllerBytes + *nameOffset);
            if (pingOffset.has_value())
                row.ping = readInt(controllerBytes + *pingOffset);
            if (rankOffset.has_value())
                row.rank = readInt(controllerBytes + *rankOffset);
            if (rankTypeOffset.has_value())
                row.rankType = readInt(controllerBytes + *rankTypeOffset);
            if (accountOffset.has_value() && moneyServicesOffset.has_value()) {
                const void* moneyServices = readPointer(controllerBytes + *moneyServicesOffset);
                if (moneyServices)
                    row.money = readInt(static_cast<const std::byte*>(moneyServices) + *accountOffset);
            }

            // Kills (the scoreboard's K column) come from the CONTROLLER's action-tracking
            // services -> m_matchStats -> CSPerRoundStats_t::m_iKills (FrameworkCS2's
            // KillsColumn reads the same chain). The PAWN-side
            // CCSPlayer_ActionTrackingServices::m_iKills is NOT networked - it stays 0 on
            // the client, verified in-game.
            if (actionTrackingOffset.has_value() && matchStatsOffset.has_value() && killsOffset.has_value()) {
                if (const void* actionTracking = readPointer(controllerBytes + *actionTrackingOffset))
                    row.kills = readInt(static_cast<const std::byte*>(actionTracking) + *matchStatsOffset + *killsOffset);
            }

            row.isLocalPlayer = controller == hookContext.localPlayerController();
            row.observerMode = -1;
            // The controller's own m_iTeamNum (C_BaseEntity field) is the death-proof team
            // source; the pawn's team overrides it when the pawn is resolvable.
            row.team = static_cast<int>(controller.teamNumber());

            // Pawn-side fields (health, max health, observer mode) resolve THROUGH the
            // controller's m_hPlayerPawn handle. If the pawn is missing entirely (mid-transition)
            // the row survives with alive=false instead of the player vanishing.
            if (auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(controller.pawn())) {
                auto&& pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(pawnEntity));
                const auto pawnBytes = reinterpret_cast<const std::byte*>(pawnEntity);
                row.health = pawn.health().valueOr(0);
                row.alive = row.health > 0;
                row.team = static_cast<int>(pawn.teamNumber());
                if (maxHealthOffset.has_value())
                    row.maxHealth = readInt(pawnBytes + *maxHealthOffset);
                if (observerServicesOffset.has_value() && observerModeOffset.has_value()) {
                    const void* observerServices = readPointer(pawnBytes + *observerServicesOffset);
                    if (observerServices)
                        row.observerMode = readInt(static_cast<const std::byte*>(observerServices) + *observerModeOffset);
                }
            }

            // Team damage done to their own team, from the tracker's per-slot records (keyed by
            // the same controller slot the player_hurt events carry).
            row.teamDamage = TeamDamageTracker<HookContext>::totalTeamDamageForSlot(row.slot);

            ++rowSlot;
        });

        // Group the teams: CT block on top, T block below (stable, so each block keeps the
        // entity-loop order inside the team). CT=3 sorts before T=2; anything else sorts last.
        for (int i = 1; i < rowSlot; ++i) {
            const auto key = rows[i];
            int j = i - 1;
            while (j >= 0 && teamOrderKey(rows[j].team) > teamOrderKey(key.team)) {
                rows[j + 1] = rows[j];
                --j;
            }
            rows[j + 1] = key;
        }

        player_list::publish(rows, rowSlot);
    }

private:
    // Team grouping order: CT (3) first, T (2) second, anything else last.
    [[nodiscard]] static constexpr int teamOrderKey(int team) noexcept
    {
        return team == 3 ? 0 : team == 2 ? 1 : 2;
    }

    [[nodiscard]] static int readInt(const std::byte* address) noexcept
    {
        if (!address)
            return 0;
        int value = 0;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    [[nodiscard]] static const void* readPointer(const std::byte* address) noexcept
    {
        if (!address)
            return nullptr;
        const void* pointer = nullptr;
        std::memcpy(&pointer, address, sizeof(pointer));
        return pointer;
    }

    static void copyName(char* destination, const std::byte* source) noexcept
    {
        if (!source)
            return;
        // Player names are attacker-controlled bytes; same sanity idea as SpectatorList.
        if (!looksLikeName(reinterpret_cast<const char*>(source)))
            return;
        std::strncpy(destination, reinterpret_cast<const char*>(source), 39);
        destination[39] = '\0';
    }

    [[nodiscard]] static bool looksLikeName(const char* name) noexcept
    {
        if (!name || name[0] == '\0')
            return false;
        for (int i = 0; i < 32 && name[i] != '\0'; ++i) {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c < 0x20 && c != '\t')
                return false;
        }
        return true;
    }

    HookContext& hookContext;
};
