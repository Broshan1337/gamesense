#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>

// Session-level init health ledger. Feature modules that validate runtime prerequisites
// (signature-checked hardcoded offsets, pattern-resolved functions...) record one line here,
// pass or fail. The first render frame after init dumps the whole list into the game console
// under [status] - open ~ right after injection and everything that decided to fail closed is
// visible immediately instead of being debugged as "the feature mysteriously does nothing".
namespace StatusReport
{
    inline constexpr int kMaxEntries = 32;

    struct Entry {
        const char* message{nullptr};
        bool ok{false};
    };

    inline Entry entries[kMaxEntries];
    inline std::atomic<int> count{0};
    // Guarded by count transitions; dumping happens once, from a single thread.
    inline std::atomic<bool> dumped{false};

    inline void record(const char* message, bool ok) noexcept
    {
        const auto index = count.fetch_add(1, std::memory_order_acq_rel);
        if (index >= 0 && index < kMaxEntries)
            entries[index] = Entry{message, ok};
    }

    // The console write itself needs engine readiness - iterate lazily via callback from the
    // caller (ViewRenderHook_onRenderStart), keeping this header free of game dependencies.
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
