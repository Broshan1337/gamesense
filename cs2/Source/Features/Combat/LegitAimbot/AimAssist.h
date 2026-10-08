#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aim_assist {
enum class Mode { Smooth, Magnet, Snap };
struct Step { float pitch{}, yaw{}; };
struct Parameters {
    Mode mode{};
    float smooth{5}, strength{65}, deadzone{0.05f}, speed{180}, snapFov{2};
};
// Exponential response at a fixed reference tick rate gives the same response
// over elapsed time rather than changing speed with CreateMove frequency.
inline Step step(float pitch, float yaw, float dt, const Parameters& p) noexcept
{
    if (!std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(dt) || dt <= 0
        || !std::isfinite(p.smooth) || !std::isfinite(p.strength) || !std::isfinite(p.speed)
        || !std::isfinite(p.deadzone) || !std::isfinite(p.snapFov)) return {};
    yaw = std::remainder(yaw, 360.0f);
    const float distance = std::hypot(pitch, yaw);
    if (distance <= std::max(0.0f, p.deadzone)) return {};
    if (p.mode == Mode::Snap && distance <= p.snapFov) return {pitch, yaw};
    const float blend = 1 - std::exp(-dt * 64 / std::max(1.0f, p.smooth));
    float factor = blend * std::clamp(p.strength / 100, 0.0f, 1.0f);
    if (p.mode == Mode::Magnet) factor *= std::clamp(1 - distance / std::max(0.1f, p.snapFov * 2), 0.15f, 1.0f);
    factor = std::min(factor, std::max(0.0f, p.speed) * dt / distance);
    return {pitch * factor, yaw * factor};
}
struct Acquisition {
    std::uint32_t handle{}, previous{};
    float started{}, lastTime{};
    bool ready(std::uint32_t next, float now, float reaction, float switchDelay) noexcept {
        if (!next || !std::isfinite(now)) { reset(); return false; }
        if (now < lastTime) reset();
        if (handle != next) { previous = handle; handle = next; started = now; }
        lastTime = now;
        return now - started >= std::max(0.0f, previous ? switchDelay : reaction);
    }
    void reset() noexcept { *this = {}; }
};
}
