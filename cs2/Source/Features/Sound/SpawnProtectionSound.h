#pragma once

#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Sound/SpawnProtectionSoundConfigVariables.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>

// Plays a bell the moment spawn protection runs out, as an audio cue that you can now be shot.
//
// Unlike the other event features here, the interesting moment is NOT an event: nothing fires when
// protection expires. round_freeze_end only tells us when the window STARTS, so the expiry has to
// be noticed by watching the clock each frame - hence run(), called from the render hook.
//
// Deliberately sound only, no on-screen indicator.
template <typename HookContext>
class SpawnProtectionSound {
public:
    explicit SpawnProtectionSound(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event || !GET_CONFIG_VAR(SpawnProtectionSoundEnabled))
            return;

        if (!game_events::is(event, "round_freeze_end"))
            return;

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        armed = true;
        endTime = now.value() + kProtectionSeconds;
    }

    // Called every frame. Cheap when disarmed, which is almost always.
    void run() const noexcept
    {
        if (!armed)
            return;

        // Checked here rather than at arm time so that turning the feature off mid-round silently
        // disarms instead of leaving a bell queued to fire later.
        if (!GET_CONFIG_VAR(SpawnProtectionSoundEnabled)) {
            armed = false;
            return;
        }

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        // curtime jumps backwards on a map change or reconnect, which would otherwise leave this
        // armed with an end time far in the future and no bell for the rest of the session.
        if (now.value() < endTime - kProtectionSeconds) {
            armed = false;
            return;
        }

        if (now.value() < endTime)
            return;

        // Disarm BEFORE playing, so the bell can only ever fire once per round even if the command
        // below fails or the frame is re-entered.
        armed = false;
        hookContext.template make<EngineCommandExecutor>().execute("play sounds/training/timer_bell");
    }

private:
    // Matches the reference implementation's window. Not read from the game - CS2 does not expose a
    // spawn-protection duration to the client - so it is an assumption about the server, and will
    // be wrong on servers configured differently.
    static constexpr float kProtectionSeconds = 5.0f;

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib. Kept with
    // the feature rather than in the shared feature-states struct so it can be removed in one piece.
    inline static bool armed = false;
    inline static float endTime = 0.0f;

    HookContext& hookContext;
};
