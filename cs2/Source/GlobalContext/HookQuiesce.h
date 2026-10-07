#pragma once

#include <atomic>
#include <cstdint>















namespace HookQuiesce
{
    inline std::atomic<bool> unloading{false};
    inline std::atomic<int> inFlight{0};

    [[nodiscard]] inline bool isShuttingDown() noexcept
    {
        return unloading.load(std::memory_order_acquire);
    }

    inline void beginShutdown() noexcept
    {
        unloading.store(true, std::memory_order_release);
    }

    struct InFlight {
        InFlight() noexcept { inFlight.fetch_add(1, std::memory_order_acq_rel); }
        ~InFlight() noexcept { inFlight.fetch_sub(1, std::memory_order_acq_rel); }

        InFlight(const InFlight&) = delete;
        InFlight& operator=(const InFlight&) = delete;
    };

    
    
    
    inline void drainInFlightCallbacks() noexcept
    {
        constexpr int kMaxSpins = 20000000;
        for (int i = 0; i < kMaxSpins && inFlight.load(std::memory_order_acquire) > 0; ++i) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }
}
