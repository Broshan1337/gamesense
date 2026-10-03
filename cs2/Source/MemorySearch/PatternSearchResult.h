#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <Utils/GenericPointer.h>

class PatternSearchResult {
public:
    PatternSearchResult(GenericPointer base, std::size_t patternFoundOffset, std::span<const std::byte> foundPatternBytes) noexcept
        : base{base}
        , patternFoundOffset{patternFoundOffset}
        , foundPatternBytes{foundPatternBytes}
    {
        assert(base);
    }

    PatternSearchResult() = default;

    PatternSearchResult& add(std::size_t offset) noexcept
    {
        if (base) {
            extraOffset += offset;
            assert(extraOffset < foundPatternBytes.size());
        }
        return *this;
    }

    [[nodiscard]] GenericPointer abs(std::size_t offsetToNextInstruction = 4) const noexcept
    {
        if (base) {
            using OffsetType = std::int32_t;
            OffsetType offset;
            assert(offsetToNextInstruction >= sizeof(OffsetType));
            assert(foundPatternBytes.size() - extraOffset >= sizeof(OffsetType));
            std::memcpy(&offset, foundPatternBytes.data() + extraOffset, sizeof(OffsetType));
            return base.as<const std::byte*>() + patternFoundOffset + extraOffset + offsetToNextInstruction + offset;
        }
        return {};
    }

    template <typename FieldOffsetType>
    [[nodiscard]] FieldOffsetType readOffset() const noexcept
    {
        if (base) {
            typename FieldOffsetType::OffsetType result;
            assert(foundPatternBytes.size() - extraOffset >= sizeof(result));
            std::memcpy(&result, foundPatternBytes.data() + extraOffset, sizeof(result));
            return FieldOffsetType{result};
        }
        return {};
    }

    template <typename T>
    [[nodiscard]] T as() const noexcept
    {
        if (base)
            return T(base.as<const std::byte*>() + patternFoundOffset + extraOffset);
        return T(base.as<void*>());
    }

    [[nodiscard]] std::array<std::byte, 8> read() const noexcept
    {
        std::array<std::byte, 8> result{};
        if (base)
            std::memcpy(result.data(), foundPatternBytes.data() + extraOffset, (std::min)(foundPatternBytes.size() - extraOffset, std::size_t{8}));
        return result;
    }

    // Reads the single byte at the offset, sign-extended to 32 bits (see Read8 in
    // CodePatternOperation.h for why this exists).
    [[nodiscard]] std::array<std::byte, 8> read8() const noexcept
    {
        std::array<std::byte, 8> result{};
        if (base && foundPatternBytes.size() > extraOffset) {
            const auto value = static_cast<std::int8_t>(foundPatternBytes[extraOffset]);
            const auto extended = static_cast<std::int32_t>(value);
            std::memcpy(result.data(), &extended, sizeof(extended));
        }
        return result;
    }

    [[nodiscard]] std::array<std::byte, 8> get() const noexcept
    {
        std::array<std::byte, 8> result{};
        if (base) {
            const auto pointer = base.as<const std::byte*>() + patternFoundOffset + extraOffset;
            static_assert(sizeof(pointer) == sizeof(result));
            std::memcpy(result.data(), &pointer, 8);
        }    
        return result;
    }
    
    [[nodiscard]] std::array<std::byte, 8> abs2(std::size_t offsetToNextInstruction = 4) const noexcept
    {
        std::array<std::byte, 8> result{};
        if (base) {
            using OffsetType = std::int32_t;
            OffsetType offset;
            assert(offsetToNextInstruction >= sizeof(OffsetType));
            assert(foundPatternBytes.size() - extraOffset >= sizeof(OffsetType));
            std::memcpy(&offset, foundPatternBytes.data() + extraOffset, sizeof(OffsetType));
            const auto pointer = base.as<const std::byte*>() + patternFoundOffset + extraOffset + offsetToNextInstruction + offset;
            static_assert(sizeof(pointer) == sizeof(result));
            std::memcpy(result.data(), &pointer, 8);
        }
        return result;
    }

private:
    GenericPointer base{};
    std::size_t patternFoundOffset;
    std::span<const std::byte> foundPatternBytes;
    std::size_t extraOffset{0};
};
