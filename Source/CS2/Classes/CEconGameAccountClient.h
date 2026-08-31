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
    // (`mov rax, [rax+115868h]`).
    static constexpr int kEconClientOffset = 0x115868;

    // The shared-object cache handle that econ client holds, which the account-client accessor
    // takes as its only argument (`mov rdi, [rax+68h]`).
    static constexpr int kSharedObjectCacheOffset = 0x68;

    static constexpr int kElevatedStateOffset = 48;

    // 5 is what every consumer compares against for "this account is elevated"; sub_1E4DA90 tests
    // `*(v3 + 48) != 5` before it will report "elevated", and 2 and 6 are the two cooldown states.
    static constexpr std::uint32_t kElevatedStatePrime = 5;
};

}
