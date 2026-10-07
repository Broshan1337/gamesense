#pragma once

#include <cstddef>
#include <Utils/ObfAnnotations.h>













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

    
    
    
    
    
    
    
    __attribute__((noinline)) NS_OBF_FLATTEN void decrypt(char* out) const noexcept
    {
        
        const char* p = bytes;
        asm volatile("" : "+r"(p));
        for (std::size_t i = 0; i < N; ++i)
            out[i] = static_cast<char>(p[i] ^ key);
    }

    static constexpr std::size_t decrypted_size() noexcept { return N; }
};

} 

#define VAC_XSTR(var, literal) \
    constexpr ::vac_str::Encrypted<sizeof(literal)> var( \
        literal, static_cast<unsigned char>((sizeof(literal) * 0x9EU + 0x3BU) & 0xFF))
