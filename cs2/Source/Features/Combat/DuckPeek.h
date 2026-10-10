#pragma once

#include <cstdint>

#include <CS2/Classes/CUserCmd.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/VerifyConsole.h>

// Duck peek (velocity misc.cpp duckpeek): hold duck so the lowered hitbox hides
// behind cover at standing-inaccuracy; release the duck for the fire tick so the
// shot goes out standing (the aimbot evaluates the standing cone while armed -
// see Aimbot::prepareSpreadSamples). Split-phase like Slow Walk: staged at
// CreateMove (after the Aimbot, whose staged shot decides press vs release),
// applied as button banks at WriteMoveCrc where slot 6 cannot overwrite them.
// Never touches the player's persistent input state - banks only.
template <typename HookContext>
class DuckPeek {
public:
    explicit DuckPeek(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(bool aimbotShotStaged) const noexcept
    {
        pendingDuck = false;
        pendingRelease = false;

        if (!GET_CONFIG_VAR(aimbot_vars::DuckPeek))
            return;

        auto&& pawn = hookContext.activeLocalPlayerPawn();
        if (!pawn || pawn.isAlive() != true || pawn.isOnGround() != true)
            return;

        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        if (aimbotShotStaged) {
            // Stand up for the shot; re-duck stages again next tick.
            pendingRelease = true;
        } else {
            pendingDuck = true;
        }
    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!pendingDuck && !pendingRelease)
            return;
        if (!cmd)
            return;
        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;
        constexpr std::uint64_t kDuckButton = 1ull << 2;
        if (pendingRelease) {
            userCmd.replaceButtons(kDuckButton, 0);
        } else {
            userCmd.replaceButtons(kDuckButton, kDuckButton);
        }
    }

    void onUnload() const noexcept
    {
        pendingDuck = false;
        pendingRelease = false;
    }

private:
    HookContext& hookContext;
    inline static bool pendingDuck{false};
    inline static bool pendingRelease{false};
};