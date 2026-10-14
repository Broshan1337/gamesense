#pragma once

#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <Features/Combat/Rcs/RecoilCompensation.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <HookContext/HookContextMacros.h>

template <typename HookContext>
class Rcs {
public:
    explicit Rcs(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(rcs_vars::Enabled) || !UserCmd{cmd})
            return;
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true)
            return;
        // Snipers and shotguns are ALWAYS excluded (friend report, 2026-10-13):
        // scoped kicks fight the compensation and drag the crosshair off the
        // aim point, shotgun pellets need the raw spread. Hardcoded - no toggle.
        // weaponType: 1 pistol, 2 smg, 3 rifle, 4 shotgun, 5 sniper, 6 lmg.
        const auto weaponType = localPawn.getActiveWeapon().weaponType();
        if (localPawn.isUsingSniperRifle() || (weaponType.hasValue() && weaponType.value() == 4))
            return;
        const auto shots = localPawn.shotsFired();
        if (!shots.hasValue() || shots.value() < 1)
            return;
        const auto punch = localPawn.aimPunchAngle();
        if (!punch.hasValue() || !__builtin_isfinite(punch.value().x) || !__builtin_isfinite(punch.value().y))
            return;

        // Full current punch, scaled per axis. 100 = exact compensation (every
        // bullet lands at the crosshair); the old 200 default over-compensated
        // 2x (camera dragged, shots scattered an arc above the aim point).
        const float pitchScale = static_cast<float>(GET_CONFIG_VAR(rcs_vars::Strength)) / 100.0f;
        const float yawScale = static_cast<float>(GET_CONFIG_VAR(rcs_vars::YawStrength)) / 100.0f;
        recoil_compensation::apply(cmd, punch.value().x * pitchScale, punch.value().y * yawScale);
    }

    HookContext& hookContext;
};