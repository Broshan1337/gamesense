#pragma once

#include <algorithm>
#include <cstring>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Visuals/PlayerList/PlayerListConfigVariables.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/FieldOffset.h>
#include <Utils/StringBuilder.h>

// FrameworkCS2 port (Source/Features/PlayerList), trimmed to the columns whose data this
// project can already reach safely: name, team, health, money, ping, competitive rank, team
// damage (shared with TeamDamageTracker's records) and observer mode. Rendering happens on the
// present thread (Neverlose render -> ImGui table); this feature only builds the snapshot.
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
        if (!nameOffset.has_value() || !maxHealthOffset.has_value())
            return;

        player_list::Row rows[player_list::kMaxRows];
        int rowSlot = 0;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (rowSlot >= player_list::kMaxRows)
                return;

            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::C_CSPlayerPawn>())
                return;

            auto&& pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(entityIdentity.entity));

            auto& row = rows[rowSlot];
            row.health = pawn.health().valueOr(0);
            row.alive = row.health > 0;
            row.team = static_cast<int>(pawn.teamNumber());
            row.isLocalPlayer = pawn.isControlledByLocalPlayer();
            row.observerMode = -1;

            // Controller-side fields: name, ping, money, rank. The controller may be gone before
            // the pawn is - every read is guarded and defaults to "unknown".
            if (auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(pawn.playerController().baseEntity())) {
                const auto controllerBytes = reinterpret_cast<const std::byte*>(controllerEntity);
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
            }

            if (maxHealthOffset.has_value())
                row.maxHealth = readInt(reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *maxHealthOffset);

            if (observerServicesOffset.has_value() && observerModeOffset.has_value()) {
                const void* observerServices = readPointer(reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *observerServicesOffset);
                if (observerServices)
                    row.observerMode = readInt(static_cast<const std::byte*>(observerServices) + *observerModeOffset);
            }

            // Team damage done to their own team, from the tracker's per-slot records.
            const auto slot = entityIdentity.handle.index().value - 1;
            row.teamDamage = TeamDamageTracker<HookContext>::totalTeamDamageForSlot(slot);

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
