#pragma once

#include <atomic>





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
