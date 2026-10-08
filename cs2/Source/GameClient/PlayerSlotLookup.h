#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>






template <typename HookContext>
class PlayerSlotLookup {
public:
    explicit PlayerSlotLookup(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    static constexpr std::int64_t kMaxPlayerSlot = 63;

    [[nodiscard]] static bool isValidSlot(std::int64_t slot) noexcept
    {
        return slot >= 0 && slot <= kMaxPlayerSlot;
    }

    
    
    
    
    [[nodiscard]] auto pawnBySlot(std::int64_t slot) const noexcept
    {
        cs2::C_BaseEntity* foundEntity = nullptr;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (foundEntity)
                return;

            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;

            if (baseEntity.template as<PlayerPawn>().playerController().baseEntity().handle().index().value == slot + 1)
                foundEntity = static_cast<cs2::C_BaseEntity*>(entityIdentity.entity);
        });

        return hookContext.template make<BaseEntity>(foundEntity).template as<PlayerPawn>();
    }

    
    
    
    
    
    
    
    [[nodiscard]] const char* nameBySlot(std::int64_t slot) const noexcept
    {
        auto&& pawn = pawnBySlot(slot);
        if (!pawn)
            return kUnknownName;

        cs2::C_BaseEntity* const controller = pawn.playerController().baseEntity();
        if (!controller)
            return kUnknownName;

        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iszPlayerName");
        if (!offset.has_value() || *offset <= 0)
            return kUnknownName;

        const auto name = reinterpret_cast<const char*>(reinterpret_cast<const std::byte*>(controller) + *offset);
        return looksLikeName(name) ? name : kUnknownName;
    }

private:
    static constexpr const char* kUnknownName = "Player";

    [[nodiscard]] static bool looksLikeName(const char* name) noexcept
    {
        if (!name || *name == '\0')
            return false;

        constexpr int kMaxNameLength = 128;
        for (int i = 0; i < kMaxNameLength; ++i) {
            if (name[i] == '\0')
                return i > 0;
            if (static_cast<unsigned char>(name[i]) < 0x20)
                return false;
        }
        return false;
    }

    HookContext& hookContext;
};
