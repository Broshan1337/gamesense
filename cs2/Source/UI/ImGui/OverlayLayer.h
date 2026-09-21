#pragma once

#include <cstdint>
#include <mutex>

#include <Utils/SpinLock.h>

// Game-anchored overlay drawing: a tiny fixed-size frame buffer the game thread publishes
// screen-anchored primitives into, and the present thread draws on ImGui's foreground draw
// list (over every window, no ImGui window needed). First consumer: off-screen enemy arrows.
//
// Coordinates are PERCENT OF SCREEN so the feature stays resolution-independent.
// Exception: the hitmarker is screen-ANCHORED (crosshair center) and uses PIXEL sizes,
// because that is what its config sliders mean.
namespace overlay_layer
{

struct Arrow {
    float xPercent;   // 0..100, ellipse position around screen center
    float yPercent;
    float angleDeg;   // direction the arrow points
};

constexpr int kMaxArrows = 8;

inline SpinLock lock;
inline Arrow arrows[kMaxArrows];
inline int arrowCount = 0;

// Screen-anchored four-line hitmarker around the crosshair (pixel dimensions, pre-faded rgba).
struct Hitmarker {
    float gap;
    float length;
    std::uint32_t rgba; // color::Rgba packed 0xRRGGBBAA
};

inline SpinLock hitmarkerLock;
inline Hitmarker hitmarker{};
inline bool hasHitmarker = false;

// World-anchored grenade burn timer: percent-of-screen position of the projected world point,
// the remaining seconds, and the packed text color. The present thread formats and centers it.
struct Timer {
    float xPercent; // 0..100
    float yPercent; // 0..100
    float seconds;
    std::uint32_t rgba;
};

constexpr int kMaxTimers = 16;

inline SpinLock timersLock;
inline Timer timers[kMaxTimers];
inline int timerCount = 0;

// Game thread: replace (or clear, with count 0) the frame's timer set.
inline void publishTimers(const Timer* newTimers, int count) noexcept
{
    if (count > kMaxTimers)
        count = kMaxTimers;
    const std::lock_guard guard{timersLock};
    for (int i = 0; i < count; ++i)
        timers[i] = newTimers[i];
    timerCount = count;
}

// Game thread: atomically replace the frame's arrow set.
inline void publish(const Arrow* newArrows, int count) noexcept
{
    if (count > kMaxArrows)
        count = kMaxArrows;
    const std::lock_guard guard{lock};
    for (int i = 0; i < count; ++i)
        arrows[i] = newArrows[i];
    arrowCount = count;
}

// Game thread: replace (or clear, with {}) the frame's hitmarker.
inline void publishHitmarker(const Hitmarker& newHitmarker) noexcept
{
    const std::lock_guard guard{hitmarkerLock};
    hitmarker = newHitmarker;
    hasHitmarker = newHitmarker.rgba != 0;
}

// Present thread: snapshot under the lock.
struct Snapshot {
    Arrow arrows[kMaxArrows];
    int count = 0;
    Hitmarker hitmarker{};
    bool hasHitmarker = false;
    Timer timers[kMaxTimers];
    int timerCount = 0;
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
    return s;
}

}
