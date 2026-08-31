#pragma once

#include <atomic>
#include <cstdint>

// Unload quiesce: hooks run on several game threads (render, input, events, scene-system workers).
// Restoring vftables while any of those threads is inside one of our callbacks means the next
// HookContext it constructs dereferences a destroyed FullGlobalContext. So shutdown happens in two
// phases:
//
//   1. beginShutdown() latches an atomic flag; every entry point checks it FIRST and, once set,
//      short-circuits its feature logic (it still calls the game's original function - skipping that
//      would break input/crc/event flow during the teardown frames).
//   2. The teardown thread drains a counter of in-flight hook callbacks before destroyGlobalContext().
//      With the flag set, no NEW callback touches context data and the ones already inside finish in
//      bounded time (they are single-frame work), so the wait terminates on its own.
//
// Not counted: the thread that itself initiated the drain (ViewRenderHook_onRenderStart) - it waits
// AFTER its own callback body is done.
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

    // Waits until every other-thread hook callback has left our code. Called only after the flag is
    // set (so no new flights start). Yield-loop with a hard iteration cap: even if something went
    // wrong the unload must still proceed - tearing down 0.5s late beats hanging the render thread.
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
