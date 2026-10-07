#pragma once

#include <Features/Combat/Autowall/Penetration.h>
#include <GameClient/Tracing/Tracing.h>

class Autowall {
public:
    // Existing Lua API supplies damage-at-point and power, without weapon range
    // metadata. Preserve that contract using the same bounded neutral model.
    static Optional<float> penetratedDamage(const cs2::Vector& start, const cs2::Vector& end,
        void* skip, void* target, float damage, float power) noexcept
    {
        return penetration::estimate(start, end, skip, target, damage, power,
            [](const auto& from, const auto& to, void* entity) {
                return Tracing::traceLine(from, to, entity, kBulletMask);
            });
    }

    template <typename CanPenetrate>
    static Optional<penetration::Impact> evaluate(const cs2::Vector& start, const cs2::Vector& end,
        void* skip, void* target, penetration::Bullet bullet, penetration::Limits limits,
        penetration::TraceBudget& budget, CanPenetrate&& canPenetrate) noexcept
    {
        return penetration::simulate(start, end, skip, target, bullet, limits, budget,
            [](const auto& from, const auto& to, void* entity) {
                return Tracing::traceLine(from, to, entity, kBulletMask);
            }, canPenetrate);
    }

    static constexpr std::uint64_t kBulletMask = 0x1C300B;
};
