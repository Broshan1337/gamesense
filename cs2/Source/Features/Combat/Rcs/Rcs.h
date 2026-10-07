#pragma once

#include <CS2/Classes/CUserCmd.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <Features/Combat/Rcs/RecoilCompensation.h>
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
        const auto shots = localPawn.shotsFired();
        if (!shots.hasValue() || shots.value() < 1)
            return;
        const auto punch = localPawn.aimPunchAngle();
        if (!punch.hasValue())
            return;

        // Fresh command angles do not retain last command's correction. Compensate the
        // entire current kick rather than subtracting a delta against stale static state.
        const auto scale = static_cast<float>(GET_CONFIG_VAR(rcs_vars::Strength)) / 100.0f;
        recoil_compensation::apply(cmd, punch.value().x * scale, punch.value().y * scale);
    }

private:
    HookContext& hookContext;
};
