#pragma once

#include <cstdint>

#include <Utils/MurmurHash2.h>

namespace cs2
{









struct CUtlStringToken {
    explicit CUtlStringToken(const char* string) noexcept
        : hashCode{murmurHash2Lower(string, constLength(string), 0x31415926)}
        , debugName{string}
    {
    }

    std::uint32_t hashCode{};
    std::uint32_t pad{};
    const char* debugName{};
};

}
