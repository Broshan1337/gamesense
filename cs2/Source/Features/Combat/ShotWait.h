#pragma once
#include <algorithm>
#include <cstdint>

namespace shot_wait {
// Accuracy wait is an optional delay before accepting the configured hitchance
// estimate; maximum accuracy always fires immediately. Damage and ground/air
// eligibility remain hard requirements, including after timeout.
// The delay runs from the tick the hitchance starts passing, and restarts when
// it stops passing. It used to run from the tick the target became eligible,
// so a target watched for a while (e.g. before scoping in) had already used
// the whole delay and the shot left on the first passing estimate.
class State {
public:
    void reset() noexcept { target=0; started=-1; }
    bool ready(std::uint32_t handle, int tick, bool eligible, bool maxAccuracy,
               bool chancePassed, bool wait, int delay) noexcept
    {
        if (!eligible || !handle || tick<=0) { reset(); return false; }
        if (maxAccuracy) { reset(); return true; }
        if (!wait) { reset(); return chancePassed; }
        if (!chancePassed) { reset(); return false; }
        if (target!=handle || started<0 || tick<started) { target=handle; started=tick; }
        return tick-started>=std::max(1,delay);
    }
private:
    std::uint32_t target{};
    int started{-1};
};
}
