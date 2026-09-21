#pragma once

#include <cstddef>

// Pattern vault: the module's signature byte arrays are its most valuable static secret -
// they are the first thing a reverser copies from a disk image, because they are the entire
// knowledge base of where the game's internals live.
//
// Design: the byte arrays are XOR-encrypted at compile time (consteval) and decrypted IN
// PLACE on first use, before any scan runs. The disk image and every static copy of the
// module therefore carry only ciphertext; plaintext exists exclusively in live process
// memory after the first scan. NOT cryptography - a live process dump still reveals them;
// this closes the static-analysis / "copy their sigs" surface.
//
// The flag is intentionally a plain bool (not std::atomic): decryption happens on the
// module-init thread before any consumer exists, and the decrypt is idempotent (same bytes
// rewritten on every run). Do not lift this into a multi-thread-first-use context without
// atomics.
constexpr char patternVaultKey(std::size_t index) noexcept
{
    const unsigned k = static_cast<unsigned>(index * 0x9E3779B9u) + 0x85EBCA6Bu;
    return static_cast<char>((k >> 17) & 0xFF);
}

template <std::size_t N>
[[nodiscard]] consteval auto patternVaultEncrypt(const std::array<char, N>& plaintext) noexcept
{
    std::array<char, N> encrypted{};
    for (std::size_t i = 0; i < N; ++i)
        encrypted[i] = static_cast<char>(plaintext[i] ^ patternVaultKey(i));
    return encrypted;
}
