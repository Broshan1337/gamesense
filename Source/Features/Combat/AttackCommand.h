#pragma once

#include <cstddef>
#include <cstdint>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <GameClient/UserCmd.h>

// Fires through the outgoing usercmd itself - the subtick path, with no persistent input state.
//
// One call = one complete attack on THIS command, carried by IN_ATTACK in BOTH button banks of
// buttons_pb (bank1 held + bank2 changed-this-tick), plus the command's raw words so client
// prediction and the recomputed move_crc agree. Setting only bank2 reads as "pressed but not
// held" and the server refuses to fire.
//
// NO explicit CSubtickMoveStep is appended, and attack1_start_history_index is OPTIONAL here.
// Both are measurements, not shortcuts - in-game on build 14177 the command this hook receives
// carries an EMPTY input_history (the old "hist=-1" measurement was right all along), so there is
// nothing for the index to point at and no entries for SubtickShotWriter to rewrite at this hook.
// A bank-only attack still registers: the server fires it through its own fallback ("CSBaseGunFire
// player %s no history - using calculated time", libserver) - exactly a whole-tick press, the same
// shape pre-subtick CS fired with. When a history IS present (other builds / other paths), the
// newest entry is pointed at so the server resolves a real sub-tick time instead of falling back.
//
// This mirrors velocity-cs2's normal-shot fire path: their fire_gun() sets the two button words
// plus attack1_start_history_index and nothing else - explicit press/release steps exist only in
// their doubletap path, and they CLEAR all pre-existing steps before features run rather than
// share the timeline with them. Appending our own press@0.0/release@1.0 pair was the fake-bullets
// regression: it is not what any working reference does for a normal shot, and the step field is
// SHARED with quantized mouse input and the strafer's yaw deltas, so near capacity the pair was
// silently skipped and those ticks went out shaped differently from the rest (intermittent
// registration). With banks alone there is nothing to skip: every firing tick is wire-identical.
//
// It MUST run on the fully-built command - our WriteMoveCrc hook (slot 7, pre-original) -
// because slot 6 rebuilds buttons_pb and subtick_moves from raw input state after CreateMove, so
// anything spliced in earlier is overwritten before it reaches the wire.
//
// THE PULSE (measured, in-game): the server fires on the press TRANSITION, judging continuity
// across commands. IN_ATTACK present on consecutive commands is ONE press - a semi-auto (deagle)
// fires exactly once and nothing follows ("auto shoot shoots once and that's it"), while a real
// click releases between shots. So a press is never emitted on consecutive commands: the tick
// after a press carries no attack (a release - the command is game-built and attack-free unless
// the player is really clicking), which restores the edge for the next shot. Auto weapons are
// unaffected - their own cycle time dominates.
template <typename HookContext>
class AttackCommand {
public:
    explicit AttackCommand(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Clears the cross-command pulse state. Called on unload so a re-injection starts with a fresh
    // press edge (otherwise the first post-inject shot could be eaten as a phantom "release").
    static void reset() noexcept
    {
        pressedLastCommand = false;
    }

    // Returns false only when the command or buttons_pb is unreachable - the caller must not
    // pretend a shot happened. A successful press is an honest one: the banks that make the
    // server fire are written, and the log says exactly which shot shape went out. True is also
    // returned on release ticks (nothing is wrong; this command simply carries no attack).
    bool press(cs2::CUserCmd* cmd) const noexcept
    {
        const UserCmd userCmd{cmd};
        if (!userCmd) {
            pressedLastCommand = false;
            return false;
        }

        // The release half of the pulse: last command pressed, so this one must not - the edge
        // the server's next press needs is created by this gap.
        if (pressedLastCommand) {
            pressedLastCommand = false;
            return true;
        }

        if (!userCmd.pressButtonsBothBanks(cs2::CCSGOInput::Buttons::kAttack))
            return false;
        pressedLastCommand = true;

        // The game's own convention, measured off a real click (history of 2 -> index 1): point at
        // the NEWEST entry. Only written while the game left it at -1; their click wins over ours.
        // On this build the command usually arrives here with an empty input_history and the a1
        // write is skipped - EXCEPT when the rage aimbot staged this shot: its SubtickShotWriter
        // writer runs BEFORE this press in the same hook and resurrects a recycled entry
        // (current_size 0 -> 1), so the write lands and the server resolves the shot along the
        // silent entry. Without history the server fires the bank-only press through its
        // calculated-time fallback (measured, see the header comment).
        if (const auto historySize = userCmd.inputHistorySize(); historySize.hasValue())
            static_cast<void>(userCmd.setAttack1StartHistoryIndex(historySize.value() - 1));
        return true;
    }

private:
    static constexpr const char* kTag = "attack";

    // True when the previous command we touched carried the press. Static for the same reason the
    // triggerbot's armed/fireAtTime are: the pulse spans commands, not objects. Both consumers
    // (triggerbot, force-shot) share it - when they want the same tick, the second one's shot
    // simply becomes the release tick and fires next command.
    inline static bool pressedLastCommand{false};

    HookContext& hookContext;
};
