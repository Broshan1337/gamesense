#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>

// Maps the 0-based player slots that game events carry ("attacker", "userid") to the actual player,
// and gets a display name out of them.
//
// Shared because every event-driven feature needs exactly this and the two non-obvious parts below
// are worth having in one place rather than reimplemented per feature.
template <typename HookContext>
class PlayerSlotLookup {
public:
    explicit PlayerSlotLookup(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // 65535 is the engine's "nobody" marker (world/fall damage); anything outside a real slot
    // range is rejected rather than trusted.
    static constexpr std::int64_t kMaxPlayerSlot = 63;

    [[nodiscard]] static bool isValidSlot(std::int64_t slot) noexcept
    {
        return slot >= 0 && slot <= kMaxPlayerSlot;
    }

    // Player controllers occupy entity indices 1..maxplayers in slot order, so a slot maps to
    // controller entity index slot+1. There is no index->entity lookup available, and the entity
    // classifier does not recognise CCSPlayerController, so this goes via the pawns (which it does
    // recognise) and matches on each pawn's own controller.
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

    // m_iszPlayerName is a fixed char array inside the controller, not a pointer, so this reads
    // bytes in-object and never dereferences anything. That matters: if the schema lookup fails, or
    // the field is ever something other than an array, the worst case is unreadable bytes - which
    // the printable check rejects - rather than a fault.
    //
    // The returned string is attacker-controlled and may contain anything, including percent signs.
    // It must never be used as a printf format - see ChatPrinter, which always goes through "%s".
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
