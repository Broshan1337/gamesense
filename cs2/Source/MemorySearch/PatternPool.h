#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>

#include <Utils/Meta.h>
#include <Utils/StrongTypeAlias.h>
#include <Utils/TypeList.h>

#include "CodePatternOperation.h"
#include "PatternPoolBuilder.h"
#include "PatternPoolView.h"
#include "PatternVault.h"
#include <Utils/ObfAnnotations.h>

template <std::size_t BufferSize = 0, std::size_t NumberOfPatterns = 0, typename PatternTypesList = TypeList<>>
class PatternPool {
public:
    template <PatternPoolBuilder builder>
    [[nodiscard]] static consteval auto from() noexcept
    {
        using SortedPatternTypes = typename decltype(builder)::PatternTypes::template sortBy<Projected<UnpackStrongTypeAlias, SizeOf>::Value>;
        PatternPool<builder.tempPool.bufferSize, builder.tempPool.numberOfPatterns, SortedPatternTypes> pool;
        copyPatterns(builder.tempPool, pool, typename decltype(builder)::PatternTypes{}, SortedPatternTypes{});
        // The emitted-at-rest pool buffer must be ciphertext (PatternVault.h): the plaintext
        // from the consteval builder is erased here, and getView() decrypts in place at
        // runtime before the first scan.
        pool.encrypt();
        return pool;
    }

    using PatternTypes = PatternTypesList;

    // Idle states: the buffer sits XOR-sealed either under the static consteval key (as
    // emitted at rest) or - once a scan window has run under the shipped module - under the
    // RUNTIME session key (wipe-after-scan: the plaintext exists in memory only during a
    // scan). getView unseals on demand; seal() re-locks.
    enum class SealState : unsigned char { kStaticSealed, kOpen, kRuntimeSealed };

    // Decrypts the vaulted pattern bytes on demand and returns the plaintext view.
    // Non-const on purpose: the runtime state of this object mutates between seal states.
    // FAIL-CLOSED: under the shipped module build an un-armed vault (no verified session
    // trailer) never unseals - the view then points at ciphertext and every scan finds
    // nothing. Offline builds (no NS_PATTERN_VAULT_REQUIRES_SESSION) always unseal.
    [[nodiscard]] __attribute__((annotate("+fla"))) PatternPoolView getView() noexcept
    {
        // CRITICAL (fixed same session, caught live in-game): the runtime session key lives
        // ONLY in the idle re-seal (kOpen -> kRuntimeSealed via seal()). Unsealing must strip
        // EXACTLY the key the current seal state carries - unsealing kStaticSealed with
        // K_static THEN K_runtime left the buffer double-XORed (= plaintext ^ K_runtime) and
        // every scan ran on garbage ("Failed to find pattern" dialogs with random bytes).
        if (sealState == SealState::kStaticSealed) {
            if (!pattern_vault::vaultUnlockAllowed())
                return {NumberOfPatterns, buffer.data(), patternLengths.data(), patternOffsets.data(), operations.data()};
            for (std::size_t i = 0; i < BufferSize; ++i)
                buffer[i] = static_cast<char>(buffer[i] ^ patternVaultKey(i));
            sealState = SealState::kOpen;
        } else if (sealState == SealState::kRuntimeSealed) {
            for (std::size_t i = 0; i < BufferSize; ++i)
                buffer[i] = static_cast<char>(buffer[i] ^ pattern_vault::runtimeLockByte(i));
            sealState = SealState::kOpen;
        }
        return {NumberOfPatterns, buffer.data(), patternLengths.data(), patternOffsets.data(), operations.data()};
    }

    // Wipe-after-scan: re-seal an open pool under the runtime session key. No-op on offline
    // builds (the runtime key is zero there - the pool simply stays open, like pre-hardening).
    void seal() noexcept
    {
        if (sealState != SealState::kOpen || !pattern_vault::kVaultRequiresSessionLock
            || !pattern_vault::vaultUnlockAllowed())
            return;
        for (std::size_t i = 0; i < BufferSize; ++i)
            buffer[i] = static_cast<char>(buffer[i] ^ pattern_vault::runtimeLockByte(i));
        sealState = SealState::kRuntimeSealed;
    }

private:
    template <typename... SourceTypes, typename... DestTypes>
    static consteval void copyPatterns(const auto& tempPatternPool, auto& pool, TypeList<SourceTypes...>, TypeList<DestTypes...>) noexcept
    {
        std::size_t outPatternIndex{0}, outBufferIndex{0};
        (copyPattern(TypeList<SourceTypes...>::template indexOf<DestTypes>(), outPatternIndex, outBufferIndex, tempPatternPool, pool), ...);
    }

    static consteval void copyPattern(std::size_t patternIndex, std::size_t& outIndex, std::size_t& outBuffer, const auto& tempPool, auto& pool) noexcept
    {
        const auto patternBufferIndex = std::accumulate(tempPool.patternLengths.begin(), tempPool.patternLengths.begin() + patternIndex, 0);
        std::copy_n(tempPool.buffer.begin() + patternBufferIndex, tempPool.patternLengths[patternIndex], pool.buffer.begin() + outBuffer);
        outBuffer += tempPool.patternLengths[patternIndex];
        pool.patternLengths[outIndex] = tempPool.patternLengths[patternIndex];
        pool.patternOffsets[outIndex] = tempPool.patternOffsets[patternIndex];
        pool.operations[outIndex] = tempPool.operations[patternIndex];
        ++outIndex;
    }

    template <std::size_t, std::size_t, typename>
    friend class PatternPool;

    consteval void encrypt() noexcept
    {
        for (std::size_t i = 0; i < BufferSize; ++i)
            buffer[i] = static_cast<char>(buffer[i] ^ patternVaultKey(i));
    }

    std::array<char, BufferSize> buffer{};
    std::array<std::uint8_t, NumberOfPatterns> patternLengths{};
    std::array<std::uint8_t, NumberOfPatterns> patternOffsets{};
    std::array<CodePatternOperation, NumberOfPatterns> operations{};
    SealState sealState{SealState::kStaticSealed};
};
