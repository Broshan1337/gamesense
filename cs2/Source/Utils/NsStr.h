#pragma once

#include <cstddef>
#include <string_view>
#include <Utils/ObfAnnotations.h>

// Compile-time XOR obfuscation for identity-bearing literals (the module's
// name/path vocabulary): brand strings, config/log/crash paths, the Panorama
// hook script, Discord presence text. The goal is narrow: keep these byte
// strings out of .rodata AND out of code immediates (the optimizer will happily
// emit a short string literal as a movabs immediate inside the function body -
// `strings` finds those too), so a static disk/memory scan for "who is this
// module" does not match.
//
// Same machinery and same caveats as hooks/vac_str.h:
//  - obfuscation, not encryption: the plaintext exists transiently in a stack
//    buffer at each use site (it is compared against live game state or passed
//    to the engine/OS, so it must exist).
//  - the decrypt is noinline + asm-opaque: without that, the optimizer
//    precomputes the XOR and emits the PLAINTEXT (as .rodata.cst16 vector
//    stores or as short-string movabs immediates). Do not remove, and do not
//    build with LTO without re-verifying with `strings`.
//  - key is derived from the literal length AND content, never zero.
//
// Use-site pattern:
//     NS_STR(logPath, "/tmp/gamesense_gui.log");   // in a function body
//     std::fopen(logPath, "r");                    // Plaintext -> const char*
//     logPath.size();                              // length without NUL
namespace ns_str
{

constexpr unsigned char keyFor(std::size_t len, const char* s) noexcept
{
    unsigned sum = 0;
    for (std::size_t i = 0; i + 1 < len; ++i)
        sum += static_cast<unsigned char>(s[i]);
    const unsigned char k = static_cast<unsigned char>((len * 0x9EU + sum * 0x3BU + 0x2AU) & 0xFF);
    return k ? k : static_cast<unsigned char>(0x5A);
}

template <std::size_t N>
struct Encrypted {
    unsigned char key;
    char bytes[N];

    constexpr Encrypted(const char (&s)[N]) : key(keyFor(N, s)), bytes{}
    {
        for (std::size_t i = 0; i < N; ++i)
            bytes[i] = static_cast<char>(s[i] ^ key);
    }

    // noinline is load-bearing, see the file comment.
    __attribute__((noinline)) NS_OBF_FLATTEN void decrypt(char* out) const noexcept
    {
        const char* p = bytes;
        asm volatile("" : "+r"(p));
        for (std::size_t i = 0; i < N; ++i)
            out[i] = static_cast<char>(p[i] ^ key);
    }

    static constexpr std::size_t decrypted_size() noexcept { return N; }
};

// Stack-resident decrypted string. Implicitly converts to const char* (the
// overwhelmingly common use); std::string_view is opt-in via to_view() to keep
// overload resolution unambiguous.
template <std::size_t N>
struct Plaintext {
    char data[N];

    explicit Plaintext(const Encrypted<N>& e) noexcept { e.decrypt(data); }

    const char* c_str() const noexcept { return data; }
    std::size_t size() const noexcept { return N - 1; }
    operator const char*() const noexcept { return data; }
    std::string_view to_view() const noexcept { return std::string_view(data, N - 1); }
};

} // namespace ns_str

// Declares the encrypted literal + its stack plaintext under one name. The name
// behaves like a local const char* variable.
#define NS_STR(name, literal)                                       \
    constexpr ::ns_str::Encrypted<sizeof(literal)> ns_enc_##name(literal); \
    ::ns_str::Plaintext<sizeof(literal)> name{ns_enc_##name}

// Materializes a stack plaintext from a namespace/class-level Encrypted member:
//     static constexpr ns_str::Encrypted<sizeof("...")> kFooEnc{"..."};
//     NS_DEC(foo, kFooEnc);   // function-local stack plaintext
#define NS_DEC(name, enc) ::ns_str::Plaintext<(enc).decrypted_size()> name{enc}
