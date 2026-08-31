#pragma once

#include <GameClient/EngineCommandExecutor.h>

// Plays one short sound the first time a frame renders after injection - a "we're in" confirmation
// that does not need the menu opened to be seen.
//
// Driven from the render hook rather than from finishInit(), and that is the point: init runs
// inside a PeepEvents call that can land very early, while the engine's command buffer is what
// actually carries the `play` command and only drains during normal frame processing. Waiting for
// the first rendered frame means the engine is demonstrably running by the time we ask it for
// anything.
//
// Deliberately has no config toggle. Every other feature has one, but a setting for this could
// never be used: the sound has already played by the time the menu can be opened to turn it off.
template <typename HookContext>
class WelcomeSound {
public:
    explicit WelcomeSound(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (played)
            return;

        played = true;
        hookContext.template make<EngineCommandExecutor>().execute(kSound);
    }

private:
    // The same short bell the hit sound uses (sounds/training/timer_bell) - a soft "we're in" chime
    // instead of the loud covert case-reveal fanfare that used to play here. Verified present by
    // enumerating pak01_dir.vpk's directory tree. `play` takes the path WITHOUT its .vsnd_c extension.
    //
    // Verified-present alternatives if this wears thin: sounds/ui/item_reveal6_ancient (the old loud
    // fanfare), sounds/ui/achievement_earned (short chime), sounds/buttons/bell1 (classic UI bell),
    // sounds/ui/lobby_notification_joined (subtle blip).
    static constexpr auto kSound = "play sounds/training/timer_bell";

    // No reset on unload is needed: unloading now unmaps the library, so a re-injection gets fresh
    // .bss and greets you again on its own. See Platform/Linux/LinuxSelfUnload.h.
    //
    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static bool played{false};

    HookContext& hookContext;
};
