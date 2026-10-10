#pragma once

#include <array>

#include "BytePatternStorage.h"
#include "BytePatternView.h"
#include "PatternVault.h"
#include <Utils/ObfAnnotations.h>




template <BytePatternStorage Storage>
NS_OBF_FLATTEN auto operator ""_pat()
{
    static constexpr auto kEncrypted = []() consteval {
        std::array<char, Storage.size> encrypted{};
        for (std::size_t i = 0; i < Storage.size; ++i)
            encrypted[i] = static_cast<char>(Storage.pattern[i] ^ patternVaultKey(i));
        return encrypted;
    }();
    static std::array<char, Storage.size> pattern{kEncrypted};
    static bool decrypted = false;
    if (!decrypted) {
        for (std::size_t i = 0; i < Storage.size; ++i)
            pattern[i] = static_cast<char>(pattern[i] ^ patternVaultKey(i));
        decrypted = true;
    }

    return BytePatternView<Storage.size>{pattern};
}
