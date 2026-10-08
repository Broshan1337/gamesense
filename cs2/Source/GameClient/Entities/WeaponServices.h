#pragma once

#include <cstddef>

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
        cs2::CUtlVector<cs2::CEntityHandle>* handles = nullptr;
        if (weaponServices) {
            const auto offset = hookContext.schemaSystem().getFieldOffset("CPlayer_WeaponServices", "m_hMyWeapons");
            if (offset.has_value() && *offset > 0)
                handles = reinterpret_cast<cs2::CUtlVector<cs2::CEntityHandle>*>(reinterpret_cast<std::byte*>(weaponServices) + *offset);
        }
        return hookContext.template make<PlayerWeapons>(handles);
    }

    [[nodiscard]] auto getActiveWeapon() const noexcept
    {
        return hookContext.template make<BaseWeapon>(static_cast<cs2::C_CSWeaponBase*>(hookContext.template make<EntitySystem>().getEntityFromHandle(hookContext.patternSearchResults().template get<OffsetToActiveWeapon>().of(weaponServices).valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX}))));
    }

    [[nodiscard]] auto activeWeaponHandle() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToActiveWeapon>().of(weaponServices).toOptional();
    }

    
    
    
    
    
    
    
    
    
    void setActiveWeaponHandle(cs2::CEntityHandle handle) const noexcept
    {
        hookContext.patternSearchResults().template get<OffsetToActiveWeapon>().of(weaponServices) = handle;
    }

private:
    HookContext& hookContext;
    cs2::CCSPlayer_WeaponServices* weaponServices;
};
