#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Sound/HitSoundConfigVariables.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>

// Plays a short bell whenever the LOCAL player damages someone. Driven from the
// FireEventClientSide hook, which until now had no feature logic hanging off it at all.
//
// Deliberately only fires for damage WE deal: player_hurt is broadcast to every client for
// every player, so without the attacker check this would ring on every hit in the server.
template <typename HookContext>
class HitSound {
public:
    explicit HitSound(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        if (!GET_CONFIG_VAR(HitSoundEnabled))
            return;

        if (!game_events::is(event, "player_hurt"))
            return;

        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        play();
    }

private:
    // Played through the engine's client command buffer rather than a sound-emitter API - far
    // less machinery, and it's what a known-working reference implementation of this same
    // feature does. `play` takes the sound path WITHOUT its .vsnd_c extension.
    //
    // sounds/training/timer_bell is a real shipped asset (verified by enumerating pak01_dir.vpk's
    // directory tree). Other verified-present alternatives if this one doesn't suit:
    // sounds/training/bell_impact (the previous choice), sounds/training/bell_normal,
    // sounds/buttons/bell1 (short classic UI bell), sounds/ui/coin_pickup_01.
    //
    // Note the command is queued, not executed inline: ExecuteClientCommand formats the string
    // and hands it to the engine's command buffer (Cbuf_AddText equivalent), which drains on the
    // next frame pass. That costs ~1 frame and is not perceptible.
    void play() const noexcept
    {
        hookContext.template make<EngineCommandExecutor>().execute("play sounds/training/timer_bell");
    }

    HookContext& hookContext;
};
