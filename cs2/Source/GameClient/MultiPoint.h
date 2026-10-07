#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <Features/Combat/ShotGeometry.h>
#include <GameClient/Hitboxes.h>
#include <Utils/Trig.h>








class MultiPoint {
public:
    static constexpr int kMaxPoints = 12;

    struct Point {
        cs2::Vector position;
        bool isCenter;
    };

    [[nodiscard]] static cs2::Vector rotateVector(const float* q, const cs2::Vector& v) noexcept
    {
        
        const float tx = 2.0f * (q[1] * v.z - q[2] * v.y);
        const float ty = 2.0f * (q[2] * v.x - q[0] * v.z);
        const float tz = 2.0f * (q[0] * v.y - q[1] * v.x);
        return cs2::Vector{
            v.x + q[3] * tx + (q[1] * tz - q[2] * ty),
            v.y + q[3] * ty + (q[2] * tx - q[0] * tz),
            v.z + q[3] * tz + (q[0] * ty - q[1] * tx),
        };
    }

    
    
    
    static int generate(const Hitboxes::Entry& hb, const cs2::Vector& center, const float* boneRotation,
                        float pointScalePercent, const cs2::Vector& shootPos, float inaccuracy,
                        bool dynamicPointscale, Point* out) noexcept
    {
        int count = 0;
        const auto push = [&](const cs2::Vector& p) { out[count].position = p; out[count].isCenter = false; ++count; };

        const auto scale = pointScalePercent < 0.0f ? 0.0f : (pointScalePercent > 100.0f ? 100.0f : pointScalePercent) / 100.0f;
        if (scale <= 0.01f) {
            out[count++] = {center, false};
            return count;
        }

        const auto radius = hb.radius > 0.0f ? hb.radius : 2.0f;
        const auto sr = radius * scale;
        if (sr < 0.1f) {
            out[count++] = {center, false};
            return count;
        }

        const auto hbMidX = (hb.mins.x + hb.maxs.x) * 0.5f;
        const auto hbMidY = (hb.mins.y + hb.maxs.y) * 0.5f;
        const auto hbMidZ = (hb.mins.z + hb.maxs.z) * 0.5f;
        const cs2::Vector localMins{hb.mins.x - hbMidX, hb.mins.y - hbMidY, hb.mins.z - hbMidZ};
        const cs2::Vector localMaxs{hb.maxs.x - hbMidX, hb.maxs.y - hbMidY, hb.maxs.z - hbMidZ};
        const auto rotatedMins = rotateVector(boneRotation, localMins);
        const auto rotatedMaxs = rotateVector(boneRotation, localMaxs);
        const cs2::Vector capsuleA{center.x + rotatedMins.x, center.y + rotatedMins.y, center.z + rotatedMins.z};
        const cs2::Vector capsuleB{center.x + rotatedMaxs.x, center.y + rotatedMaxs.y, center.z + rotatedMaxs.z};

        
        const auto safePoint = [&](const cs2::Vector& point, float inward) noexcept {
            const auto abx = capsuleB.x - capsuleA.x;
            const auto aby = capsuleB.y - capsuleA.y;
            const auto abz = capsuleB.z - capsuleA.z;
            const auto abSq = abx * abx + aby * aby + abz * abz;
            const auto tRaw = abSq < 1.0e-8f ? 0.0f : ((point.x - capsuleA.x) * abx + (point.y - capsuleA.y) * aby + (point.z - capsuleA.z) * abz) / abSq;
            const auto t = tRaw < 0.0f ? 0.0f : (tRaw > 1.0f ? 1.0f : tRaw);
            const cs2::Vector closest{capsuleA.x + abx * t, capsuleA.y + aby * t, capsuleA.z + abz * t};
            const auto dx = point.x - closest.x;
            const auto dy = point.y - closest.y;
            const auto dz = point.z - closest.z;
            const auto dist = trig::squareRoot(dx * dx + dy * dy + dz * dz);
            const auto maxD = radius * inward;
            if (dist <= maxD)
                return point;
            if (dist < 1.0e-6f)
                return closest;
            const auto f = maxD / dist;
            return cs2::Vector{closest.x + dx * f, closest.y + dy * f, closest.z + dz * f};
        };

        
        
        const auto shootDir = shot_geometry::normalized(cs2::Vector{center.x - shootPos.x, center.y - shootPos.y, center.z - shootPos.z});
        const auto angles = shot_geometry::anglesTo(shootPos, center);
        const auto basis = shot_geometry::angleVectors(angles.pitch, angles.yaw);
        const auto right = cs2::Vector{-basis.left.x, -basis.left.y, -basis.left.z};

        switch (hb.index) {
        case 0: { 
            const auto topCap = (capsuleA.z > capsuleB.z) ? capsuleA : capsuleB;
            out[count++] = {safePoint(cs2::Vector{topCap.x + basis.up.x * sr, topCap.y + basis.up.y * sr, topCap.z + basis.up.z * sr}, 0.85f), false};
            out[count++] = {safePoint(cs2::Vector{topCap.x - basis.forward.x * sr, topCap.y - basis.forward.y * sr, topCap.z - basis.forward.z * sr}, 0.85f), false};
            out[count++] = {safePoint(cs2::Vector{topCap.x + (basis.up.x - basis.forward.x) * sr, topCap.y + (basis.up.y - basis.forward.y) * sr, topCap.z + (basis.up.z - basis.forward.z) * sr}, 0.85f), false};
            out[count++] = {safePoint(cs2::Vector{center.x + right.x * sr, center.y + right.y * sr, center.z + right.z * sr}, 0.85f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - right.x * sr, center.y - right.y * sr, center.z - right.z * sr}, 0.85f), false};
            for (int i = 1; i < 4; ++i) {
                const auto t = static_cast<float>(i) / 3.0f;
                const auto arcRx = basis.up.x * (1.0f - t) + right.x * t;
                const auto arcRy = basis.up.y * (1.0f - t) + right.y * t;
                const auto arcRz = basis.up.z * (1.0f - t) + right.z * t;
                const auto arcLx = basis.up.x * (1.0f - t) - right.x * t;
                const auto arcLy = basis.up.y * (1.0f - t) - right.y * t;
                const auto arcLz = basis.up.z * (1.0f - t) - right.z * t;
                const auto rLen = trig::squareRoot(arcRx * arcRx + arcRy * arcRy + arcRz * arcRz);
                const auto lLen = trig::squareRoot(arcLx * arcLx + arcLy * arcLy + arcLz * arcLz);
                out[count++] = {safePoint(cs2::Vector{topCap.x + (arcRx / rLen) * sr * 0.9f, topCap.y + (arcRy / rLen) * sr * 0.9f, topCap.z + (arcRz / rLen) * sr * 0.9f}, 0.85f), false};
                out[count++] = {safePoint(cs2::Vector{topCap.x + (arcLx / lLen) * sr * 0.9f, topCap.y + (arcLy / lLen) * sr * 0.9f, topCap.z + (arcLz / lLen) * sr * 0.9f}, 0.85f), false};
            }
            break;
        }
        case 2:
        case 3: { 
            out[count++] = {safePoint(cs2::Vector{center.x + right.x * sr, center.y + right.y * sr, center.z + right.z * sr}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - right.x * sr, center.y - right.y * sr, center.z - right.z * sr}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x + basis.up.x * sr * 0.7f, center.y + basis.up.y * sr * 0.7f, center.z + basis.up.z * sr * 0.7f}, 0.9f), false};
            out[count++] = {safePoint(capsuleA, 0.9f), false};
            out[count++] = {safePoint(capsuleB, 0.9f), false};
            break;
        }
        case 4:
        case 5:
        case 6: { 
            out[count++] = {safePoint(cs2::Vector{center.x + right.x * sr, center.y + right.y * sr, center.z + right.z * sr}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - right.x * sr, center.y - right.y * sr, center.z - right.z * sr}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x + basis.up.x * sr * 0.65f, center.y + basis.up.y * sr * 0.65f, center.z + basis.up.z * sr * 0.65f}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - basis.up.x * sr * 0.5f, center.y - basis.up.y * sr * 0.5f, center.z - basis.up.z * sr * 0.5f}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x + (right.x + basis.up.x) * sr * 0.75f, center.y + (right.y + basis.up.y) * sr * 0.75f, center.z + (right.z + basis.up.z) * sr * 0.75f}, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - (right.x + basis.up.x) * sr * 0.75f, center.y - (right.y + basis.up.y) * sr * 0.75f, center.z - (right.z + basis.up.z) * sr * 0.75f}, 0.9f), false};
            break;
        }
        case 7: case 8: case 9: case 10: case 11: case 12: { 
            out[count++] = {safePoint(capsuleA, 0.9f), false};
            out[count++] = {safePoint(capsuleB, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x + right.x * sr, center.y + right.y * sr, center.z + right.z * sr}, 0.85f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - right.x * sr, center.y - right.y * sr, center.z - right.z * sr}, 0.85f), false};
            break;
        }
        case 13: case 14: case 15: case 16: case 17: case 18: { 
            out[count++] = {safePoint(capsuleA, 0.9f), false};
            out[count++] = {safePoint(capsuleB, 0.9f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - basis.forward.x * sr * 0.7f, center.y - basis.forward.y * sr * 0.7f, center.z - basis.forward.z * sr * 0.7f}, 0.9f), false};
            break;
        }
        default: {
            out[count++] = {safePoint(cs2::Vector{center.x + right.x * sr, center.y + right.y * sr, center.z + right.z * sr}, 0.85f), false};
            out[count++] = {safePoint(cs2::Vector{center.x - right.x * sr, center.y - right.y * sr, center.z - right.z * sr}, 0.85f), false};
            break;
        }
        }

        
        
        
        if (dynamicPointscale && inaccuracy > 0.001f && hb.radius > 0.0f) {
            const auto dist = trig::squareRoot((center.x - shootPos.x) * (center.x - shootPos.x) + (center.y - shootPos.y) * (center.y - shootPos.y) + (center.z - shootPos.z) * (center.z - shootPos.z));
            if (dist > 0.0f) {
                for (int i = 0; i < count; ++i) {
                    const auto dx = out[i].position.x - center.x;
                    const auto dy = out[i].position.y - center.y;
                    const auto dz = out[i].position.z - center.z;
                    const auto deltaLen = trig::squareRoot(dx * dx + dy * dy + dz * dz);
                    const auto effectiveR = radius * 2.0f - deltaLen;
                    auto probability = (effectiveR / (inaccuracy * dist));
                    probability *= probability;
                    if (probability > 1.0f)
                        probability = 1.0f;
                    out[i].position = cs2::Vector{center.x + dx * probability, center.y + dy * probability, center.z + dz * probability};
                }
            }
        }
        return count;
    }
};
