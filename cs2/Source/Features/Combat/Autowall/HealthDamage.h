#pragma once
#include <algorithm>
#include <cmath>
#include <Utils/Optional.h>

namespace penetration {
// Normal CS2 player armor. Penetration returns unscaled damage; apply this once
// using the hitgroup actually struck, after all material and range losses.
inline Optional<float> healthDamage(float damage, int group, int armor, bool helmet,
    float armorRatio, float headMultiplier) noexcept
{
    if (!std::isfinite(damage) || damage < 0 || group < 1 || group > 8 || armor < 0
        || !std::isfinite(armorRatio) || armorRatio < 0
        || !std::isfinite(headMultiplier) || headMultiplier <= 0) return {};
    switch (group) {
    case 1: damage *= headMultiplier; break;
    case 3: damage *= 1.25f; break;
    case 6: case 7: damage *= .75f; break;
    default: break;
    }
    if (armor > 0 && ((group >= 2 && group <= 5) || group == 8 || (group == 1 && helmet))) {
        const float reduced = damage * std::clamp(armorRatio * .5f, 0.0f, 1.0f);
        damage = (damage - reduced) * .5f > armor ? damage - armor / .5f : reduced;
    }
    if (!std::isfinite(damage)) return {};
    return std::floor(std::max(0.0f, damage));
}
}
