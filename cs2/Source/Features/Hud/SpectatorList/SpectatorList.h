#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Hud/SpectatorList/SpectatorListParams.h>
#include <Features/Hud/SpectatorList/SpectatorSnapshot.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/VerifyConsole.h>
















template <typename HookContext>
class SpectatorList {
public:
    explicit SpectatorList(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        char names[spectator_list::kMaxNames][40]{};
        int count = 0;
        bool spectatingOthers = false;

        if (GET_CONFIG_VAR(spectator_list_params::SpectatorListEnabled))
            collectSpectators(names, count, spectatingOthers);

        spectator_list::publish(names, count, spectatingOthers);
    }

private:
    void collectSpectators(char (&names)[spectator_list::kMaxNames][40], int& count, bool& spectatingOthers) const noexcept
    {
        count = 0;
        spectatingOthers = false;

        
        
        
        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pObserverServices");
        const auto targetOffset = hookContext.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_hObserverTarget");
        const auto nameOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iszPlayerName");
        const auto healthOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_iHealth");
        if (!servicesOffset.has_value() || !targetOffset.has_value() || !nameOffset.has_value() || !healthOffset.has_value()) {
            VerifyConsole::write(30.0f, "spec", "schema offsets unresolved - spectator detection inactive");
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return;

        std::uint32_t povHandle = localPawn.baseEntity().handle().value;
        if (povHandle == 0)
            return;

        
        
        const auto* const localEntity = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity()));
        int localHealth{};
        std::memcpy(&localHealth, localEntity + *healthOffset, sizeof(localHealth));
        if (localHealth <= 0) {
            void* localServices{};
            std::memcpy(&localServices, localEntity + *servicesOffset, sizeof(localServices));
            if (localServices) {
                std::uint32_t watchedHandle{};
                std::memcpy(&watchedHandle, reinterpret_cast<const std::byte*>(localServices) + *targetOffset, sizeof(watchedHandle));
                if (watchedHandle != 0 && watchedHandle != povHandle) {
                    povHandle = watchedHandle;
                    spectatingOthers = true;
                }
            }
        }

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (count >= spectator_list::kMaxNames)
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = baseEntity.template as<PlayerPawn>();
            if (!pawn || pawn.isControlledByLocalPlayer())
                return;

            auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(pawn.baseEntity());
            if (!pawnEntity)
                return;

            void* observerServices{};
            std::memcpy(&observerServices, reinterpret_cast<const std::byte*>(pawnEntity) + *servicesOffset, sizeof(observerServices));
            if (!observerServices)
                return;   

            std::uint32_t targetHandleValue{};
            std::memcpy(&targetHandleValue, reinterpret_cast<const std::byte*>(observerServices) + *targetOffset, sizeof(targetHandleValue));
            if (targetHandleValue != povHandle)
                return;

            auto&& controller = pawn.playerController().baseEntity();
            if (!controller)
                return;
            const char* name = "?";
            const auto controllerName = reinterpret_cast<const char*>(reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(controller)) + *nameOffset);
            if (looksLikeName(controllerName))
                name = controllerName;

            
            
            std::size_t i = 0;
            for (; name[i] != '\0' && i < sizeof(names[0]) - 1; ++i)
                names[count][i] = name[i];
            names[count][i] = '\0';
            ++count;
        });
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
