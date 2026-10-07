#pragma once
#include <algorithm>
#include <cstdint>

namespace shot_wait {
// Accuracy wait is an optional delay before accepting the configured hitchance
// estimate; maximum accuracy always fires immediately. Damage and ground/air
// eligibility remain hard requirements, including after timeout.
class State {
public:
    void reset() noexcept { target=0; started=-1; }
    bool ready(std::uint32_t handle, int tick, bool eligible, bool maxAccuracy,
               bool chancePassed, bool wait, int delay) noexcept
    {
        if (!eligible || !handle || tick<=0) { reset(); return false; }
        if (maxAccuracy) { reset(); return true; }
        if (!wait) { reset(); return chancePassed; }
        if (target!=handle || started<0 || tick<started) { target=handle; started=tick; }
        return chancePassed && tick-started>=std::max(1,delay);
    }
private:
    std::uint32_t target{};
    int started{-1};
};
}
