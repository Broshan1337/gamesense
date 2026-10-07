#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <Utils/Optional.h>

// Reads and writes the movement fields of a user command.
//
// Every accessor tolerates the command, or its base message, being absent - returning an empty
// Optional or doing nothing - because this sits directly on the input path and a null here must
// never become a crash.
class UserCmd {
public:
    explicit UserCmd(cs2::CUserCmd* cmd) noexcept
        : cmd{cmd}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return baseMessage() != nullptr;
    }

    // The yaw the command will actually be sent with. Read from the command rather than from the
    // player entity on purpose: this is the value the server will use, and it is already final by
    // the time anything runs after the original CreateMove.
    [[nodiscard]] Optional<float> viewYaw() const noexcept
    {
        const auto angles = viewAngles();
        if (!angles)
            return {};
        return readFloat(angles, cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset);
    }

    // The pitch counterpart of viewYaw(), for the same reason - the value the sent command carries.
    [[nodiscard]] Optional<float> viewPitch() const noexcept
    {
        const auto angles = viewAngles();
        if (!angles)
            return {};
        return readFloat(angles, cs2::CUserCmd::BaseMessage::ViewAngles::kPitchOffset);
    }

    // Overwrites the view angles the command will be sent with. The angles live in a CMsgQAngle
    // submessage the game has already populated this tick (viewYaw()/viewPitch() read it back), so we
    // overwrite pitch/yaw in place and re-mark the submessage present defensively. Per this command's
    // own pipeline this is the angle the server uses, so writing it steers the shot without touching
    // the rendered view (a silent correction). Does nothing if the submessage is absent.
    void setViewAngles(float pitch, float yaw) const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return;

        std::byte* angles = nullptr;
        std::memcpy(&angles, base + cs2::CUserCmd::BaseMessage::kViewAnglesOffset, sizeof(angles));
        if (!angles)
            return;

        std::memcpy(angles + cs2::CUserCmd::BaseMessage::ViewAngles::kPitchOffset, &pitch, sizeof(pitch));
        std::memcpy(angles + cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, &yaw, sizeof(yaw));

        orHasBit(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, cs2::CUserCmd::BaseMessage::kViewAnglesHasBit);
        setOuterHasBit(cs2::CUserCmd::kBaseMessageHasBit);
    }

    // Reads what the game left in the command's forwardmove field. Used to tell whether the player
    // is already driving this component themselves, so their own input can win over ours.
    [[nodiscard]] Optional<float> forwardMove() const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return {};
        return readFloat(base, cs2::CUserCmd::BaseMessage::kForwardMoveOffset);
    }

    // The leftmove counterpart, used for the same override test.
    [[nodiscard]] Optional<float> leftMove() const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return {};
        return readFloat(base, cs2::CUserCmd::BaseMessage::kLeftMoveOffset);
    }

    [[nodiscard]] Optional<int> mouseDx() const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return {};

        int value{};
        std::memcpy(&value, base + cs2::CUserCmd::BaseMessage::kMouseDxOffset, sizeof(value));
        return value;
    }

    [[nodiscard]] bool isButtonDown(std::uint64_t buttons) const noexcept
    {
        if (!cmd)
            return false;

        std::uint64_t current{};
        std::memcpy(&current, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset, sizeof(current));
        return (current & buttons) != 0;
    }

    // The full first button word - what the player is holding this tick. Needed whole (rather than
    // one mask at a time) by anything that tracks press EDGES across ticks, like the quantized
    // strafer's movement-key tracker.
    [[nodiscard]] std::uint64_t buttonState1() const noexcept
    {
        if (!cmd)
            return 0;

        std::uint64_t current{};
        std::memcpy(&current, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset, sizeof(current));
        return current;
    }

    // Presses buttons by OR-ing into the command's first button word. Never assigns: that word
    // carries every other key the player is holding, and overwriting it would release them all.
    void pressButtons(std::uint64_t buttons) const noexcept
    {
        if (!cmd || !buttons)
            return;

        auto* const word = reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset;

        std::uint64_t current{};
        std::memcpy(&current, word, sizeof(current));
        current |= buttons;
        std::memcpy(word, &current, sizeof(current));
    }

    // Forces buttons down or up across BOTH of the command's first two button words.
    //
    // Both banks, not just the first, and that is not a detail: a jump cleared out of buttonstate1
    // while buttonstate2 still carries it reads as "still held", so the key never comes back up,
    // the next landing is not a fresh press, and the player ends up unable to jump at all. The
    // working reference clears and sets the pair together for exactly this reason.
    //
    // Unlike pressButtons this can take something away from the player, so it is only ever called
    // with the single bit the feature owns.
    void setButtonState(std::uint64_t buttons, bool pressed) const noexcept
    {
        if (!cmd || !buttons)
            return;

        setButtonStateInBank(cs2::CUserCmd::kButtonState1Offset, buttons, pressed);
        setButtonStateInBank(cs2::CUserCmd::kButtonState2Offset, buttons, pressed);
    }

    // Presses buttons in BOTH places the reference's set_buttonstate2 does: the command's own raw
    // word, and - the part that actually matters - `base.buttons_pb`, which is the only copy that
    // reaches the server. Writing just the raw word is too late once slot 6 has copied it; see
    // CUserCmd::BaseMessage::kButtonsPbOffset for why.
    //
    // Bank 2 specifically, because that is the bank the reference triggerbot uses for attack.
    // Returns false if buttons_pb is not reachable, in which case nothing was written anywhere and
    // the caller should not pretend a shot happened.
    [[nodiscard]] bool pressButtonsInBank2(std::uint64_t buttons) const noexcept
    {
        if (!cmd || !buttons)
            return false;

        auto* const base = baseMessage();
        if (!base)
            return false;

        std::byte* buttonsPb = nullptr;
        std::memcpy(&buttonsPb, base + cs2::CUserCmd::BaseMessage::kButtonsPbOffset, sizeof(buttonsPb));
        if (!buttonsPb)
            return false;

        // The command's own word first, so the client's local prediction agrees with what is sent.
        auto* const word = reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState2Offset;
        std::uint64_t rawCurrent{};
        std::memcpy(&rawCurrent, word, sizeof(rawCurrent));
        rawCurrent |= buttons;
        std::memcpy(word, &rawCurrent, sizeof(rawCurrent));

        // ...and now the copy the server reads.
        using ButtonsPb = cs2::CUserCmd::BaseMessage::ButtonsPb;
        std::uint64_t pbCurrent{};
        std::memcpy(&pbCurrent, buttonsPb + ButtonsPb::kButtonState2Offset, sizeof(pbCurrent));
        pbCurrent |= buttons;
        std::memcpy(buttonsPb + ButtonsPb::kButtonState2Offset, &pbCurrent, sizeof(pbCurrent));

        std::uint32_t pbHasBits{};
        std::memcpy(&pbHasBits, buttonsPb + ButtonsPb::kHasBitsOffset, sizeof(pbHasBits));
        pbHasBits |= ButtonsPb::kButtonState2HasBit;
        std::memcpy(buttonsPb + ButtonsPb::kHasBitsOffset, &pbHasBits, sizeof(pbHasBits));

        // And mark buttons_pb itself present on the base message - slot 6 sets this every time it
        // touches the sub-message, and a sub-message that is not marked present is not serialized.
        std::uint32_t baseHasBits{};
        std::memcpy(&baseHasBits, base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(baseHasBits));
        baseHasBits |= cs2::CUserCmd::BaseMessage::kButtonsPbHasBit;
        std::memcpy(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, &baseHasBits, sizeof(baseHasBits));

        return true;
    }

    // Presses buttons the way a real click does, measured against a working shot: the attack bit is
    // carried in BOTH buttonstate1 (held) and buttonstate2 (changed this tick), in both the raw
    // command words and buttons_pb. Setting only buttonstate2 (pressed but not held) was why the
    // server saw "attack pressed but not held" and never fired - the whole remaining fake-bullet bug.
    //
    // Returns false if buttons_pb is unreachable, in which case nothing was written and the caller
    // should not pretend a shot happened.
    [[nodiscard]] bool pressButtonsBothBanks(std::uint64_t buttons) const noexcept
    {
        if (!cmd || !buttons)
            return false;

        auto* const base = baseMessage();
        if (!base)
            return false;

        std::byte* buttonsPb = nullptr;
        std::memcpy(&buttonsPb, base + cs2::CUserCmd::BaseMessage::kButtonsPbOffset, sizeof(buttonsPb));

        orInto(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset, buttons);
        orInto(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState2Offset, buttons);

        // CRC serialization copies the native banks into its own button protobuf.
        // The command protobuf may still be absent before that original call.
        if (!buttonsPb)
            return true;

        using ButtonsPb = cs2::CUserCmd::BaseMessage::ButtonsPb;
        orInto(buttonsPb + ButtonsPb::kButtonState1Offset, buttons);
        orHasBit(buttonsPb + ButtonsPb::kHasBitsOffset, ButtonsPb::kButtonState1HasBit);
        orInto(buttonsPb + ButtonsPb::kButtonState2Offset, buttons);
        orHasBit(buttonsPb + ButtonsPb::kHasBitsOffset, ButtonsPb::kButtonState2HasBit);

        orHasBit(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, cs2::CUserCmd::BaseMessage::kButtonsPbHasBit);
        return true;
    }

    // Strips the primary attack from this command everywhere it could fire from: the attack bit is
    // AND-NOT-ed out of both raw button words AND both buttons_pb banks (the copy the server reads -
    // the raw words alone are invisible to the server once slot 6 has copied them, and vice versa),
    // and attack1_start_history_index is reset to its -1 "no attack" default. Has-bits are left
    // alone on purpose: clearing them could unset OTHER buttons' presence, and a bank serializing
    // an explicit zero is exactly "not held". Subtick attack steps are NOT touched here - they
    // carry no button field for other steps, so the caller neutralizes them via
    // SubtickMoves::releaseButton (the two together are the full "hold fire this tick").
    //
    // This is the mirror of pressButtonsBothBanks for taking a shot AWAY from a command the game
    // (or a real click) already put one on: the spread gate holds fire on ticks where no exact
    // spread correction exists instead of letting a cone-luck bullet out.
    void suppressAttack(std::uint64_t buttons) const noexcept
    {
        if (!cmd || !buttons)
            return;

        andNotInto(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset, buttons);
        andNotInto(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState2Offset, buttons);

        const auto base = baseMessage();
        if (!base)
            return;

        std::byte* buttonsPb = nullptr;
        std::memcpy(&buttonsPb, base + cs2::CUserCmd::BaseMessage::kButtonsPbOffset, sizeof(buttonsPb));
        if (!buttonsPb)
            return;

        using ButtonsPb = cs2::CUserCmd::BaseMessage::ButtonsPb;
        andNotInto(buttonsPb + ButtonsPb::kButtonState1Offset, buttons);
        andNotInto(buttonsPb + ButtonsPb::kButtonState2Offset, buttons);

        // The attack marker itself. The value -1 is the field's declared default ("no attack began
        // in this command") and what the game's own Clear() writes, so this reads identically to a
        // command the player never clicked on.
        const int noAttackStarted = -1;
        std::memcpy(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kAttack1StartHistoryIndexOffset,
                    &noAttackStarted, sizeof(noAttackStarted));
    }

    // Reads back the per-command RNG seed the game derived for this command, when it actually set
    // the field. An unset seed is a legitimate state and returns {} rather than 0, so callers can
    // distinguish "engine never wrote one" from "seed zero".
    [[nodiscard]] Optional<int> randomSeed() const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return {};

        std::uint32_t hasBits{};
        std::memcpy(&hasBits, base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(hasBits));
        if ((hasBits & cs2::CUserCmd::BaseMessage::kRandomSeedHasBit) == 0)
            return {};

        int value{};
        std::memcpy(&value, base + cs2::CUserCmd::BaseMessage::kRandomSeedOffset, sizeof(value));
        return value;
    }

    // Stamps a seed into the command together with its has-bit - the write the FVA-style emitter
    // performs to keep wire history chains and server-side RNG rewinds consistent (see
    // Features/Game/FvaEmulator.h). Writing WITHOUT marking the bit would silently serialize as
    // nothing: same rule every other protobuf setter here follows.
    void setRandomSeed(int seed) const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return;

        std::memcpy(base + cs2::CUserCmd::BaseMessage::kRandomSeedOffset, &seed, sizeof(seed));
        orHasBit(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, cs2::CUserCmd::BaseMessage::kRandomSeedHasBit);
    }

    // How many input-history entries this command carries. Empty is a legitimate answer on a
    // command built before any input was sampled, and it means there is no entry for an attack to
    // point at.
    [[nodiscard]] Optional<int> inputHistorySize() const noexcept
    {
        if (!cmd)
            return {};

        int size{};
        std::memcpy(&size, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kInputHistorySizeOffset, sizeof(size));
        if (size <= 0 || size > cs2::CUserCmd::kMaxInputHistoryEntries)
            return {};
        return size;
    }

    // Marks which input-history entry the primary attack began at. Without this the field keeps its
    // -1 default and the server treats the command as containing no attack at all.
    //
    // Refuses to write unless the field currently reads -1. That is both correct - the game setting
    // it means the player is really shooting and we must not overwrite their index - and a cheap
    // guard on the offsets themselves: a wrong offset reading exactly -1 while the neighbouring
    // history size also validates is not a plausible coincidence.
    [[nodiscard]] bool setAttack1StartHistoryIndex(int index) const noexcept
    {
        if (!cmd || index < 0)
            return false;

        auto* const field = reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kAttack1StartHistoryIndexOffset;

        int current{};
        std::memcpy(&current, field, sizeof(current));
        if (current != kNoAttackStarted)
            return false;

        std::memcpy(field, &index, sizeof(index));
        setOuterHasBit(cs2::CUserCmd::kAttack1StartHistoryIndexHasBit);
        return true;
    }

    // The forced counterpart of setAttack1StartHistoryIndex: overwrites the field EVEN when the
    // game already wrote it, updating the has-bit unconditionally. This is the rage silent-aim
    // write: on a manual click the game points the marker at its own newest history entry (whose
    // angles are the crosshair), which would make the server resolve the shot along the CROSSHAIR
    // and never consult our redirected entry - the "shots go where I'm pointing" bug. When we
    // claimed a history entry for the shot, the marker must point at OUR entry, game-written
    // value or not. Only the rage shot writer (which just claimed the entry) may call this.
    bool forceAttack1StartHistoryIndex(int index) const noexcept
    {
        if (!cmd || index < 0)
            return false;

        std::memcpy(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kAttack1StartHistoryIndexOffset,
                    &index, sizeof(index));
        setOuterHasBit(cs2::CUserCmd::kAttack1StartHistoryIndexHasBit);
        return true;
    }

    void setForwardMove(float value) const noexcept
    {
        setFloatField(cs2::CUserCmd::BaseMessage::kForwardMoveOffset, cs2::CUserCmd::BaseMessage::kForwardMoveHasBit, value);
    }

    void setLeftMove(float value) const noexcept
    {
        setFloatField(cs2::CUserCmd::BaseMessage::kLeftMoveOffset, cs2::CUserCmd::BaseMessage::kLeftMoveHasBit, value);
    }

    // Public because the subtick-move helper needs the base message: `subtick_moves` is a field of
    // CBaseUserCmdPB, not of the command wrapper.
    [[nodiscard]] std::byte* baseMessage() const noexcept
    {
        if (!cmd)
            return nullptr;

        std::byte* base = nullptr;
        std::memcpy(&base, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kBaseMessageOffset, sizeof(base));
        return base;
    }

private:
    // OR bits into a 64-bit word / a 32-bit has-bits word in place, without disturbing the rest.
    static void orInto(std::byte* at, std::uint64_t bits) noexcept
    {
        std::uint64_t value{};
        std::memcpy(&value, at, sizeof(value));
        value |= bits;
        std::memcpy(at, &value, sizeof(value));
    }

    static void andNotInto(std::byte* at, std::uint64_t bits) noexcept
    {
        std::uint64_t value{};
        std::memcpy(&value, at, sizeof(value));
        value &= ~bits;
        std::memcpy(at, &value, sizeof(value));
    }

    static void orHasBit(std::byte* at, std::uint32_t bit) noexcept
    {
        std::uint32_t value{};
        std::memcpy(&value, at, sizeof(value));
        value |= bit;
        std::memcpy(at, &value, sizeof(value));
    }

    void setButtonStateInBank(int offset, std::uint64_t buttons, bool pressed) const noexcept
    {
        auto* const word = reinterpret_cast<std::byte*>(cmd) + offset;

        std::uint64_t current{};
        std::memcpy(&current, word, sizeof(current));
        current = pressed ? (current | buttons) : (current & ~buttons);
        std::memcpy(word, &current, sizeof(current));
    }

    [[nodiscard]] std::byte* viewAngles() const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return nullptr;

        std::byte* angles = nullptr;
        std::memcpy(&angles, base + cs2::CUserCmd::BaseMessage::kViewAnglesOffset, sizeof(angles));
        return angles;
    }

    [[nodiscard]] static Optional<float> readFloat(const std::byte* object, int offset) noexcept
    {
        float value{};
        std::memcpy(&value, object + offset, sizeof(value));
        return value;
    }

    // Writing a protobuf field means writing the value AND setting its has-bit; without the bit the
    // field is simply not serialized and the write is silently discarded.
    void setFloatField(int offset, std::uint32_t hasBit, float value) const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return;

        std::memcpy(base + offset, &value, sizeof(value));

        std::uint32_t hasBits{};
        std::memcpy(&hasBits, base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(hasBits));
        hasBits |= hasBit;
        std::memcpy(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, &hasBits, sizeof(hasBits));

        // The OUTER has-bit, marking the base submessage as present on the command itself. The
        // game sets this every single time it touches the base message - `*(cmd + 32) |= 1` - and
        // omitting it was an oversight: a field written into a submessage that is not itself marked
        // present can simply not be serialized, which looks exactly like the write being ignored.
        std::uint32_t outerHasBits{};
        auto* const outer = reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kOuterHasBitsOffset;
        std::memcpy(&outerHasBits, outer, sizeof(outerHasBits));
        outerHasBits |= cs2::CUserCmd::kBaseMessageHasBit;
        std::memcpy(outer, &outerHasBits, sizeof(outerHasBits));
    }

    void setOuterHasBit(std::uint32_t hasBit) const noexcept
    {
        auto* const outer = reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kOuterHasBitsOffset;

        std::uint32_t hasBits{};
        std::memcpy(&hasBits, outer, sizeof(hasBits));
        hasBits |= hasBit;
        std::memcpy(outer, &hasBits, sizeof(hasBits));
    }

    // The declared default of attack1_start_history_index, and the value the game resets it to in
    // CSGOUserCmdPB::Clear(). Means "no attack began in this command".
    static constexpr int kNoAttackStarted = -1;

    cs2::CUserCmd* cmd;
};
