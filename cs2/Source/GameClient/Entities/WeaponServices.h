#pragma once

#include <CS2/Classes/CCSPlayer_WeaponServices.h>
#include <CS2/Classes/Entities/C_CSWeaponBase.h>
#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Constants/EntityHandle.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <MemoryPatterns/PatternTypes/WeaponServicesPatternTypes.h>
#include "BaseWeapon.h"
#include "PlayerWeapons.h"

template <typename HookContext>
class WeaponServices {
public:
    WeaponServices(HookContext& hookContext, cs2::CCSPlayer_WeaponServices* weaponServices) noexcept
        : hookContext{hookContext}
        , weaponServices{weaponServices}
    {
    }

    [[nodiscard]] decltype(auto) weapons() const noexcept
    {
        return hookContext.template make<PlayerWeapons>(hookContext.patternSearchResults().template get<OffsetToWeapons>().of(weaponServices).get());
    }

    [[nodiscard]] auto getActiveWeapon() const noexcept
    {
        return hookContext.template make<BaseWeapon>(static_cast<cs2::C_CSWeaponBase*>(hookContext.template make<EntitySystem>().getEntityFromHandle(hookContext.patternSearchResults().template get<OffsetToActiveWeapon>().of(weaponServices).valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX}))));
    }

    [[nodiscard]] auto activeWeaponHandle() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToActiveWeapon>().of(weaponServices).toOptional();
    }

    // EXPERIMENTAL - raw write to m_hActiveWeapon, the same field activeWeaponHandle() reads.
    // Used to force a holster-then-redeploy cycle (set to invalid, then back to the real
    // handle) on a weapon whose subclass/model was just swapped while already equipped -
    // every prior real success in this project's knife-animation investigation (a rejoin, and
    // a fresh console `give` after this session's subclass swap was already active) happened
    // right after a genuine deploy event, never from modifying an already-equipped weapon's
    // data in place. This is a different field from BaseEntity::cycleOwnerHandle()'s
    // m_hOwnerEntity (weapon ownership, already tried and disproven alone) - this is the
    // player's own "which weapon is currently selected" field, never toggled before.
    void setActiveWeaponHandle(cs2::CEntityHandle handle) const noexcept
    {
        hookContext.patternSearchResults().template get<OffsetToActiveWeapon>().of(weaponServices) = handle;
    }

private:
    HookContext& hookContext;
    cs2::CCSPlayer_WeaponServices* weaponServices;
};
