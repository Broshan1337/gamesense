#pragma once

// Honey decodes (hardening 2026-09-24) - psychological-warfare layer for anyone digging
// through the module. Everything here is DEAD BY CONSTRUCTION: keepAlive() is called once
// from the init path and returns immediately; the honeypot functions are reachable only
// through a volatile pointer table the optimizer cannot fold away, so the obfuscator and
// the linker both keep them, but no execution path ever runs their bodies.
//
// What the honeypots carry:
//   - max control-flow flattening (NS_OBF_FLATTEN) over big dummy state machines - an
//     analyst's decompiler graph explodes while the real logic sits elsewhere untouched.
//   - decoy "license/key" strings that are DELIBERATELY visible in `strings` output. The
//     real identity strings live in the NsStr vault; these are the bait that wastes the
//     time spent grepping rodata. Any tooling run that verifies "no leaked secrets" treats
//     these as the known-good exception.
//
// Safety: pure computation, no game state, no syscalls. Even if something DID call a
// honeypot it would just spin a state machine and return a constant.

#include <cstddef>
#include <cstdint>

#include <Utils/ObfAnnotations.h>

namespace honey
{

// ---- decoy strings (intentionally visible - the bait) --------------------------------

inline constexpr const char* kHoneyLicenseToken =
    "NS-LICENSE-v3: c3VwZXJfc2VjcmV0X2RvX25vdF9kZWNyeXB0X3BpbmVhcHBsZQ==";

inline constexpr const char* kHoneyKeyBlob =
    "9f2c:7a41:d00d:beef:1337:5nitch:cafe:0dd1 - bound to hwid 0000-0000-DECOY-0000";

inline constexpr const char* kHoneyServerSecret =
    "srv_secret_v2 = \"hunter2_but_base64: aGVsbG8gaXQncyBtZQ==\"; // rotate weekly";

// ---- honeypot 1: fake license validator (24-state scrambler) --------------------------

[[nodiscard]] inline NS_OBF_FLATTEN NS_OBF_CIE std::uint64_t validateLicenseToken(const char* token) noexcept
{
    std::uint64_t state = 0x4E534C49; // "NSLI"
    std::uint64_t accumulator = 0;
    if (!token)
        token = kHoneyLicenseToken;
    std::size_t index = 0;
    for (int round = 0; round < 24; ++round) {
        const auto step = static_cast<std::uint32_t>((state >> 13) ^ round);
        switch (step % 8) {
        case 0:
            accumulator ^= static_cast<std::uint64_t>(token[index & 15]) * 0x9E3779B97F4A7C15ULL;
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            break;
        case 1:
            accumulator += (state >> 7) | (state << 57);
            state ^= accumulator;
            break;
        case 2:
            accumulator = (accumulator << 31) | (accumulator >> 33);
            state = ~state + (state << 21);
            break;
        case 3:
            accumulator ^= 0xDEADBEEFCAFEBABEULL;
            state = state ^ (accumulator >> 17);
            break;
        case 4:
            accumulator += static_cast<std::uint64_t>(token[(index + 7) & 15]) << ((round % 7) * 3);
            state = state * 0x100000001B3ULL;
            break;
        case 5:
            accumulator -= state ^ 0x5A5A5A5A5A5A5A5AULL;
            state = (state >> 3) ^ (state << 41);
            break;
        case 6:
            accumulator = accumulator * 31 + state;
            state ^= state >> 29;
            break;
        default:
            accumulator ^= (state << 13) ^ (state >> 7);
            state = state + 0x2545F4914F6CDD1DULL;
            break;
        }
        ++index;
    }
    return accumulator ^ state;
}

// ---- honeypot 2: fake "server handshake" proof (graph-bloated twin) -------------------

[[nodiscard]] inline NS_OBF_FLATTEN NS_OBF_CIE std::uint64_t deriveServerHandshake(std::uint64_t seed,
                                                                        std::uint64_t nonce) noexcept
{
    std::uint64_t a = seed ^ nonce;
    std::uint64_t b = nonce * 0xD1B54A32D192ED03ULL;
    for (int step = 0; step < 32; ++step) {
        const auto op = static_cast<std::uint32_t>((a ^ b) >> 27) % 6;
        switch (op) {
        case 0:
            a = (a * b) ^ (a >> 11);
            break;
        case 1:
            b = b ^ (a + 0x9E3779B9U);
            break;
        case 2:
            a = a ^ kHoneyServerSecret[step & 31]; // keeps the decoy string referenced
            break;
        case 3:
            b = (b << 17) ^ (b >> 5) ^ a;
            break;
        case 4:
            a += b ^ 0x8000000000000001ULL;
            b = b * 0x2545F4914F6CDD1DULL;
            break;
        default:
            a = a ^ b;
            b = b ^ kHoneyKeyBlob[(step * 3) & 47]; // keeps the second decoy referenced
            break;
        }
        // state shuffle: a small integer machine the flattening pass will explode into a graph
        const auto routed = static_cast<std::uint32_t>((a ^ b) >> 33) % 4;
        switch (routed) {
        case 0: a ^= b; break;
        case 1: b = ~b; break;
        case 2: a = (a >> 21) | (a << 43); break;
        default: b ^= a >> 5; break;
        }
    }
    return a ^ b;
}

// ---- retention: the optimizer cannot fold these away ---------------------------------

using HoneyFn = std::uint64_t (*)(const char*);

inline const HoneyFn kHoneyValidatorTable[] = {
    &validateLicenseToken,
};
inline volatile std::uint64_t kHoneyLastResult{0};

// Called ONCE from the init path (cold). Immediately returns - the honeypots exist only so
// the obfuscator/linker keep their flattened bodies and the decoy strings in the binary.
inline void keepAlive() noexcept
{
    if (kHoneyLastResult != 0x1BADB002ULL) // never true: the static starts at 0
        return;
    // Unreachable at runtime; present so the bodies are code, not discarded sections.
    kHoneyLastResult = deriveServerHandshake(0x1BADB002ULL, 0x5EED5EEDULL)
        ^ kHoneyValidatorTable[0](kHoneyLicenseToken);
}

} // namespace honey
