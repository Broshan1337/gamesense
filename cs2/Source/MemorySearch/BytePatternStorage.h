#pragma once

#include <algorithm>
#include <array>

#include "BytePatternConverter.h"
#include "PatternVault.h"

// The stored bytes are XOR ciphertext (PatternVault.h): plaintext pattern bytes must not
// reach the emitted binary. Only the consteval evaluation below ever sees plaintext.
template <std::size_t Capacity>
struct BytePatternStorage {
    explicit(false) consteval BytePatternStorage(const char (&patternString)[Capacity])
    {
        BytePatternConverter converter{patternString};
        const auto [convertedPattern, error] = converter();
        if (error == BytePatternConverterError::NoError) {
            std::copy(convertedPattern.begin(), convertedPattern.end(), pattern.begin());
            size = convertedPattern.size();
        } else {
            errorOccured();
        }
    }

    [[nodiscard]] consteval auto encrypted() const noexcept
    {
        return patternVaultEncrypt(pattern);
    }

    std::array<char, Capacity> pattern{};
    std::size_t size{0};

private:
    void errorOccured();
};
