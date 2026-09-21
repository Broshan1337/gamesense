#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include <Utils/SpinLock.h>

// COMBAT HUD shared state (hit counters + hit feed). The counters and feed lines are ONLY
// touched on the game thread (CombatStats event bookkeeping + its run() publish); the present
// thread draws them through the publish/snapshot spinlock pair - the same shape as
// cheat_ometer's HUD snapshot and SpectatorSnapshot.
namespace combat_stats_hud
{

// One feed line, rendered as colored segments: faint verb + accent name + accent damage +
// faint suffix ("hit <name> for <dmg> in head" / "killed <name>" / "missed xN").
struct FeedEntry {
    char kind{0};        // 0 = empty slot, 'h' hit, 'k' kill, 'm' miss
    char name[44]{};
    int damage{0};
    bool headshot{false};
    int missCount{0};
    double spawnTime{0.0}; // monotonic seconds - drives the slide/fade entrance
};

// Entrance animation: seconds from spawn to full opacity, and the px slide distance. Kept here
// because the semantics belong to the entry lifecycle, while the drawing happens present-side.
inline constexpr double kFeedSlideSeconds = 0.3;
inline constexpr float kFeedSlidePixels = 6.0f;

inline constexpr int kFeedLines = 5;

inline SpinLock lock;
inline std::uint32_t shotsFired{0};
inline std::uint32_t hitsLanded{0};
inline FeedEntry feedEntries[kFeedLines]{};

// "a session is running" gate, published by CombatStats::run() (game thread) - the old
// Panorama boxes had it for free (their HUD root only existed in-match); the draw side uses
// it so the boxes never float over the main menu.
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
