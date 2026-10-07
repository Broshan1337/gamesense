#pragma once

#include <algorithm>
#include <cstring>
#include <ctime>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Visuals/PlayerList/PlayerListConfigVariables.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <GameClient/Entities/PlayerController.h>
#include <UI/ImGui/GuiLog.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/FieldOffset.h>
#include <Utils/StringBuilder.h>












template <typename HookContext>
class PlayerList {
public:
    explicit PlayerList(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        
        
        
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
        if (!nameOffset.has_value() || !maxHealthOffset.has_value()) {
            
            
            
            static std::int64_t lastDiagNs = 0;
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const std::int64_t nowNs = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
            if (nowNs - lastDiagNs > 10'000'000'000LL) {
                lastDiagNs = nowNs;
                gui_log::write("[matchdiag] schema lookup failed: CCSPlayerController.m_iszPlayerName -> %s",
                    hookContext.schemaSystem().diagnoseFieldLookup("CCSPlayerController", "m_iszPlayerName"));
            }
            return;
        }

        player_list::Row rows[player_list::kMaxRows];
        int rowSlot = 0;

        
        
        
        int chainEntities = 0;
        int chainControllers = 0;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            ++chainEntities;
            if (rowSlot >= player_list::kMaxRows)
                return;

            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::CCSPlayerController>())
                return;
            ++chainControllers;

            auto&& controller = hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(entityIdentity.entity));
            auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(entityIdentity.entity);
            const auto controllerBytes = reinterpret_cast<const std::byte*>(controllerEntity);

            auto& row = rows[rowSlot];
            
            
            
            
            row.slot = static_cast<int>(hookContext.template make<BaseEntity>(controllerEntity).handle().index().value) - 1;

            
            
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

            
            
            
            
            
            if (actionTrackingOffset.has_value() && matchStatsOffset.has_value() && killsOffset.has_value()) {
                if (const void* actionTracking = readPointer(controllerBytes + *actionTrackingOffset))
                    row.kills = readInt(static_cast<const std::byte*>(actionTracking) + *matchStatsOffset + *killsOffset);
            }

            row.isLocalPlayer = controller == hookContext.localPlayerController();
            row.observerMode = -1;
            
            
            row.team = static_cast<int>(controller.teamNumber());

            
            
            
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

            
            
            row.teamDamage = TeamDamageTracker<HookContext>::totalTeamDamageForSlot(row.slot);

            ++rowSlot;
        });

        
        
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

        
        {
            static std::int64_t lastDiagNs = 0;
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const std::int64_t nowNs = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
            if (lastDiagNs == 0 || nowNs - lastDiagNs > 10'000'000'000LL) {
                lastDiagNs = nowNs;
                gui_log::write("[chaindiag] plist: classifierInit=%d identities=%d controllers=%d rows=%d",
                    hookContext.entityClassifier().initialized() ? 1 : 0, chainEntities, chainControllers, rowSlot);
            }
        }
    }

private:
    
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
