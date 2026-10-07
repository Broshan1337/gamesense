#pragma once

#include <array>
#include <atomic>
#include <cstddef>















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


















namespace pattern_vault
{

#if defined(NS_PATTERN_VAULT_REQUIRES_SESSION)
inline constexpr bool kVaultRequiresSessionLock = true;
#else
inline constexpr bool kVaultRequiresSessionLock = false;
#endif

inline std::array<unsigned char, 32> runtimeLockKey{};
inline std::atomic<bool> runtimeLockArmed{false};



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

} 
