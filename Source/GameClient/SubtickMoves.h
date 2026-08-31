#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Appends real CSubtickMoveStep entries to a command's `subtick_moves`.
//
// CS2 does not act on a button from the button words alone in every case - it also carries, per
// tick, the exact sub-tick moments a button went down and came up. A bunnyhop that only flips the
// button word gives the server a key that was "held the whole tick", which is not the same thing as
// a tap and does not reliably produce a jump on the landing tick.
//
// Nothing here reimplements protobuf. The two steps the game itself takes to grow this field -
// arena-allocate a zeroed step, then RepeatedPtrFieldBase::AddAllocated - are called through the
// game's own functions, so an appended step is byte-identical to one the input system produced.
// The fast path (reusing an already-allocated spare element) mirrors slot 6's inline version.
template <typename HookContext>
class SubtickMoves {
public:
    explicit SubtickMoves(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Appends a step at `when` (0..1 through the tick) and returns it, or nullptr if the field is
    // full or the helpers could not be resolved. The returned step has clean has-bits: only `when`
    // is set, so the caller decides what the step actually carries.
    [[nodiscard]] std::byte* add(std::byte* baseMessage, float when) const noexcept
    {
        if (!baseMessage)
            return nullptr;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        int currentSize{};
        std::memcpy(&currentSize, field + Field::kCurrentSizeOffset, sizeof(currentSize));
        if (currentSize < 0 || currentSize >= Field::kMaxSteps)
            return nullptr;

        auto* const step = reuseSpareStep(field, currentSize);
        auto* const result = step ? step : allocateStep(field);
        if (!result)
            return nullptr;

        // A reused element still holds the previous tick's fields, so the has-bits are cleared to
        // give the caller the same blank step a fresh allocation would.
        const std::uint32_t noFields{};
        std::memcpy(result + cs2::CSubtickMoveStep::kHasBitsOffset, &noFields, sizeof(noFields));

        setFloat(result, cs2::CSubtickMoveStep::kWhenOffset, cs2::CSubtickMoveStep::kWhenHasBit, when);
        return result;
    }

    // A button transition. `pressed` false is a release, which is the half that makes a tap a tap.
    static void setButton(std::byte* step, std::uint64_t button, bool pressed) noexcept
    {
        if (!step)
            return;

        std::memcpy(step + cs2::CSubtickMoveStep::kButtonOffset, &button, sizeof(button));
        setHasBit(step, cs2::CSubtickMoveStep::kButtonHasBit);

        const std::uint8_t pressedByte = pressed ? 1 : 0;
        std::memcpy(step + cs2::CSubtickMoveStep::kPressedOffset, &pressedByte, sizeof(pressedByte));
        setHasBit(step, cs2::CSubtickMoveStep::kPressedHasBit);
    }

    // A pure view-angle adjustment at `when` through the tick. This is the mechanism the quantized
    // strafer steers with: the server rotates the view by `yaw_delta` at the step's moment, no
    // button or analog component involved.
    static void setYawDelta(std::byte* step, float delta) noexcept
    {
        if (!step)
            return;

        setFloat(step, cs2::CSubtickMoveStep::kYawDeltaOffset, cs2::CSubtickMoveStep::kYawDeltaHasBit, delta);
    }

    static void setPitchDelta(std::byte* step, float delta) noexcept
    {
        if (!step)
            return;

        setFloat(step, cs2::CSubtickMoveStep::kPitchDeltaOffset, cs2::CSubtickMoveStep::kPitchDeltaHasBit, delta);
    }

    // Explicit zero analog components. An unset protobuf float reads back as 0, so on the wire this
    // is the same as leaving the fields out - but the reference WRITES them, and a step that carries
    // the same has-bits as one the game's own input system produced is the safest thing to send.
    static void setAnalogDeltas(std::byte* step, float forward, float left) noexcept
    {
        if (!step)
            return;

        setFloat(step, cs2::CSubtickMoveStep::kAnalogForwardDeltaOffset, cs2::CSubtickMoveStep::kAnalogForwardDeltaHasBit, forward);
        setFloat(step, cs2::CSubtickMoveStep::kAnalogLeftDeltaOffset, cs2::CSubtickMoveStep::kAnalogLeftDeltaHasBit, left);
    }

    // The largest `when` already scheduled in the command's subtick_moves (0.0 when empty).
    //
    // Later writers need this to place their own steps AFTER whatever exists instead of stacking
    // onto the same instants: the game processes the steps in order, and two features both assuming
    // "the whole tick is free" would interleave unpredictably.
    [[nodiscard]] static float maxWhen(std::byte* baseMessage) noexcept
    {
        if (!baseMessage)
            return 0.0f;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        int currentSize{};
        std::memcpy(&currentSize, field + Field::kCurrentSizeOffset, sizeof(currentSize));
        if (currentSize <= 0)
            return 0.0f;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep)
            return 0.0f;

        float largest{0.0f};
        for (int i = 0; i < currentSize; ++i) {
            std::byte* step = nullptr;
            std::memcpy(&step, rep + Field::kRepElementsOffset + static_cast<std::ptrdiff_t>(i) * sizeof(step), sizeof(step));
            if (!step)
                continue;

            float when{};
            std::memcpy(&when, step + cs2::CSubtickMoveStep::kWhenOffset, sizeof(when));
            if (when > largest)
                largest = when;
        }
        return largest;
    }

    // How many subtick steps the command already carries. The 32-slot field is SHARED with the
    // game's own quantized mouse input (one step per mouse event on quantized-input servers), so
    // this is what tells a "couldn't add steps" tick apart: a full field is contention, a small
    // count is a broken append.
    [[nodiscard]] static int count(std::byte* baseMessage) noexcept
    {
        if (!baseMessage)
            return 0;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        int currentSize{};
        std::memcpy(&currentSize, field + Field::kCurrentSizeOffset, sizeof(currentSize));
        return currentSize > 0 ? currentSize : 0;
    }

    // Live field state for diagnostics: current size, allocated capacity, and whether the rep
    // (element array) exists at all. Reading allocated requires the rep pointer, so a null rep
    // reports allocated = -1.
    struct FieldStats {
        int current{};
        int allocated{-1};
        bool repNull{true};
    };

    [[nodiscard]] static FieldStats fieldStats(std::byte* baseMessage) noexcept
    {
        FieldStats stats;
        if (!baseMessage)
            return stats;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        std::memcpy(&stats.current, field + Field::kCurrentSizeOffset, sizeof(stats.current));

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep)
            return stats;

        stats.repNull = false;
        std::memcpy(&stats.allocated, rep + Field::kRepAllocatedSizeOffset, sizeof(stats.allocated));
        return stats;
    }

    // Turns every PRESSED step for `button` in the command's subtick_moves into a RELEASE at the
    // same instant. The server fires on the press TRANSITION, so a press rewritten as a release
    // can never fire - but the timeline keeps its shape (count, `when` order, all other steps),
    // which matters because the field is shared with quantized mouse input and the strafer's yaw
    // deltas: dropping steps entirely would shift everyone else's timing.
    //
    // This is the subtick half of "hold fire this tick" (UserCmd::suppressAttack is the bank half):
    // a real click reaches the server through BOTH the button banks and the attack press step slot 6
    // emitted for it, so both must be neutralized before a gated shot can be called suppressed.
    static void releaseButton(std::byte* baseMessage, std::uint64_t button) noexcept
    {
        if (!baseMessage || !button)
            return;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        int currentSize{};
        std::memcpy(&currentSize, field + Field::kCurrentSizeOffset, sizeof(currentSize));
        std::byte* rep = nullptr;
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep || currentSize <= 0)
            return;

        const int size = currentSize > Field::kMaxSteps ? Field::kMaxSteps : currentSize;
        for (int i = 0; i < size; ++i) {
            std::byte* step = nullptr;
            std::memcpy(&step, rep + Field::kRepElementsOffset + static_cast<std::ptrdiff_t>(i) * sizeof(step), sizeof(step));
            if (!step)
                continue;

            std::uint64_t stepButton{};
            std::memcpy(&stepButton, step + cs2::CSubtickMoveStep::kButtonOffset, sizeof(stepButton));
            std::uint32_t hasBits{};
            std::memcpy(&hasBits, step + cs2::CSubtickMoveStep::kHasBitsOffset, sizeof(hasBits));
            if ((hasBits & cs2::CSubtickMoveStep::kButtonHasBit) == 0 || stepButton != button)
                continue;

            const std::uint8_t released = 0;
            std::memcpy(step + cs2::CSubtickMoveStep::kPressedOffset, &released, sizeof(released));
            setHasBit(step, cs2::CSubtickMoveStep::kPressedHasBit);
        }
    }

    // velocity-cs2's desubtick: empties the field the way protobuf's Clear() does - size to zero,
    // allocated elements left behind as spares that add()'s fast path picks up. The 32 slots are
    // shared with the game's own quantized mouse input, so every slot a mouse event occupies is
    // one a feature's step cannot use; clearing BEFORE the features run (the reference does it at
    // the top of every CreateMove) is what keeps the timeline theirs instead of the mouse's.
    static void clear(std::byte* baseMessage) noexcept
    {
        if (!baseMessage)
            return;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        const int empty{};
        std::memcpy(field + Field::kCurrentSizeOffset, &empty, sizeof(empty));
    }

    // Stable-sorts the command's subtick steps by their `when` timestamp.
    //
    // Appending is not enough when other writers already populated the timeline: quantized mouse
    // input emits one step per mouse event and the strafer spreads yaw deltas across the tick, so a
    // press@0.0 appended after a step@0.5 breaks the timeline's monotonic order - and the engine
    // processes steps SEQUENTIALLY, trusting array order to match time order. An out-of-order attack
    // transition is exactly how you get client-predicted shots the server never takes (fake bullets:
    // sound/flash/ammo locally, nothing server-side). Stable, so steps sharing a `when` keep the
    // engine's relative order (the bunnyhop's release-before-press pair depends on that).
    static void sortByWhen(std::byte* baseMessage) noexcept
    {
        if (!baseMessage)
            return;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        int currentSize{};
        std::memcpy(&currentSize, field + Field::kCurrentSizeOffset, sizeof(currentSize));
        std::byte* rep = nullptr;
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep || currentSize < 2 || currentSize > Field::kMaxSteps)
            return;

        std::byte* steps[Field::kMaxSteps];
        std::memcpy(steps, rep + Field::kRepElementsOffset, sizeof(std::byte*) * currentSize);

        // Insertion sort: stable, and the field holds at most 32 entries.
        for (int i = 1; i < currentSize; ++i) {
            std::byte* key = steps[i];
            const float keyWhen = stepWhen(key);
            int j = i - 1;
            while (j >= 0 && stepWhen(steps[j]) > keyWhen) {
                steps[j + 1] = steps[j];
                --j;
            }
            steps[j + 1] = key;
        }

        std::memcpy(rep + Field::kRepElementsOffset, steps, sizeof(std::byte*) * currentSize);
    }

private:
    // The step's scheduled moment through the tick; a step with no `when` counts as 0.0 (start),
    // which is also what a fresh arena-zeroed step reads as.
    [[nodiscard]] static float stepWhen(const std::byte* step) noexcept
    {
        float when = 0.0f;
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, step + cs2::CSubtickMoveStep::kHasBitsOffset, sizeof(hasBits));
        if (hasBits & cs2::CSubtickMoveStep::kWhenHasBit)
            std::memcpy(&when, step + cs2::CSubtickMoveStep::kWhenOffset, sizeof(when));
        return when;
    }

    // The game's own fast path: if the repeated field still holds allocated-but-unused elements,
    // take the next one instead of allocating. Skipping this and always allocating would leak those
    // spares and desynchronise the field's allocated count from its size.
    [[nodiscard]] static std::byte* reuseSpareStep(std::byte* field, int currentSize) noexcept
    {
        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep)
            return nullptr;

        int allocatedSize{};
        std::memcpy(&allocatedSize, rep + Field::kRepAllocatedSizeOffset, sizeof(allocatedSize));
        if (currentSize >= allocatedSize)
            return nullptr;

        std::byte* step = nullptr;
        std::memcpy(&step, rep + Field::kRepElementsOffset + static_cast<std::ptrdiff_t>(currentSize) * sizeof(step), sizeof(step));
        if (!step)
            return nullptr;

        const int newSize = currentSize + 1;
        std::memcpy(field + Field::kCurrentSizeOffset, &newSize, sizeof(newSize));
        return step;
    }

    [[nodiscard]] std::byte* allocateStep(std::byte* field) const noexcept
    {
        const auto createStep = hookContext.patternSearchResults().template get<CreateSubtickMoveStep>();
        const auto addAllocated = hookContext.patternSearchResults().template get<RepeatedPtrFieldAddAllocated>();
        if (!createStep || !addAllocated)
            return nullptr;

        // The arena the command is being built in. Passing it on is what keeps the step's lifetime
        // tied to the command instead of leaking one allocation per tick.
        void* arena = nullptr;
        std::memcpy(&arena, field + cs2::CUserCmd::BaseMessage::SubtickMoves::kArenaOffset, sizeof(arena));

        auto* const step = static_cast<std::byte*>(createStep(arena));
        if (!step)
            return nullptr;

        addAllocated(field, step);
        return step;
    }

    static void setHasBit(std::byte* step, std::uint32_t hasBit) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, step + cs2::CSubtickMoveStep::kHasBitsOffset, sizeof(hasBits));
        hasBits |= hasBit;
        std::memcpy(step + cs2::CSubtickMoveStep::kHasBitsOffset, &hasBits, sizeof(hasBits));
    }

    static void setFloat(std::byte* step, int offset, std::uint32_t hasBit, float value) noexcept
    {
        std::memcpy(step + offset, &value, sizeof(value));
        setHasBit(step, hasBit);
    }

    HookContext& hookContext;
};
