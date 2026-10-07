#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>



















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

    
    
    
    
    
    
    void forceMove(float forwardMove, float leftMove) const noexcept
    {
        if (!input)
            return;

        const auto buttons = buttonsFor(forwardMove, leftMove);

        writeFloat(input, cs2::CCSGOInput::kForwardMoveOffset, forwardMove);
        writeFloat(input, cs2::CCSGOInput::kLeftMoveOffset, leftMove);
        replaceMoveButtons(input, cs2::CCSGOInput::kButtonStateOffset, buttons);

        forEachQueuedSample([&](std::byte* sample) {
            writeFloat(sample, cs2::CCSGOInput::InputSampleQueue::kForwardMoveOffset, forwardMove);
            writeFloat(sample, cs2::CCSGOInput::InputSampleQueue::kLeftMoveOffset, leftMove);
            replaceMoveButtons(sample, cs2::CCSGOInput::InputSampleQueue::kButtonStateOffset, buttons);
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

    
    
    static void orButtons(std::byte* object, int offset, std::uint64_t buttons) noexcept
    {
        if (!buttons)
            return;

        std::uint64_t current{};
        std::memcpy(&current, object + offset, sizeof(current));
        current |= buttons;
        std::memcpy(object + offset, &current, sizeof(current));
    }

    static void replaceMoveButtons(std::byte* object, int offset, std::uint64_t buttons) noexcept
    {
        using Buttons = cs2::CCSGOInput::Buttons;
        constexpr auto moveMask = Buttons::kForward | Buttons::kBack | Buttons::kMoveLeft | Buttons::kMoveRight;
        std::uint64_t current{};
        std::memcpy(&current, object + offset, sizeof(current));
        current = (current & ~moveMask) | buttons;
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
