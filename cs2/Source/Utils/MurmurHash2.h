#pragma once

#include <cstdint>







[[nodiscard]] inline std::uint32_t murmurHash2Lower(const char* str, int len, std::uint32_t seed) noexcept
{
    constexpr std::uint32_t m = 0x5bd1e995;
    constexpr std::uint32_t r = 24;

    const auto lower = [](char c) -> std::uint32_t {
        return (c >= 'A' && c <= 'Z') ? static_cast<std::uint32_t>(c) + 32 : static_cast<std::uint32_t>(c);
    };

    std::uint32_t h = seed ^ static_cast<std::uint32_t>(len);
    int i = 0;

    while (len >= 4) {
        std::uint32_t k = lower(str[i]) | (lower(str[i + 1]) << 8) | (lower(str[i + 2]) << 16) | (lower(str[i + 3]) << 24);
        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        i += 4;
        len -= 4;
    }

    if (len == 3) {
        h ^= lower(str[i + 2]) << 16;
        h ^= lower(str[i + 1]) << 8;
        h ^= lower(str[i]);
        h *= m;
    } else if (len == 2) {
        h ^= lower(str[i + 1]) << 8;
        h ^= lower(str[i]);
        h *= m;
    } else if (len == 1) {
        h ^= lower(str[i]);
        h *= m;
    }

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}

[[nodiscard]] constexpr int constLength(const char* str) noexcept
{
    int length = 0;
    while (str[length] != '\0')
        ++length;
    return length;
}
