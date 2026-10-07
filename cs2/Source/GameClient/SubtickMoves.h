#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>












template <typename HookContext>
class SubtickMoves {
public:
    explicit SubtickMoves(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
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

        
        
        const std::uint32_t noFields{};
        std::memcpy(result + cs2::CSubtickMoveStep::kHasBitsOffset, &noFields, sizeof(noFields));

        setFloat(result, cs2::CSubtickMoveStep::kWhenOffset, cs2::CSubtickMoveStep::kWhenHasBit, when);
        return result;
    }

    
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

    
    
    
    static void setAnalogDeltas(std::byte* step, float forward, float left) noexcept
    {
        if (!step)
            return;

        setFloat(step, cs2::CSubtickMoveStep::kAnalogForwardDeltaOffset, cs2::CSubtickMoveStep::kAnalogForwardDeltaHasBit, forward);
        setFloat(step, cs2::CSubtickMoveStep::kAnalogLeftDeltaOffset, cs2::CSubtickMoveStep::kAnalogLeftDeltaHasBit, left);
    }

    
    
    
    
    
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

    
    
    
    
    
    static void clear(std::byte* baseMessage) noexcept
    {
        if (!baseMessage)
            return;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        const int empty{};
        std::memcpy(field + Field::kCurrentSizeOffset, &empty, sizeof(empty));
    }

    
    
    
    
    
    
    // Remove only fields owned by horizontal movement. Keep the sample's time,
    // view deltas, jump, duck and attack events intact.
    static void stripMovement(std::byte* baseMessage, std::uint64_t movementMask) noexcept
    {
        stripAnalog(baseMessage);
        if (!baseMessage || !movementMask)
            return;
        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* field = baseMessage + Field::kFieldOffset;
        int size{};
        std::byte* rep{};
        std::memcpy(&size, field + Field::kCurrentSizeOffset, sizeof(size));
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep || size <= 0 || size > Field::kMaxSteps)
            return;
        for (int i = 0; i < size; ++i) {
            std::byte* step{};
            std::memcpy(&step, rep + Field::kRepElementsOffset + i * sizeof(step), sizeof(step));
            if (!step)
                continue;
            std::uint32_t bits{};
            std::uint64_t button{};
            std::memcpy(&bits, step + cs2::CSubtickMoveStep::kHasBitsOffset, sizeof(bits));
            if (!(bits & cs2::CSubtickMoveStep::kButtonHasBit))
                continue;
            std::memcpy(&button, step + cs2::CSubtickMoveStep::kButtonOffset, sizeof(button));
            if (!(button & movementMask))
                continue;
            button &= ~movementMask;
            std::memcpy(step + cs2::CSubtickMoveStep::kButtonOffset, &button, sizeof(button));
            if (!button) {
                bits &= ~(cs2::CSubtickMoveStep::kButtonHasBit | cs2::CSubtickMoveStep::kPressedHasBit);
                std::memcpy(step + cs2::CSubtickMoveStep::kHasBitsOffset, &bits, sizeof(bits));
            }
        }
    }

    static void stripAnalog(std::byte* baseMessage) noexcept
    {
        if (!baseMessage)
            return;

        using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
        auto* const field = baseMessage + Field::kFieldOffset;

        int currentSize{};
        std::memcpy(&currentSize, field + Field::kCurrentSizeOffset, sizeof(currentSize));
        std::byte* rep = nullptr;
        std::memcpy(&rep, field + Field::kRepOffset, sizeof(rep));
        if (!rep || currentSize <= 0)
            return;

        constexpr std::uint32_t kAnalogBits = cs2::CSubtickMoveStep::kAnalogForwardDeltaHasBit
            | cs2::CSubtickMoveStep::kAnalogLeftDeltaHasBit;
        const int size = currentSize > Field::kMaxSteps ? Field::kMaxSteps : currentSize;
        for (int i = 0; i < size; ++i) {
            std::byte* step = nullptr;
            std::memcpy(&step, rep + Field::kRepElementsOffset + static_cast<std::ptrdiff_t>(i) * sizeof(step), sizeof(step));
            if (!step)
                continue;

            std::uint32_t hasBits{};
            std::memcpy(&hasBits, step + cs2::CSubtickMoveStep::kHasBitsOffset, sizeof(hasBits));
            if ((hasBits & kAnalogBits) == 0)
                continue;
            hasBits &= ~kAnalogBits;
            std::memcpy(step + cs2::CSubtickMoveStep::kHasBitsOffset, &hasBits, sizeof(hasBits));
        }
    }

    
    
    
    
    
    
    
    
    
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
    
    
    [[nodiscard]] static float stepWhen(const std::byte* step) noexcept
    {
        float when = 0.0f;
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, step + cs2::CSubtickMoveStep::kHasBitsOffset, sizeof(hasBits));
        if (hasBits & cs2::CSubtickMoveStep::kWhenHasBit)
            std::memcpy(&when, step + cs2::CSubtickMoveStep::kWhenOffset, sizeof(when));
        return when;
    }

    
    
    
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
