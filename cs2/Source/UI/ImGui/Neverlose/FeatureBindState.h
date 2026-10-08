#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace feature_binds {
struct Entry {
    std::uint64_t id{};
    const char* label{};
    double (*get)() = nullptr;
    bool (*set)(double) = nullptr;
    int key = 0;
    bool holdMode = false;
    bool lastKeyDown = false;
    bool numeric = false;
    bool integral = false;
    bool active = false;
    double minimum = 0.0, maximum = 1.0;
    double boundValue = 1.0;
    double restoreValue = 0.0;

    void restore() noexcept
    {
        if (active && set)
            set(restoreValue);
        active = false;
        lastKeyDown = false;
    }

    void setBoundValue(double value) noexcept
    {
        if (!std::isfinite(value))
            return;
        value = std::clamp(value, minimum, maximum);
        boundValue = integral ? std::round(value) : value;
        if (numeric && active && set)
            set(boundValue);
    }

    void update(bool down) noexcept
    {
        if (!key || !get || !set) {
            restore();
            return;
        }
        if (holdMode || numeric) {
            const bool rising = down && !lastKeyDown;
            const bool activate = holdMode ? rising : rising && !active;
            const bool deactivate = holdMode ? !down && active : rising && active;
            if (deactivate) {
                restore();
            } else if (activate) {
                restoreValue = get();
                active = true;
                set(numeric ? boundValue : 1.0);
            }
        } else if (down && !lastKeyDown) {
            set(get() == 0.0 ? 1.0 : 0.0);
        }
        lastKeyDown = down;
    }
};
}
