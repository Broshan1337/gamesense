#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <Utils/Optional.h>






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

    
    
    
    [[nodiscard]] Optional<float> viewYaw() const noexcept
    {
        const auto angles = viewAngles();
        if (!angles)
            return {};
        return readFloat(angles, cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset);
    }

    
    [[nodiscard]] Optional<float> viewPitch() const noexcept
    {
        const auto angles = viewAngles();
        if (!angles)
            return {};
        return readFloat(angles, cs2::CUserCmd::BaseMessage::ViewAngles::kPitchOffset);
    }

    
    
    
    
    
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

    
    
    [[nodiscard]] Optional<float> forwardMove() const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return {};
        return readFloat(base, cs2::CUserCmd::BaseMessage::kForwardMoveOffset);
    }

    
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

    
    
    
    [[nodiscard]] std::uint64_t buttonState1() const noexcept
    {
        if (!cmd)
            return 0;

        std::uint64_t current{};
        std::memcpy(&current, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset, sizeof(current));
        return current;
    }

    
    
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

    
    
    
    
    
    
    
    
    
    void setButtonState(std::uint64_t buttons, bool pressed) const noexcept
    {
        if (!cmd || !buttons)
            return;

        setButtonStateInBank(cs2::CUserCmd::kButtonState1Offset, buttons, pressed);
        setButtonStateInBank(cs2::CUserCmd::kButtonState2Offset, buttons, pressed);
    }

    
    
    
    
    
    
    
    
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

        
        auto* const word = reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState2Offset;
        std::uint64_t rawCurrent{};
        std::memcpy(&rawCurrent, word, sizeof(rawCurrent));
        rawCurrent |= buttons;
        std::memcpy(word, &rawCurrent, sizeof(rawCurrent));

        
        using ButtonsPb = cs2::CUserCmd::BaseMessage::ButtonsPb;
        std::uint64_t pbCurrent{};
        std::memcpy(&pbCurrent, buttonsPb + ButtonsPb::kButtonState2Offset, sizeof(pbCurrent));
        pbCurrent |= buttons;
        std::memcpy(buttonsPb + ButtonsPb::kButtonState2Offset, &pbCurrent, sizeof(pbCurrent));

        std::uint32_t pbHasBits{};
        std::memcpy(&pbHasBits, buttonsPb + ButtonsPb::kHasBitsOffset, sizeof(pbHasBits));
        pbHasBits |= ButtonsPb::kButtonState2HasBit;
        std::memcpy(buttonsPb + ButtonsPb::kHasBitsOffset, &pbHasBits, sizeof(pbHasBits));

        
        
        std::uint32_t baseHasBits{};
        std::memcpy(&baseHasBits, base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(baseHasBits));
        baseHasBits |= cs2::CUserCmd::BaseMessage::kButtonsPbHasBit;
        std::memcpy(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, &baseHasBits, sizeof(baseHasBits));

        return true;
    }

    
    
    
    
    
    
    
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

    
    
    
    
    
    
    
    
    
    
    
    
    // Replace movement intent in native and serialized banks, preserving all
    // unrelated buttons. Used by counter-strafing rather than OR-ing opposites.
    void replaceButtons(std::uint64_t mask, std::uint64_t buttons) const noexcept
    {
        if (!cmd || !mask) return;
        const auto replace = [&](std::byte* at) {
            std::uint64_t value{};
            std::memcpy(&value, at, sizeof(value));
            value = (value & ~mask) | (buttons & mask);
            std::memcpy(at, &value, sizeof(value));
        };
        replace(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState1Offset);
        replace(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState2Offset);
        andNotInto(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kButtonState3Offset, mask);
        auto* base = baseMessage();
        if (!base) return;
        std::byte* pb{};
        std::memcpy(&pb, base + cs2::CUserCmd::BaseMessage::kButtonsPbOffset, sizeof(pb));
        if (pb) {
            using B = cs2::CUserCmd::BaseMessage::ButtonsPb;
            replace(pb + B::kButtonState1Offset);
            replace(pb + B::kButtonState2Offset);
            andNotInto(pb + B::kButtonState3Offset, mask);
            orHasBit(pb + B::kHasBitsOffset, B::kButtonState1HasBit | B::kButtonState2HasBit | B::kButtonState3HasBit);
            orHasBit(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, cs2::CUserCmd::BaseMessage::kButtonsPbHasBit);
        }
        setOuterHasBit(cs2::CUserCmd::kBaseMessageHasBit);
    }

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

        
        
        
        const int noAttackStarted = -1;
        std::memcpy(reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::kAttack1StartHistoryIndexOffset,
                    &noAttackStarted, sizeof(noAttackStarted));
    }

    
    
    
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

    
    
    
    
    void setRandomSeed(int seed) const noexcept
    {
        const auto base = baseMessage();
        if (!base)
            return;

        std::memcpy(base + cs2::CUserCmd::BaseMessage::kRandomSeedOffset, &seed, sizeof(seed));
        orHasBit(base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, cs2::CUserCmd::BaseMessage::kRandomSeedHasBit);
    }

    
    
    
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

    
    
    [[nodiscard]] std::byte* baseMessage() const noexcept
    {
        if (!cmd)
            return nullptr;

        std::byte* base = nullptr;
        std::memcpy(&base, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kBaseMessageOffset, sizeof(base));
        return base;
    }

private:
    
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

    
    
    static constexpr int kNoAttackStarted = -1;

    cs2::CUserCmd* cmd;
};
