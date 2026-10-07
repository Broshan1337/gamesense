#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <CS2/Classes/Vector.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>



















template <typename HookContext>
class SpreadSolver {
public:
    explicit SpreadSolver(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    struct WeaponSpreadParams {
        std::int16_t itemDefinitionIndex;
        int numBullets;
        float inaccuracy;
        float spread;
        float recoilIndex;
    };

    
    
    struct Angles {
        float pitch;
        float yaw;
        float roll;
    };

    
    
    
    [[nodiscard]] Optional<WeaponSpreadParams> weaponParams(auto&& weapon) const noexcept
    {
        
        
        
        weapon.updateAccuracyPenalty();

        const auto itemDef = weapon.itemDefinitionIndex();
        const auto bullets = weapon.numBullets();
        const auto inacc = weapon.inaccuracy();
        const auto spr = weapon.spread();
        const auto recoil = weapon.recoilIndex();
        if (!itemDef.hasValue() || !bullets.hasValue() || !inacc.hasValue() || !spr.hasValue() || !recoil.hasValue())
            return {};
        return WeaponSpreadParams{
            static_cast<std::int16_t>(itemDef.value()),
            bullets.value(),
            inacc.value(),
            spr.value(),
            recoil.value(),
        };
    }

    
    
    
    
    
    
    [[nodiscard]] Optional<WeaponSpreadParams> weaponParamsAtVelocity(auto&& weapon, cs2::C_BaseEntity* pawn, const cs2::Vector& velocity) const noexcept
    {
        
        
        weapon.updateAccuracyPenalty();

        const auto itemDef = weapon.itemDefinitionIndex();
        const auto bullets = weapon.numBullets();
        const auto spr = weapon.spread();
        const auto recoil = weapon.recoilIndex();
        const auto inacc = weapon.inaccuracyAtVelocity(pawn, velocity);
        if (!itemDef.hasValue() || !bullets.hasValue() || !inacc.hasValue() || !spr.hasValue() || !recoil.hasValue())
            return {};
        return WeaponSpreadParams{
            static_cast<std::int16_t>(itemDef.value()),
            bullets.value(),
            inacc.value(),
            spr.value(),
            recoil.value(),
        };
    }

    
    
    
    [[nodiscard]] Optional<std::uint32_t> seed(const Angles& angles, int tick) const noexcept
    {
        const auto seedFn = hookContext.patternSearchResults().template get<PointerToSpreadSeedFunction>();
        if (!seedFn)
            return {};
        const cs2::Vector asVector{angles.pitch, angles.yaw, angles.roll};
        return seedFn(nullptr, &asVector, tick);
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] cs2::Vector spreadOffset(std::uint32_t seed, const WeaponSpreadParams& params) const noexcept
    {
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        return cs2::Vector{};
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] Optional<Angles> findSpreadCorrection(const Angles& aim, int tick, const WeaponSpreadParams& params) const noexcept
    {
        
        
        
        
        
        
        if (params.inaccuracy + params.spread < kNegligibleCone)
            return Angles{aim.pitch, aim.yaw, 0.0f};

        const auto targetDir = forwardVector(aim.pitch, aim.yaw);

        
        
        
        
        const auto maxDeflectionDeg = trig::arcTangent2(params.inaccuracy + params.spread, 1.0f) * trig::kRadiansToDegrees;
        const auto spanDeg = (maxDeflectionDeg + kMarginDegrees > kMaxBucketsPerAxis * kBucketDegrees * 0.5f)
            ? kMaxBucketsPerAxis * kBucketDegrees * 0.5f
            : maxDeflectionDeg + kMarginDegrees;
        const int pitchMin = static_cast<int>(floorF((aim.pitch - spanDeg) / kBucketDegrees));
        const int pitchMax = static_cast<int>(ceilF((aim.pitch + spanDeg) / kBucketDegrees));
        const int yawMin = static_cast<int>(floorF((aim.yaw - spanDeg) / kBucketDegrees));
        const int yawMax = static_cast<int>(ceilF((aim.yaw + spanDeg) / kBucketDegrees));

        bool found{};
        float bestResidual = kAcceptResidualDegrees;
        Angles best{};
        
        float bestPitch{}, bestYaw{}, bestOffsetX{}, bestOffsetY{}, bestLength{}, bestChord{};

        for (int j = yawMin; j <= yawMax; ++j) {
            const auto writtenYaw = static_cast<float>(j) * kBucketDegrees;
            for (int i = pitchMin; i <= pitchMax; ++i) {
                const auto writtenPitch = static_cast<float>(i) * kBucketDegrees;

                const auto candidateSeed = seed(Angles{writtenPitch, writtenYaw, 0.0f}, tick);
                if (!candidateSeed.hasValue())
                    return {};

                const auto offset = spreadOffset(candidateSeed.value(), params);
                const auto length = trig::squareRoot(offset.x * offset.x + offset.y * offset.y);
                if (length < kMinOffsetLength)
                    continue;

                
                
                
                const auto fw = forwardVector(writtenPitch, writtenYaw);
                const cs2::Vector e{targetDir.x - fw.x, targetDir.y - fw.y, targetDir.z - fw.z};
                const auto chord = trig::squareRoot(e.x * e.x + e.y * e.y + e.z * e.z);
                const auto distanceDeg = trig::arcTangent2(chord, 1.0f) * trig::kRadiansToDegrees;
                const auto lengthDeg = trig::arcTangent2(length, 1.0f) * trig::kRadiansToDegrees;

                const auto residual = lengthDeg > distanceDeg ? lengthDeg - distanceDeg : distanceDeg - lengthDeg;
                if (residual >= bestResidual)
                    continue;

                
                
                
                
                
                const auto pitchRad = writtenPitch * trig::kDegreesToRadians;
                const auto yawRad = writtenYaw * trig::kDegreesToRadians;
                const auto sp = trig::sine(pitchRad), cp = trig::cosine(pitchRad);
                const auto sy = trig::sine(yawRad), cy = trig::cosine(yawRad);
                const auto eUp = e.x * (sp * cy) + e.y * (sp * sy) + e.z * cp;
                const auto eRight = e.x * sy - e.y * cy;
                const auto rollDeg = (trig::arcTangent2(offset.y, offset.x) - trig::arcTangent2(eUp, eRight)) * trig::kRadiansToDegrees;

                found = true;
                bestResidual = residual;
                best = Angles{writtenPitch, writtenYaw, normalizeRoll(rollDeg)};
                bestPitch = writtenPitch;
                bestYaw = writtenYaw;
                bestOffsetX = offset.x;
                bestOffsetY = offset.y;
                bestLength = length;
                bestChord = chord;
                if (residual <= kExactResidualDegrees)
                    return best; 
            }
        }

        if (!found)
            return {};

        
        
        
        
        
        
        
        const auto rho0 = trig::arcCosine(1.0f - bestChord * bestChord * 0.5f); 
        const auto slide = rho0 - trig::arcCosine(1.0f - bestLength * bestLength * 0.5f);
        const auto sinRho = trig::sine(rho0);
        if (trig::absolute(slide) > kMaxSlideDegrees * trig::kDegreesToRadians || sinRho < 1e-6f)
            return best;

        const auto cHat = forwardVector(bestPitch, bestYaw);
        const auto cosRho = trig::cosine(rho0);
        const auto dX = targetDir.x - cHat.x * cosRho;
        const auto dY = targetDir.y - cHat.y * cosRho;
        const auto dZ = targetDir.z - cHat.z * cosRho;
        const auto nLen = trig::squareRoot(dX * dX + dY * dY + dZ * dZ);
        if (nLen < kMinOffsetLength)
            return best;
        const cs2::Vector nHat{dX / nLen, dY / nLen, dZ / nLen};
        const cs2::Vector newDir{cHat.x * trig::cosine(slide) + nHat.x * trig::sine(slide),
                                 cHat.y * trig::cosine(slide) + nHat.y * trig::sine(slide),
                                 cHat.z * trig::cosine(slide) + nHat.z * trig::sine(slide)};

        const auto refined = dirToAngles(newDir);
        
        const auto bucketOf = [](float angle) { return static_cast<int>(floorF(angle / kBucketDegrees + 0.5f)); };
        if (bucketOf(refined.pitch) != bucketOf(bestPitch)
            || bucketOf(refined.yaw) != bucketOf(bestYaw))
            return best;

        const auto refFw = forwardVector(refined.pitch, refined.yaw);
        const cs2::Vector refE{targetDir.x - refFw.x, targetDir.y - refFw.y, targetDir.z - refFw.z};
        const auto refChord = trig::squareRoot(refE.x * refE.x + refE.y * refE.y + refE.z * refE.z);
        const auto refResidual = bestLength > refChord ? bestLength - refChord : refChord - bestLength;
        if (refResidual >= bestResidual)
            return best; 

        const auto pitchRad = refined.pitch * trig::kDegreesToRadians;
        const auto yawRad = refined.yaw * trig::kDegreesToRadians;
        const auto sp = trig::sine(pitchRad), cp = trig::cosine(pitchRad);
        const auto sy = trig::sine(yawRad), cy = trig::cosine(yawRad);
        const auto eUp = refE.x * (sp * cy) + refE.y * (sp * sy) + refE.z * cp;
        const auto eRight = refE.x * sy - refE.y * cy;
        const auto rollDeg = (trig::arcTangent2(bestOffsetY, bestOffsetX) - trig::arcTangent2(eUp, eRight)) * trig::kRadiansToDegrees;
        return Angles{refined.pitch, refined.yaw, normalizeRoll(rollDeg)};
    }

private:
    
    
    
    static constexpr int kMaxNumBullets = 32;
    static constexpr float kMaxRecoilIndex = 100.0f;
    
    
    
    static constexpr int kMaxSpreadSteps = 512;

    
    
    static constexpr float kBucketDegrees = 0.5f;
    
    static constexpr float kMarginDegrees = 1.0f;
    
    
    
    static constexpr int kMaxBucketsPerAxis = 64;
    
    
    
    static constexpr float kAcceptResidualDegrees = 0.25f;
    
    static constexpr float kExactResidualDegrees = 0.02f;
    
    
    
    static constexpr float kMaxSlideDegrees = 0.33f;
    
    static constexpr float kMinOffsetLength = 1e-5f;
    
    
    
    static constexpr float kNegligibleCone = 1e-4f;

    
    
    [[nodiscard]] static cs2::Vector forwardVector(float pitchDeg, float yawDeg) noexcept
    {
        const auto pitchRad = pitchDeg * trig::kDegreesToRadians;
        const auto yawRad = yawDeg * trig::kDegreesToRadians;
        const auto sp = trig::sine(pitchRad), cp = trig::cosine(pitchRad);
        const auto sy = trig::sine(yawRad), cy = trig::cosine(yawRad);
        return cs2::Vector{cp * cy, cp * sy, -sp};
    }

    
    [[nodiscard]] static Angles dirToAngles(const cs2::Vector& dir) noexcept
    {
        const auto horizontal = trig::squareRoot(dir.x * dir.x + dir.y * dir.y);
        return Angles{
            trig::arcTangent2(-dir.z, horizontal) * trig::kRadiansToDegrees,
            trig::arcTangent2(dir.y, dir.x) * trig::kRadiansToDegrees,
            0.0f};
    }

    
    
    [[nodiscard]] static constexpr float floorF(float value) noexcept
    {
        const auto truncated = static_cast<int>(value);
        return (value >= 0.0f || static_cast<float>(truncated) == value) ? static_cast<float>(truncated) : static_cast<float>(truncated - 1);
    }

    [[nodiscard]] static constexpr float ceilF(float value) noexcept
    {
        return -floorF(-value);
    }

    [[nodiscard]] static float normalizeRoll(float rollDeg) noexcept
    {
        auto roll = rollDeg;
        while (roll > 180.0f)
            roll -= 360.0f;
        while (roll < -180.0f)
            roll += 360.0f;
        return roll;
    }

    HookContext& hookContext;
};
