#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CEconGameAccountClient.h>
#include <Features/Game/FakePrimeConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Makes OUR OWN client display the account as Prime.
//
// This is cosmetic and local, in the same sense the skin changer is: it edits a value this client
// holds about itself, so this client's own UI reads it back. It does not and cannot grant Prime -
// the server decides that from its own records, matchmaking re-checks it there, and nothing written
// here is ever sent anywhere. Turning it off restores what the game had.
//
// The value is rewritten every frame rather than once, because the econ client refreshes this
// struct whenever the game coordinator sends a shared-object update, which would otherwise quietly
// revert it.
template <typename HookContext>
class FakePrime {
public:
    explicit FakePrime(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        auto* const elevatedState = findElevatedState();
        if (!elevatedState)
            return;

        std::uint32_t current{};
        std::memcpy(&current, elevatedState, sizeof(current));

        if (GET_CONFIG_VAR(FakePrimeEnabled)) {
            // Anything that is not our own spoofed value is the game's real state, so it becomes
            // what we restore. Capturing it on every observation rather than only the first means a
            // refresh from the game coordinator updates the saved value instead of leaving us
            // holding something stale.
            if (current != cs2::CEconGameAccountClient::kElevatedStatePrime) {
                originalElevatedState = current;
                hasOriginalElevatedState = true;
            }

            write(elevatedState, cs2::CEconGameAccountClient::kElevatedStatePrime);
        } else if (hasOriginalElevatedState) {
            write(elevatedState, originalElevatedState);
            hasOriginalElevatedState = false;
        }
    }

    void onUnload() const noexcept
    {
        if (!hasOriginalElevatedState)
            return;

        if (auto* const elevatedState = findElevatedState())
            write(elevatedState, originalElevatedState);

        hasOriginalElevatedState = false;
    }

private:
    // Walks the chain from sub_1E4DA90. Every step is null-checked exactly where the game checks it,
    // because this runs per frame and the econ client does not exist until the game coordinator
    // session is up - before that the pointers are legitimately null rather than broken.
    [[nodiscard]] std::byte* findElevatedState() const noexcept
    {
        const auto econSystem = hookContext.patternSearchResults().template get<EconSystemAccessor>();
        const auto gameAccountClient = hookContext.patternSearchResults().template get<GameAccountClientAccessor>();
        if (!econSystem || !gameAccountClient)
            return nullptr;

        auto* const system = static_cast<std::byte*>(econSystem());
        if (!system)
            return nullptr;

        std::byte* econClient = nullptr;
        std::memcpy(&econClient, system + cs2::CEconGameAccountClient::kEconClientOffset, sizeof(econClient));
        if (!econClient)
            return nullptr;

        void* sharedObjectCache = nullptr;
        std::memcpy(&sharedObjectCache, econClient + cs2::CEconGameAccountClient::kSharedObjectCacheOffset, sizeof(sharedObjectCache));

        auto* const account = static_cast<std::byte*>(gameAccountClient(sharedObjectCache));
        if (!account)
            return nullptr;

        return account + cs2::CEconGameAccountClient::kElevatedStateOffset;
    }

    static void write(std::byte* elevatedState, std::uint32_t value) noexcept
    {
        std::memcpy(elevatedState, &value, sizeof(value));
    }

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static std::uint32_t originalElevatedState{};
    inline static bool hasOriginalElevatedState{false};

    HookContext& hookContext;
};
