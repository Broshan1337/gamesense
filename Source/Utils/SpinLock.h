#pragma once

#include <atomic>

// One-flag spinlock for the freestanding build (no std::mutex / pthread dep). Suitable for the
// short critical sections and low contention this project uses it for (event staging queue,
// present-path render serialization). Always acquire with memory_order_acquire and release with
// memory_order_release; pause on the spinning path.
class SpinLock {
public:
    void lock() noexcept
    {
        while (flag.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }

    void unlock() noexcept
    {
        flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};
