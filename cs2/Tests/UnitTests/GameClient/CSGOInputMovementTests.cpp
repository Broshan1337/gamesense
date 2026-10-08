#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include <GameClient/CSGOInputMovement.h>

namespace {

template <typename T>
T read(const std::byte* data, int offset)
{
    T value{};
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

template <typename T>
void write(std::byte* data, int offset, T value)
{
    std::memcpy(data + offset, &value, sizeof(value));
}

using Input = cs2::CCSGOInput;
using Buttons = Input::Buttons;
using Queue = Input::InputSampleQueue;

}

TEST(CSGOInputMovementTest, ForcedMovementReplacesOpposingButtonsInEverySample)
{
    alignas(8) std::array<std::byte, 3000> input{};
    alignas(8) std::array<std::byte, Queue::kSampleStride * 2> samples{};
    constexpr auto held = Buttons::kAttack | Buttons::kJump | Buttons::kBack | Buttons::kMoveRight;
    write(input.data(), Input::kButtonStateOffset, held);
    write(input.data(), Queue::kCountOffset, 2);
    write(input.data(), Queue::kBufferOffset, samples.data());
    for (int i = 0; i < 2; ++i)
        write(samples.data() + i * Queue::kSampleStride, Queue::kButtonStateOffset, held);

    const CSGOInputMovement movement{reinterpret_cast<Input*>(input.data())};
    movement.forceMove(0.6f, 0.8f);
    constexpr auto expected = Buttons::kAttack | Buttons::kJump | Buttons::kForward | Buttons::kMoveLeft;
    EXPECT_EQ(read<std::uint64_t>(input.data(), Input::kButtonStateOffset), expected);
    EXPECT_FLOAT_EQ(read<float>(input.data(), Input::kForwardMoveOffset), 0.6f);
    EXPECT_FLOAT_EQ(read<float>(input.data(), Input::kLeftMoveOffset), 0.8f);
    for (int i = 0; i < 2; ++i) {
        const auto* sample = samples.data() + i * Queue::kSampleStride;
        EXPECT_EQ(read<std::uint64_t>(sample, Queue::kButtonStateOffset), expected);
        EXPECT_FLOAT_EQ(read<float>(sample, Queue::kForwardMoveOffset), 0.6f);
        EXPECT_FLOAT_EQ(read<float>(sample, Queue::kLeftMoveOffset), 0.8f);
    }

    movement.forceMove(0.0f, 0.0f);
    EXPECT_EQ(read<std::uint64_t>(input.data(), Input::kButtonStateOffset), Buttons::kAttack | Buttons::kJump);
    for (int i = 0; i < 2; ++i)
        EXPECT_EQ(read<std::uint64_t>(samples.data() + i * Queue::kSampleStride, Queue::kButtonStateOffset), Buttons::kAttack | Buttons::kJump);
}

TEST(CSGOInputMovementTest, InvalidQueueCountsDoNotAccessTheBuffer)
{
    alignas(8) std::array<std::byte, 3000> input{};
    const CSGOInputMovement movement{reinterpret_cast<Input*>(input.data())};
    for (int count : {-1, 0, Queue::kMaxPlausibleCount + 1}) {
        write(input.data(), Queue::kCountOffset, count);
        movement.forceMove(-1.0f, 0.0f);
        EXPECT_FLOAT_EQ(read<float>(input.data(), Input::kForwardMoveOffset), -1.0f);
    }
}
