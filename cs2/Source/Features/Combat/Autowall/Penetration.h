#pragma once

#include <algorithm>
#include <cmath>

#include <Features/Combat/HitboxGeometry.h>
#include <Utils/Optional.h>

namespace penetration {

struct Bullet {
    float damage;
    float power;
    float rangeModifier{1.0f};
    float maxRange{8192.0f};

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(damage) && damage >= 1.0f && std::isfinite(power) && power >= 0.0f
            && std::isfinite(rangeModifier) && rangeModifier > 0.0f && rangeModifier <= 1.0f
            && std::isfinite(maxRange) && maxRange > 0.0f;
    }
};

struct Limits {
    float maxWallThickness{90.0f};
    float maxTotalThickness{360.0f};
    int maxPenetrations{4};
    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(maxWallThickness) && maxWallThickness > 0.0f && maxWallThickness <= 90.0f
            && std::isfinite(maxTotalThickness) && maxTotalThickness > 0.0f
            && maxPenetrations >= 0 && maxPenetrations <= 4;
    }
};

struct TraceBudget {
    int remaining{1024};
    [[nodiscard]] bool take() noexcept
    {
        if (remaining <= 0) return false;
        --remaining;
        return true;
    }
};

struct Impact {
    float damage;
    float distance;
    float thickness;
    int penetrations;
    int hitgroup{-1};
};

// Neutral material estimate: entry/exit surface modifiers are not exposed by
// the validated Linux trace adapter. No unverified engine ABI is invoked.
// Range decay applies to each newly traveled segment, rather than to the full
// distance before fixed penetration losses. Armor/hitgroup scaling happens last.
// Reference for loss terms and the four-surface limit:
// https://github.com/rollraw/qo0-csgo/blob/master/base/features/autowall.cpp
inline float decay(float damage, float distance, float rangeModifier) noexcept
{
    return static_cast<float>(double(damage) * std::pow(double(rangeModifier), double(distance) / 500.0));
}

template <typename Trace, typename CanPenetrate>
Optional<Impact> simulate(const cs2::Vector& start, const cs2::Vector& end, void* skip, void* target,
    Bullet bullet, Limits limits, TraceBudget& budget, Trace&& trace, CanPenetrate&& canPenetrate) noexcept
{
    using namespace hitbox_geometry;
    if (!finite(start) || !finite(end) || !bullet.valid() || !limits.valid())
        return {};
    const auto path = subtract(end, start);
    const float length = std::hypot(path.x, path.y, path.z);
    if (!std::isfinite(length) || length < 0.001f || length > bullet.maxRange)
        return {};
    const auto direction = scale(path, 1.0f / length);
    auto position = start;
    float traveled = 0.0f;
    float totalThickness = 0.0f;
    int penetrations = 0;
    constexpr float epsilon = 0.03125f;
    const auto validTrace = [](const auto& result) {
        return result.valid && std::isfinite(result.fraction) && result.fraction >= 0.0f && result.fraction <= 1.0f
            && result.didHit == (result.fraction < 1.0f) && finite(result.endPos) && finite(result.normal);
    };
    // Verify that an engine result actually lies on the requested ray. A corrupt
    // end position must never become a visible or penetrable target.
    const auto rayDistance = [&](const auto& point) -> Optional<float> {
        const auto delta = subtract(point, start);
        const float distance = dot(delta, direction);
        const auto perpendicular = subtract(delta, scale(direction, distance));
        if (!std::isfinite(distance) || distance < -epsilon || distance > length + epsilon
            || dot(perpendicular, perpendicular) > 0.25f)
            return {};
        return std::clamp(distance, 0.0f, length);
    };
    while (traveled < length) {
        if (!budget.take()) return {};
        const auto entry = trace(position, end, skip);
        if (!validTrace(entry)) return {};
        const auto entryDistance = rayDistance(entry.endPos);
        if (!entryDistance.hasValue() || entryDistance.value() + epsilon < traveled) return {};
        const float hitDistance = entry.didHit ? entryDistance.value() : length;
        if (!entry.didHit && std::abs(entryDistance.value() - length) > 0.5f) return {};
        // End position and fraction must describe the same trace, within rounding.
        const float fromDistance = dot(subtract(position, start), direction);
        if (std::abs(hitDistance - (fromDistance + (length - fromDistance) * entry.fraction)) > 0.5f)
            return {};
        bullet.damage = decay(bullet.damage, std::max(0.0f, hitDistance - traveled), bullet.rangeModifier);
        if (!std::isfinite(bullet.damage) || bullet.damage < 1.0f) return {};
        if (entry.reaches(target))
            return Impact{bullet.damage, hitDistance, totalThickness, penetrations};
        if (bullet.power <= 0.0f || penetrations >= limits.maxPenetrations
            || (entry.hitEntity && !canPenetrate(entry.hitEntity)))
            return {};
        const float remaining = length - hitDistance;
        const float maxProbe = std::min(limits.maxWallThickness + epsilon, remaining - epsilon);
        Optional<float> exitDistance;
        // Fine local probes find narrow exits without a target-to-eye reverse ray
        // spanning several walls. Never probe beyond the intended target point.
        const int probeCount = maxProbe > 0.0f ? static_cast<int>(std::ceil(maxProbe / 0.5f)) : 0;
        for (int probeIndex = 1; probeIndex <= probeCount; ++probeIndex) {
            const float step = std::min(float(probeIndex) * 0.5f, maxProbe);
            if (!budget.take()) return {};
            const auto probe = add(entry.endPos, scale(direction, step));
            const auto reverse = trace(probe, add(entry.endPos, scale(direction, -epsilon)), skip);
            if (!validTrace(reverse)) return {};
            if (!reverse.didHit || reverse.hitEntity != entry.hitEntity
                || dot(reverse.normal, direction) <= 0.01f)
                continue;
            const auto candidate = rayDistance(reverse.endPos);
            if (!candidate.hasValue()) return {};
            const float thickness = candidate.value() - hitDistance;
            if (thickness <= 0.0f || thickness > step + epsilon || thickness > limits.maxWallThickness)
                continue;
            // Check the reverse result's geometry as well as the forward hit.
            if (std::abs(thickness - (step - reverse.fraction * (step + epsilon))) > 0.5f)
                return {};
            exitDistance = candidate.value();
            break;
        }
        if (!exitDistance.hasValue()) return {};
        const float thickness = exitDistance.value() - hitDistance;
        totalThickness += thickness;
        if (totalThickness > limits.maxTotalThickness + 0.001f) return {};
        bullet.damage = decay(bullet.damage, thickness, bullet.rangeModifier);
        const double loss = double(bullet.damage) * 0.16 + 11.25 / bullet.power
            + double(thickness) * thickness / 24.0;
        bullet.damage = static_cast<float>(double(bullet.damage) - loss);
        if (!std::isfinite(bullet.damage) || bullet.damage < 1.0f) return {};
        ++penetrations;
        traveled = exitDistance.value();
        // A tiny nudge avoids re-hitting the same face. Charge its traveled
        // distance too; never skip the last part of a wall or target.
        const float nextDistance = std::min(length, traveled + epsilon);
        bullet.damage = decay(bullet.damage, nextDistance - traveled, bullet.rangeModifier);
        if (nextDistance >= length) return {};
        traveled = nextDistance;
        position = add(start, scale(direction, traveled));
    }
    return {};
}

// Compatibility entry for callers supplying already-decayed damage.
template <typename Trace>
Optional<float> estimate(const cs2::Vector& start, const cs2::Vector& end, void* skip, void* target,
    float damage, float power, Trace&& trace) noexcept
{
    if (power <= 0.0f) return {};
    TraceBudget budget;
    const auto impact = simulate(start, end, skip, target, {damage, power}, {}, budget, trace,
        [](void*) { return false; });
    return impact.hasValue() ? Optional<float>{impact.value().damage} : Optional<float>{};
}
}
