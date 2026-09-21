#pragma once

#include <array>

#include "BytePatternStorage.h"
#include "BytePatternView.h"
#include "PatternVault.h"
#include <Utils/ObfAnnotations.h>

// The static array below is the emitted-at-rest storage: it holds XOR ciphertext and is
// decrypted in place on first use (PatternVault.h). The returned view points at the
// persistent decrypted array, so storing the view (tests do) keeps working.
template <BytePatternStorage Storage>
__attribute__((annotate("+fla"))) auto operator ""_pat()
{
    static constexpr auto kEncrypted = [] consteval {
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
