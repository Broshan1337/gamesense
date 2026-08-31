#pragma once

#include <CS2/Classes/CUserCmd.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Trig.h>

// Standalone recoil control (RCS). During a spray, CS2 kicks the aim punch, which offsets the shot from
// where the crosshair points. This pulls the real view against that kick each tick so bullets keep
// landing on target - the same idea velocity-cs2's standalone RCS uses. It moves the ACTUAL view (unlike
// the Rage aimbot's silent recoil compensation, which corrects only the shot's input_history angle), so
// it also visually holds the crosshair down for the player.
//
// Mechanics (mirrors velocity): only active once a spray is underway (m_iShotsFired > 1). Each tick it
// applies the DELTA of the scaled aim punch since last tick - as the punch grows, (old - new) is a
// downward nudge that exactly tracks the climb; storing the scaled punch as `oldPunch` makes the next
// tick's delta correct. Reset to zero whenever RCS is not running so the next spray starts clean.
template <typename HookContext>
class Rcs {
public:
    explicit Rcs(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(rcs_vars::Enabled)) {
            reset();
            return;
        }

        UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true) {
            reset();
            return;
        }

        // Only during a spray - the first shot has no accumulated punch to fight, and gating here keeps
        // the view still while not firing.
        const auto shots = localPawn.shotsFired();
        if (!shots.hasValue() || shots.value() <= 1) {
            reset();
            return;
        }

        const auto punch = localPawn.aimPunchAngle();
        const auto currentPitch = userCmd.viewPitch();
        const auto currentYaw = userCmd.viewYaw();
        if (!punch.hasValue() || !currentPitch.hasValue() || !currentYaw.hasValue())
            return;

        const auto scale = static_cast<float>(GET_CONFIG_VAR(rcs_vars::Strength)) / 100.0f;
        const auto scaledPitch = punch.value().x * scale;
        const auto scaledYaw = punch.value().y * scale;

        // Apply only the change since last tick, so the compensation tracks the punch as it grows
        // instead of yanking the whole accumulated amount every tick.
        const auto newPitch = currentPitch.value() + (oldPunchPitch - scaledPitch);
        const auto newYaw = trig::normalizeDegrees(currentYaw.value() + (oldPunchYaw - scaledYaw));

        userCmd.setViewAngles(newPitch, newYaw);

        oldPunchPitch = scaledPitch;
        oldPunchYaw = scaledYaw;
    }

private:
    // Forgets the accumulated punch so the next spray's first RCS tick applies the full (from-zero)
    // delta rather than a stale one left over from a previous spray.
    static void reset() noexcept
    {
        oldPunchPitch = 0.0f;
        oldPunchYaw = 0.0f;
    }

    // The scaled punch we compensated last tick (static because the feature object is rebuilt each hook
    // call; there is one local player, so one shared value is correct).
    inline static float oldPunchPitch{0.0f};
    inline static float oldPunchYaw{0.0f};

    HookContext& hookContext;
};
