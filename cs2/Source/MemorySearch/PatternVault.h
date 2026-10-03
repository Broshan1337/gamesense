#pragma once

#include <array>
#include <atomic>
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

// ---- RUNTIME SESSION LOCK (hardening 2026-09-24) -----------------------------------
//
// The consteval key above is static: it is recoverable from the module binary alone. The
// idle state of a sealed pool therefore gets a SECOND XOR layer derived at RUNTIME from the
// loader-stamped session trailer (see session_bind::verifySession -> armRuntimeLock). While
// the vault is idle it is sealed under the session key; a scan window unseals it, and the
// seal() wipe re-locks it afterwards (wipe-after-scan - the TOCTOU window is one scan pass
// at init, not the whole session).
//
// Fail-closed: with NS_PATTERN_VAULT_REQUIRES_SESSION defined (the shipped module target),
// an un-armed vault never unseals - the pattern bytes stay ciphertext and every scan finds
// nothing (results zeroed, features degrade). A dumped-and-re-injected module without a
// genuine loader-stamped trailer cannot decrypt its own patterns.
//
// Offline tools (scratchpad pattern_scan) and the unit tests build WITHOUT the define: the
// runtime key stays all-zero, the extra XOR is a no-op, and the vault behaves exactly like
// the pre-hardening version.
namespace pattern_vault
{

#if defined(NS_PATTERN_VAULT_REQUIRES_SESSION)
inline constexpr bool kVaultRequiresSessionLock = true;
#else
inline constexpr bool kVaultRequiresSessionLock = false;
#endif

inline std::array<unsigned char, 32> runtimeLockKey{};
inline std::atomic<bool> runtimeLockArmed{false};

// Called ONCE by session_bind::verifySession after the injection trailer verified. The key
// is derived from trailer material at runtime - it exists nowhere in the module binary.
inline void armRuntimeLock(const unsigned char key32[32]) noexcept
{
    for (std::size_t i = 0; i < runtimeLockKey.size(); ++i)
        runtimeLockKey[i] = key32[i];
    runtimeLockArmed.store(true, std::memory_order_release);
}

[[nodiscard]] inline bool vaultUnlockAllowed() noexcept
{
    return !kVaultRequiresSessionLock || runtimeLockArmed.load(std::memory_order_acquire);
}

[[nodiscard]] inline char runtimeLockByte(std::size_t index) noexcept
{
    return static_cast<char>(runtimeLockKey[index & (runtimeLockKey.size() - 1)]);
}

} // namespace pattern_vault
