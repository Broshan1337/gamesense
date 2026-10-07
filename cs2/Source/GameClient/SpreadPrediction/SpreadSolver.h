#pragma once

#include <cmath>
#include <cstdint>
#include <CS2/Classes/Vector.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <GameClient/SpreadPrediction/SpreadModel.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <Utils/Optional.h>

template <typename HookContext>
class SpreadSolver {
public:
    explicit SpreadSolver(HookContext& context) noexcept : hookContext{context} {}

    struct WeaponSpreadParams {
        std::int16_t itemDefinitionIndex;
        int numBullets;
        float inaccuracy;
        float spread;
        float recoilIndex;
    };
    struct Angles { float pitch, yaw, roll; };

    static bool valid(const WeaponSpreadParams& params) noexcept
    {
        return params.itemDefinitionIndex > 0 && params.numBullets > 0 && params.numBullets <= 32
            && spread_model::valid(params.inaccuracy, params.spread)
            && std::isfinite(params.recoilIndex) && params.recoilIndex >= 0.0f;
    }

    Optional<WeaponSpreadParams> weaponParams(auto&& weapon) const noexcept
    {
        weapon.updateAccuracyPenalty();
        return parameters(weapon, weapon.inaccuracy());
    }

    Optional<WeaponSpreadParams> weaponParamsAtVelocity(auto&& weapon, cs2::C_BaseEntity* pawn,
                                                       const cs2::Vector& velocity) const noexcept
    {
        weapon.updateAccuracyPenalty();
        return parameters(weapon, weapon.inaccuracyAtVelocity(pawn, velocity));
    }

    Optional<std::uint32_t> seed(const Angles& angles, int tick) const noexcept
    {
        if (tick <= 0 || !std::isfinite(angles.pitch) || !std::isfinite(angles.yaw) || !std::isfinite(angles.roll))
            return {};
        const auto fn = hookContext.patternSearchResults().template get<PointerToSpreadSeedFunction>();
        if (!fn)
            return {};
        const cs2::Vector vector{angles.pitch, angles.yaw, angles.roll};
        return fn(nullptr, &vector, tick);
    }

    // CalculateSpread changed ABI on 2026-09-27. The legacy nine-argument
    // binding must NEVER be called. Unknown spread is not a zero vector.
    Optional<cs2::Vector> spreadOffset(std::uint32_t, const WeaponSpreadParams& params) const noexcept
    {
        if (valid(params) && params.inaccuracy + params.spread == 0.0f)
            return cs2::Vector{};
        return {};
    }

    static Optional<cs2::Vector> estimatedSpreadOffset(std::uint32_t sample, const WeaponSpreadParams& params) noexcept
    {
        if (!valid(params) || params.numBullets != 1)
            return {}; // Pellet patterns require a separate model.
        return spread_model::sample(sample, params.inaccuracy, params.spread);
    }

    Optional<Angles> findSpreadCorrection(const Angles& aim, int tick, const WeaponSpreadParams& params) const noexcept
    {
        if (tick <= 0 || !valid(params) || !std::isfinite(aim.pitch) || !std::isfinite(aim.yaw)
            || aim.pitch < -89.0f || aim.pitch > 89.0f)
            return {};
        if (params.inaccuracy + params.spread == 0.0f)
            return Angles{aim.pitch, aim.yaw, 0.0f};
        return {}; // An estimate is never accepted as an exact correction.
    }

private:
    Optional<WeaponSpreadParams> parameters(auto&& weapon, const Optional<float>& inacc) const noexcept
    {
        const auto item = weapon.itemDefinitionIndex();
        const auto bullets = weapon.numBullets();
        const auto spread = weapon.spread();
        const auto recoil = weapon.recoilIndex();
        if (!item.hasValue() || !bullets.hasValue() || !inacc.hasValue() || !spread.hasValue() || !recoil.hasValue())
            return {};
        const WeaponSpreadParams result{static_cast<std::int16_t>(item.value()), bullets.value(),
                                       inacc.value(), spread.value(), recoil.value()};
        return valid(result) ? Optional<WeaponSpreadParams>{result} : Optional<WeaponSpreadParams>{};
    }
    HookContext& hookContext;
};
