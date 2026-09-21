#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>

// Steers movement from the INPUT side rather than by editing the finished user command.
//
// Writing here means the game's own code - CCSGOInput slot 6 - reads our values and produces
// everything that follows from them: the movement members, the protobuf fields, and the per-subtick
// analog deltas the command also carries. Editing the finished protobuf reproduced only the first
// of those.
//
// This half alone does NOT move the player. It was measured: with the game building the entire
// command from our injected samples, nothing happened until the matching BUTTON was pressed on the
// command as well (UserCmd::pressButtons, from the slot 7 hook). Analog value and button are both
// required, which is exactly what a real keypress produces.
//
// Both halves are written on purpose:
//  - The QUEUE, because when input samples exist the game overwrites its members from them, so a
//    member-only write would be discarded.
//  - The MEMBERS, because when the queue is empty nothing overwrites them and they are what slot 6
//    copies out, so a queue-only write would do nothing.
// Neither alone covers both cases.
class CSGOInputMovement {
public:
    explicit CSGOInputMovement(cs2::CCSGOInput* input) noexcept
        : input{reinterpret_cast<std::byte*>(input)}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return input != nullptr;
    }

    // A component is only written where the player is not already driving it themselves, so their
    // own keys always win over ours - the same rule the reference implementation applies, and the
    // reason this cannot lock someone in place.
    //
    // The button that corresponds to each component is pressed alongside it, here in CCSGOInput's
    // own accumulator - the client-side "what is held down" that sub_18AAA20 tests. This is NOT
    // what reaches the command (nothing copies this accumulator into it; the command carries its
    // own button words, see UserCmd::pressButtons); it is set because a real keypress sets it too,
    // and client-side prediction reads it.
    void setMove(float forwardMove, float leftMove) const noexcept
    {
        if (!input)
            return;

        const auto buttons = buttonsFor(forwardMove, leftMove);

        writeFloatIfZero(input, cs2::CCSGOInput::kForwardMoveOffset, forwardMove);
        writeFloatIfZero(input, cs2::CCSGOInput::kLeftMoveOffset, leftMove);
        orButtons(input, cs2::CCSGOInput::kButtonStateOffset, buttons);

        forEachQueuedSample([&](std::byte* sample) {
            writeFloatIfZero(sample, cs2::CCSGOInput::InputSampleQueue::kForwardMoveOffset, forwardMove);
            writeFloatIfZero(sample, cs2::CCSGOInput::InputSampleQueue::kLeftMoveOffset, leftMove);
            orButtons(sample, cs2::CCSGOInput::InputSampleQueue::kButtonStateOffset, buttons);
        });
    }

    // The autostrafer's counterpart to setMove: writes BOTH components unconditionally, members
    // and queued samples. Ideal-angle strafing needs the fully rotated pair - forward AND side -
    // and with the player holding W the zero-guard in setMove would leave their forward at 1.0,
    // capping the wish direction at 45 degrees, never reaching the ideal angle. The player's raw
    // keys still reach the command through the button words; what is overridden is the analog
    // value the movement math consumes. Airborne-only callers.
    void forceMove(float forwardMove, float leftMove) const noexcept
    {
        if (!input)
            return;

        const auto buttons = buttonsFor(forwardMove, leftMove);

        writeFloat(input, cs2::CCSGOInput::kForwardMoveOffset, forwardMove);
        writeFloat(input, cs2::CCSGOInput::kLeftMoveOffset, leftMove);
        orButtons(input, cs2::CCSGOInput::kButtonStateOffset, buttons);

        forEachQueuedSample([&](std::byte* sample) {
            writeFloat(sample, cs2::CCSGOInput::InputSampleQueue::kForwardMoveOffset, forwardMove);
            writeFloat(sample, cs2::CCSGOInput::InputSampleQueue::kLeftMoveOffset, leftMove);
            orButtons(sample, cs2::CCSGOInput::InputSampleQueue::kButtonStateOffset, buttons);
        });
    }

private:
    [[nodiscard]] static bool isPlausibleCount(int count) noexcept
    {
        return count > 0 && count <= cs2::CCSGOInput::InputSampleQueue::kMaxPlausibleCount;
    }

    void forEachQueuedSample(auto&& handler) const noexcept
    {
        int count{};
        std::memcpy(&count, input + cs2::CCSGOInput::InputSampleQueue::kCountOffset, sizeof(count));
        if (!isPlausibleCount(count))
            return;

        std::byte* buffer = nullptr;
        std::memcpy(&buffer, input + cs2::CCSGOInput::InputSampleQueue::kBufferOffset, sizeof(buffer));
        if (!buffer)
            return;

        for (int i = 0; i < count; ++i)
            handler(buffer + static_cast<std::ptrdiff_t>(i) * cs2::CCSGOInput::InputSampleQueue::kSampleStride);
    }

    // Which key each component corresponds to. Nothing is pressed for a zero component, so a bot
    // that is only correcting sideways does not also claim to be holding a forward key.
    [[nodiscard]] static std::uint64_t buttonsFor(float forwardMove, float leftMove) noexcept
    {
        std::uint64_t buttons{};

        if (forwardMove > 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kForward;
        else if (forwardMove < 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kBack;

        if (leftMove > 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kMoveLeft;
        else if (leftMove < 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kMoveRight;

        return buttons;
    }

    // OR-ed in, never assigned: this word carries every other key the player is holding, and
    // overwriting it would release them all.
    static void orButtons(std::byte* object, int offset, std::uint64_t buttons) noexcept
    {
        if (!buttons)
            return;

        std::uint64_t current{};
        std::memcpy(&current, object + offset, sizeof(current));
        current |= buttons;
        std::memcpy(object + offset, &current, sizeof(current));
    }

    static void writeFloatIfZero(std::byte* object, int offset, float value) noexcept
    {
        float current{};
        std::memcpy(&current, object + offset, sizeof(current));
        if (current != 0.0f)
            return;

        std::memcpy(object + offset, &value, sizeof(value));
    }

    static void writeFloat(std::byte* object, int offset, float value) noexcept
    {
        std::memcpy(object + offset, &value, sizeof(value));
    }

    std::byte* input;
};
