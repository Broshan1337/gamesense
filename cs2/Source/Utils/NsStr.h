#pragma once

#include <cstddef>
#include <string_view>
#include <Utils/ObfAnnotations.h>























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

    
    __attribute__((noinline)) NS_OBF_FLATTEN void decrypt(char* out) const noexcept
    {
        const char* p = bytes;
        asm volatile("" : "+r"(p));
        for (std::size_t i = 0; i < N; ++i)
            out[i] = static_cast<char>(p[i] ^ key);
    }

    static constexpr std::size_t decrypted_size() noexcept { return N; }
};




template <std::size_t N>
struct Plaintext {
    char data[N];

    explicit Plaintext(const Encrypted<N>& e) noexcept { e.decrypt(data); }

    const char* c_str() const noexcept { return data; }
    std::size_t size() const noexcept { return N - 1; }
    operator const char*() const noexcept { return data; }
    std::string_view to_view() const noexcept { return std::string_view(data, N - 1); }
};

} 



#define NS_STR(name, literal)                                       \
    constexpr ::ns_str::Encrypted<sizeof(literal)> ns_enc_##name(literal); \
    ::ns_str::Plaintext<sizeof(literal)> name{ns_enc_##name}




#define NS_DEC(name, enc) ::ns_str::Plaintext<(enc).decrypted_size()> name{enc}
