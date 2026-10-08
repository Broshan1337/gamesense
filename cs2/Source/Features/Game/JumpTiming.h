#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace jump_timing {
struct State {
    double lastPress{-std::numeric_limits<double>::infinity()};
    bool mayPress(double now, float penalty) const noexcept {
        return std::isfinite(now) && std::isfinite(penalty) && penalty >= 0
            && (now < lastPress || now - lastPress > penalty + 1.0 / 4096);
    }
    void pressed(double now) noexcept { lastPress = now; }
    void reset() noexcept { *this = {}; }
};
inline float landingWhen(float fraction) noexcept {
    if (!std::isfinite(fraction)) return 0;
    return std::clamp(std::round(fraction * 64) / 64, 1.0f / 64, 63.0f / 64);
}
}
