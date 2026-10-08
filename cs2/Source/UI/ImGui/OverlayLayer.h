#pragma once

#include <cstdint>
#include <mutex>

#include <Utils/SpinLock.h>








namespace overlay_layer
{

struct Arrow {
    float xPercent;   
    float yPercent;
    float angleDeg;   
};

constexpr int kMaxArrows = 8;

inline SpinLock lock;
inline Arrow arrows[kMaxArrows];
inline int arrowCount = 0;


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
