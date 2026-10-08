#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>


















namespace pawn_settle {

inline std::atomic<void*> trackedPawn{nullptr};
inline std::atomic<std::int64_t> firstSeenNs{0};




inline constexpr std::int64_t kSettleNs = 3'000'000'000;

[[nodiscard]] inline std::int64_t nowNs() noexcept
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
}



[[nodiscard]] inline bool ready(const void* pawn) noexcept
{
    if (!pawn)
        return false;
    if (trackedPawn.load(std::memory_order_acquire) != pawn) {
        trackedPawn.store(const_cast<void*>(pawn), std::memory_order_release);
        firstSeenNs.store(nowNs(), std::memory_order_release);
        return false;
    }
    return nowNs() - firstSeenNs.load(std::memory_order_acquire) >= kSettleNs;
}

} 