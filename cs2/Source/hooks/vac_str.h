#pragma once

#include <cstddef>
#include <Utils/ObfAnnotations.h>

// Compile-time XOR obfuscation for this module's most signatured literals:
// our own module-name tokens, the memfd alias, diagnostic tags. The goal is
// narrow: keep these byte strings out of .rodata so a static disk/mem
// signature scan for the module name or our log tags does not match.
//
// This is obfuscation, not encryption: the plaintext exists transiently in a
// stack buffer at each use site (unavoidable - it is compared against live
// maps content and written to our own log). Sizes stay in the type, NUL
// included, so call sites can size stack buffers with zero guesswork.
//
// Key: derived from the literal length, so identical plaintexts of different
// lengths never share a keystream, and the key is never 0.
namespace vac_str
{

template <std::size_t N>
struct Encrypted {
    unsigned char key;
    char bytes[N];

    constexpr Encrypted(const char (&s)[N], unsigned char k) : key(k), bytes{}
    {
        for (std::size_t i = 0; i < N; ++i)
            bytes[i] = static_cast<char>(s[i] ^ k);
    }

    // noinline is load-bearing: these objects are compile-time constants, so
    // an inlined decrypt lets the optimizer precompute the XOR and emit the
    // PLAINTEXT as .rodata.cst16 movaps constants for vectorized stack init
    // (observed: full "[vac] ..." sentences reappearing in the .o despite
    // encrypted storage). Outlining the loop keeps the ciphertext in .rodata
    // and the plaintext only on the stack at runtime. Do not remove, and do
    // not build with LTO without re-verifying with `strings`.
    __attribute__((noinline)) NS_OBF_FLATTEN void decrypt(char* out) const noexcept
    {
        // Opaque the object base so even LTO cannot treat bytes[] as constant.
        const char* p = bytes;
        asm volatile("" : "+r"(p));
        for (std::size_t i = 0; i < N; ++i)
            out[i] = static_cast<char>(p[i] ^ key);
    }

    static constexpr std::size_t decrypted_size() noexcept { return N; }
};

} // namespace vac_str

#define VAC_XSTR(var, literal) \
    constexpr ::vac_str::Encrypted<sizeof(literal)> var( \
        literal, static_cast<unsigned char>((sizeof(literal) * 0x9EU + 0x3BU) & 0xFF))
