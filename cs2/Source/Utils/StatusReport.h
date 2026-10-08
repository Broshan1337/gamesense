#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>






namespace StatusReport
{
    inline constexpr int kMaxEntries = 32;

    struct Entry {
        const char* message{nullptr};
        bool ok{false};
    };

    inline Entry entries[kMaxEntries];
    inline std::atomic<int> count{0};
    
    inline std::atomic<bool> dumped{false};

    inline void record(const char* message, bool ok) noexcept
    {
        const auto index = count.fetch_add(1, std::memory_order_acq_rel);
        if (index >= 0 && index < kMaxEntries)
            entries[index] = Entry{message, ok};
    }

    
    
    template <typename Printer>
    inline bool dumpOnce(Printer&& print) noexcept
    {
        bool expected = false;
        if (!dumped.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;
        const int total = count.load(std::memory_order_acquire);
        for (int i = 0; i < total && i < kMaxEntries; ++i)
            print(entries[i].message, entries[i].ok);
        return total > 0;
    }
}
