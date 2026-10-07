#pragma once

#include <cstdint>

namespace cs2
{

// The local account's econ state, as the CLIENT holds it. Reached through the chain in
// sub_1E4DA90, which is the function behind the "elevated" / "awaiting_cooldown" /
// "account_cooldown" status strings:
//
//   object = *(void**)(EconSystemAccessor() + kEconClientOffset)
//   account = GameAccountClientAccessor(*(void**)(object + kSharedObjectCacheOffset))
//   elevated = *(uint32*)(account + kElevatedStateOffset) == kElevatedStatePrime
//
// The same layout the Windows build uses - nElevatedState really is at 0x30 on both.
struct CEconGameAccountClient {
    // Offset from the object returned by EconSystemAccessor to the econ client
    // (`mov rax, [rax+12C848h]` - moved from 0x115868 in the 1.41.8.2 recompile; the live
    // EconSystemAccessor pattern embeds the literal bytes 48 8B 80 48 C8 12 00, so if this
    // constant and the pattern ever disagree, the PATTERN is the current one).
    static constexpr int kEconClientOffset = 0x12C848;

    // The shared-object cache handle that econ client holds, which the account-client accessor
    // takes as its only argument (`mov rdi, [rax+68h]`).
    static constexpr int kSharedObjectCacheOffset = 0x68;

    static constexpr int kElevatedStateOffset = 48;

    // 5 is what every consumer compares against for "this account is elevated"; sub_1E4DA90 tests
    // `*(v3 + 48) != 5` before it will report "elevated", and 2 and 6 are the two cooldown states.
    static constexpr std::uint32_t kElevatedStatePrime = 5;
};

}
