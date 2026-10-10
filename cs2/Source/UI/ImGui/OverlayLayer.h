#pragma once

#include <cstdint>
#include <mutex>

#include <Utils/SpinLock.h>








namespace overlay_layer
{

// Producer side (game thread) publishes raw data; renderGameOverlay draws it.
// The old per-frame producer for Arrow was removed with the velocity-ESP tab;
// the struct is repurposed for the off-screen enemy arrows feature.

struct Arrow {
    float xPercent;   // projected enemy head position, percent of screen width (may be off-screen)
    float yPercent;   // projected enemy head position, percent of screen height
    bool behind;      // clip w <= 0: projected point mirrors through screen center
};

constexpr int kMaxArrows = 16;

inline SpinLock lock;
inline Arrow arrows[kMaxArrows];
inline int arrowCount = 0;

// Bullet tracers are IN-GAME (the weapon's own tracer particle system spawned
// via the game's fx primitive - see Features/Visuals/BulletTracers): they are
// NOT overlay primitives and never pass through here.

// Kill-effect lightning bolts: a strike from the SKY down to the victim's
// death position. The producer projects the death spot AND the world point
// straight above it (2500u up) and publishes both as screen percents; the
// draw side converts to pixels, follows the base->sky direction (capped at
// 1.5x screen height, clamped to never draw sideways/below - the floor-lines
// bug class), and builds the jagged channel + forks there.
struct Bolt {
    float xPercent;       // strike point (victim death position)
    float yPercent;
    float upXPercent;   // sky end (world point 2500u above the kill, projected)
    float upYPercent;
    float ageScale;        // 0 = fresh .. 1 = expired (fade baked in by producer)
    std::uint32_t seed;
    std::uint32_t rgba;
};

constexpr int kMaxBolts = 4;

inline SpinLock boltLock;
inline Bolt bolts[kMaxBolts];
inline int boltCount = 0;


struct Hitmarker {
    float gap;
    float length;
    std::uint32_t rgba; 
};

inline SpinLock hitmarkerLock;
inline Hitmarker hitmarker{};
inline bool hasHitmarker = false;



struct Timer {
    float xPercent; 
    float yPercent; 
    float seconds;
    std::uint32_t rgba;
};

constexpr int kMaxTimers = 16;

inline SpinLock timersLock;
inline Timer timers[kMaxTimers];
inline int timerCount = 0;


inline void publishTimers(const Timer* newTimers, int count) noexcept
{
    if (count > kMaxTimers)
        count = kMaxTimers;
    const std::lock_guard guard{timersLock};
    for (int i = 0; i < count; ++i)
        timers[i] = newTimers[i];
    timerCount = count;
}


inline void publish(const Arrow* newArrows, int count) noexcept
{
    if (count > kMaxArrows)
        count = kMaxArrows;
    const std::lock_guard guard{lock};
    for (int i = 0; i < count; ++i)
        arrows[i] = newArrows[i];
    arrowCount = count;
}

inline void publishBolts(const Bolt* newBolts, int count) noexcept
{
    if (count > kMaxBolts)
        count = kMaxBolts;
    const std::lock_guard boltGuard{boltLock};
    for (int i = 0; i < count; ++i)
        bolts[i] = newBolts[i];
    boltCount = count;
}


inline void publishHitmarker(const Hitmarker& newHitmarker) noexcept
{
    const std::lock_guard guard{hitmarkerLock};
    hitmarker = newHitmarker;
    hasHitmarker = newHitmarker.rgba != 0;
}


struct Snapshot {
    Arrow arrows[kMaxArrows];
    int count = 0;
    Hitmarker hitmarker{};
    bool hasHitmarker = false;
    Timer timers[kMaxTimers];
    int timerCount = 0;
    Bolt bolts[kMaxBolts];
    int boltCount = 0;
};

[[nodiscard]] inline Snapshot snapshot() noexcept
{
    const std::lock_guard guard{lock};
    Snapshot s;
    s.count = arrowCount;
    for (int i = 0; i < arrowCount; ++i)
        s.arrows[i] = arrows[i];
    {
        const std::lock_guard hitmarkerGuard{hitmarkerLock};
        s.hitmarker = hitmarker;
        s.hasHitmarker = hasHitmarker;
    }
    {
        const std::lock_guard timersGuard{timersLock};
        s.timerCount = timerCount;
        for (int i = 0; i < timerCount; ++i)
            s.timers[i] = timers[i];
    }
    {
        const std::lock_guard boltGuard{boltLock};
        s.boltCount = boltCount;
        for (int i = 0; i < boltCount; ++i)
            s.bolts[i] = bolts[i];
    }
    return s;
}

}
