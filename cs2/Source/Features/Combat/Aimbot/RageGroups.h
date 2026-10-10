#pragma once

#include <Features/Combat/Aimbot/RageGroupsConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>

// Runtime resolution of the per-weapon-group rage config (velocity-cs2
// ragebot::get_group). The active weapon's VData type selects one of the six
// groups; the resolved struct is what the Aimbot consumes so no feature reads
// a stale global knob. Non-gun weapons (knife/grenade/C4/taser) resolve to an
// invalid Active and the ragebot stands down for them, like velocity.

namespace rage_groups
{

inline constexpr int kGroupCount = 6;

struct Active {
    bool valid{false};
    bool enabled{false};
    bool multipoint{false};
    int pointScale{0};
    bool dynamicPointscale{false};
    bool bodyAim{false};
    bool forceShot{false};
    bool forceShotAir{false};
    int hitchance{0};
    int minDamage{0};
    bool hitHead{false};
    bool hitChest{false};
    bool hitStomach{false};
    bool hitArms{false};
    bool hitLegs{false};
    bool doubletap{false};
};

// pistol=1 smg=2 rifle=3 shotgun=4 sniper=5 lmg=6 (velocity cstypes weapon_type).
// Returns the group index 0..5 or -1 when the weapon is not a gun.
[[nodiscard]] inline constexpr int groupIndexForWeaponType(int weaponType) noexcept
{
    const int idx = weaponType - 1;
    return (idx >= 0 && idx < kGroupCount) ? idx : -1;
}

template <template <typename> class EnabledType, typename Group, typename HookContext>
[[nodiscard]] Active readActive(HookContext& hookContext) noexcept
{
    Active out;
    out.valid = true;
    out.enabled = GET_CONFIG_VAR(EnabledType<Group>);
    out.multipoint = GET_CONFIG_VAR(Multipoint<Group>);
    out.pointScale = static_cast<int>(GET_CONFIG_VAR(PointScale<Group>));
    out.dynamicPointscale = GET_CONFIG_VAR(DynamicPointscale<Group>);
    out.bodyAim = GET_CONFIG_VAR(BodyAim<Group>);
    out.forceShot = GET_CONFIG_VAR(ForceShot<Group>);
    out.forceShotAir = GET_CONFIG_VAR(ForceShotAir<Group>);
    out.hitchance = static_cast<int>(GET_CONFIG_VAR(Hitchance<Group>));
    int minDamage = static_cast<int>(GET_CONFIG_VAR(MinDamage<Group>));
    if (Bind::isDown(static_cast<int>(GET_CONFIG_VAR(MinDamageOverrideKey<Group>))))
        minDamage = static_cast<int>(GET_CONFIG_VAR(MinDamageOverrideValue<Group>));
    out.minDamage = minDamage;
    out.hitHead = GET_CONFIG_VAR(HitHead<Group>);
    out.hitChest = GET_CONFIG_VAR(HitChest<Group>);
    out.hitStomach = GET_CONFIG_VAR(HitStomach<Group>);
    out.hitArms = GET_CONFIG_VAR(HitArms<Group>);
    out.hitLegs = GET_CONFIG_VAR(HitLegs<Group>);
    out.doubletap = GET_CONFIG_VAR(Doubletap<Group>);
    return out;
}

template <typename HookContext, typename Weapon>
[[nodiscard]] Active active(HookContext& hookContext, Weapon&& weapon) noexcept
{
    const auto type = weapon.weaponType();
    if (!type.hasValue())
        return {};
    // Specific-weapon override (RageWeaponDefs.h): when the weapon's own config
    // is enabled it replaces the weapon-type group's for every field.
    if (const auto def = weapon.itemDefinitionIndex(); def.hasValue()) {
#define RAGE_WEAPON_OVERRIDE_X(W, defIndex, pretty) \
        case defIndex: \
            if (GET_CONFIG_VAR(OverrideEnabled<rage_groups::W>)) \
                return readActive<OverrideEnabled, rage_groups::W>(hookContext); \
            break;
        switch (def.value()) {
            RAGE_WEAPON_CONFIG_LIST_NS(RAGE_WEAPON_OVERRIDE_X)
        default: break;
        }
#undef RAGE_WEAPON_OVERRIDE_X
    }
    switch (groupIndexForWeaponType(type.value())) {
    case 0: return readActive<Enabled, Pistol>(hookContext);
    case 1: return readActive<Enabled, Smg>(hookContext);
    case 2: return readActive<Enabled, Rifle>(hookContext);
    case 3: return readActive<Enabled, Shotgun>(hookContext);
    case 4: return readActive<Enabled, Sniper>(hookContext);
    case 5: return readActive<Enabled, Lmg>(hookContext);
    default: return {};
    }
}

}
