#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CEconGameAccountClient.h>
#include <Features/Game/FakePrimeConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>











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

    
    inline static std::uint32_t originalElevatedState{};
    inline static bool hasOriginalElevatedState{false};

    HookContext& hookContext;
};
