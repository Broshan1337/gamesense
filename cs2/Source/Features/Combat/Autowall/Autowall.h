#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <GameClient/Tracing/Tracing.h>
#include <Utils/Optional.h>















class Autowall {
public:
    
    
    
    [[nodiscard]] static Optional<float> penetratedDamage(const cs2::Vector& start,
                                                          const cs2::Vector& endPoint,
                                                          void* skipEntity, void* targetEntity,
                                                          float damageAtPoint,
                                                          float penetrationPower) noexcept
    {
        if (penetrationPower <= 0.0f)
            return {};

        const float dx = endPoint.x - start.x;
        const float dy = endPoint.y - start.y;
        const float dz = endPoint.z - start.z;
        const float len = squareRoot(dx * dx + dy * dy + dz * dz);
        if (len < 1.0f)
            return {};
        const cs2::Vector dir{dx / len, dy / len, dz / len};

        float damage = damageAtPoint;
        cs2::Vector pos = start;
        for (int layer = 0; layer < kMaxLayers; ++layer) {
            
            
            const auto fwd = Tracing::traceLine(pos, endPoint, skipEntity, kBulletMask);
            if (!fwd.valid)
                return {};
            if (fwd.reaches(targetEntity))
                return damage;                       
            if (fwd.hitEntity != nullptr)
                return {};                           

            
            
            const auto rev = Tracing::traceLine(endPoint, pos, skipEntity, kBulletMask);
            if (!rev.valid || !rev.didHit || rev.hitEntity != nullptr)
                return {};

            const float wx = fwd.endPos.x - rev.endPos.x;
            const float wy = fwd.endPos.y - rev.endPos.y;
            const float wz = fwd.endPos.z - rev.endPos.z;
            const float thickness = squareRoot(wx * wx + wy * wy + wz * wz);

            const float ffPen = ffBulletPenetration();
            damage -= maxPositive(3.75f / penetrationPower) * (3.0f / ffPen)
                    + 0.16f * damage
                    + thickness * thickness / (24.0f * ffPen);
            if (damage < 1.0f)
                return {};

            
            pos = offset(rev.endPos, dir, kExitEpsilon);
        }
        return {};                                   
    }

    
    
    
    
    
    [[nodiscard]] static float ffBulletPenetration() noexcept
    {
        const auto& anchors = tracing_sigs::resolved();
        if (!anchors.ffOk)
            return kDefaultFfPenetration;

        using GetValuePtrFn = const float* (*)(const void* cvar, int splitSlot);
        const auto fn = reinterpret_cast<GetValuePtrFn>(anchors.ffGetter);
        const auto* valuePtr = fn(reinterpret_cast<const void*>(anchors.ffObject), -1);
        if (!valuePtr)
            return kDefaultFfPenetration;
        const auto value = *valuePtr;
        if (!(value > 0.1f))                         
            return kDefaultFfPenetration;
        return value;
    }

private:
    static constexpr int kMaxLayers = 4;             
    static constexpr float kExitEpsilon = 2.0f;      
    static constexpr float kDefaultFfPenetration = 1.0f;

    static constexpr std::uint64_t kBulletMask = 0x1C300B;               

    [[nodiscard]] static float maxPositive(float v) noexcept
    {
        return v > 0.0f ? v : 0.0f;
    }

    [[nodiscard]] static float squareRoot(float v) noexcept
    {
        
        if (v <= 0.0f)
            return 0.0f;
        float guess = v * 0.5f + 0.5f;
        for (int i = 0; i < 24; ++i)
            guess = 0.5f * (guess + v / guess);
        return guess;
    }

    [[nodiscard]] static cs2::Vector offset(const cs2::Vector& v, const cs2::Vector& dir, float dist) noexcept
    {
        return cs2::Vector{v.x + dir.x * dist, v.y + dir.y * dist, v.z + dir.z * dist};
    }

};
