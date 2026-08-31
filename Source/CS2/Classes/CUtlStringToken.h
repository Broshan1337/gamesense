#pragma once

#include <cstdint>

#include <Utils/MurmurHash2.h>

namespace cs2
{

// Source 2's hashed-string key type. Game-event field accessors take this BY POINTER, not by
// value - confirmed from the Linux decompile of CGameEvent vtable slot 13, which reads three
// separate pieces out of its second argument: a dword at +0, a dword at +4, and a qword at +8.
//
// The layout below matches that: the hash is what the lookup actually keys on, and the trailing
// pointer is the debug/original string that Source 2 keeps alongside it in non-shipping builds.
// We populate both rather than just the hash, because slot 13 copies all 16 bytes through to the
// underlying lookup primitive (vtable slot 8) and we don't want to hand it uninitialised memory.
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
