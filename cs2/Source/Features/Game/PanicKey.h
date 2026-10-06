#pragma once

#include <cstdint>

#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/Triggerbot/Triggerbot.h>
#include <Features/Game/PanicKeyConfigVariables.h>
#include <GameClient/Bind.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/VerifyConsole.h>

// The combat panic key (CS2-settings-style bind, captured in the UI). While ENGAGED, every
// combat/movement feature short-circuits at the entry-point level (see EntryPoints.h) - the
// aimbots never run, the triggerbot disarms, movement assists stand down - leaving pure vanilla
// input. Toggling the bind again releases the panic.
//
// This is a TOGGLE, not hold-to-panic: mid-match you want to flick it with the least motion, and
// an unattended held key that quietly re-enables everything on release would defeat its purpose.
// State is static (one global), polled once per user command so a rising edge toggles exactly
// once regardless of how many entry points ask.
//
// Scope: deliberately combat + movement only. Visuals, glow, skin changer, sound and radio keep
// running while panicked - those are not what "make this stop shooting/aiming right now" means.
template <typename HookContext>
class PanicKey {
public:
    explicit PanicKey(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Polled from CreateMove (every tick, game thread): updates the toggle state.
    void run() const noexcept
    {
        const int bindValue = GET_CONFIG_VAR(panic_vars::Bind);
        if (bindValue <= Bind::kOff || bindValue > Bind::kLast) {
            // 2026-10-05 trap fix (dave): an ENGAGED panic with the key unbound used to be
            // unrecoverable - the release path needs a pressable bound key, and a config
            // that lost the bind (fresh default.cfg, a rebind capture that never fired)
            // left the latch stuck ENGAGED forever ("aimbot completely disabled and won't
            // turn back on"). The panic is a temporary failsafe: with no way to press it,
            // it releases (and the bind can be re-set in the menu afterwards).
            if (panicActive) {
                panicActive = false;
                VerifyConsole::write(0.0f, "panic", "bind removed - combat features re-enabled");
            }
            return;
        }

        const bool down = Bind::isDown(bindValue);

        // Rising edge only. lastKeyWasDown is shared per-feature; changing the bound key while
        // holding the old one can cause one extra edge at most - harmless for a failsafe.
        if (down && !lastKeyWasDown) {
            panicActive = !panicActive;
            if (panicActive)
                engage();
            VerifyConsole::write(0.0f, "panic", "%s - combat features %s",
                panicActive ? "ENGAGED" : "released",
                panicActive ? "disabled" : "enabled");
        }
        lastKeyWasDown = down;
    }

    [[nodiscard]] static bool isActive() noexcept
    {
        return panicActive;
    }

private:
    // Engaging stands down anything with cross-command state, so releasing the panic later
    // cannot fire a stale shot: a half-armed triggerbot timer and a phantom attack-pulse edge
    // would both survive the panic otherwise.
    void engage() const noexcept
    {
        hookContext.template make<Triggerbot>().disarmStatics();
        hookContext.template make<AttackCommand>().reset();
    }

    inline static bool panicActive{false};
    inline static bool lastKeyWasDown{false};

    HookContext& hookContext;
};
