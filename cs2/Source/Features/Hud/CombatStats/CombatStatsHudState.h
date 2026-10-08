#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include <Utils/SpinLock.h>





namespace combat_stats_hud
{



struct FeedEntry {
    char kind{0};        
    char name[44]{};
    int damage{0};
    bool headshot{false};
    int missCount{0};
    double spawnTime{0.0}; 
};



inline constexpr double kFeedSlideSeconds = 0.3;
inline constexpr float kFeedSlidePixels = 6.0f;

inline constexpr int kFeedLines = 5;

inline SpinLock lock;
inline std::uint32_t shotsFired{0};
inline std::uint32_t hitsLanded{0};
inline FeedEntry feedEntries[kFeedLines]{};




inline std::atomic<bool> hudLive{false};

inline void publish(std::uint32_t shots, std::uint32_t hits, const FeedEntry* entries, int count) noexcept
{
    if (count > kFeedLines)
        count = kFeedLines;
    const std::lock_guard guard{lock};
    shotsFired = shots;
    hitsLanded = hits;
    if (entries) {
        for (int i = 0; i < count; ++i)
            feedEntries[i] = entries[i];
    }
}

struct Snapshot {
    std::uint32_t shotsFired{0};
    std::uint32_t hitsLanded{0};
    FeedEntry feedEntries[kFeedLines]{};
};

[[nodiscard]] inline Snapshot snapshot() noexcept
{
    const std::lock_guard guard{lock};
    Snapshot s;
    s.shotsFired = shotsFired;
    s.hitsLanded = hitsLanded;
    for (int i = 0; i < kFeedLines; ++i)
        s.feedEntries[i] = feedEntries[i];
    return s;
}

}
