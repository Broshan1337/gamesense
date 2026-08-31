#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <GameClient/Tracing/Tracing.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/Optional.h>
#include <Utils/StatusReport.h>

// Velocity-parity bullet penetration for the rage aimbot: given a shot ray and the weapon's inputs,
// returns the damage that survives all wall layers between start and endPoint, or {} if the shot
// cannot reach it (another player in the way / too much accumulated loss / degenerate geometry).
//
// The per-layer loss math is transcribed from the game's own FireBullets penetration code
// (libclient sub_14FDBA0; full RE trail in AUTOWALL_RE_NOTES.md):
//     loss = max(0, 3.75 / penPower) * (3.0 / ff_pen)      ff_pen = ff_damage_bullet_penetration
//          + 0.16 * currentDamage
//          + thickness^2 / (24 * ff_pen)
//     dead when damage drops below 1; at most 4 layers (the game's default penetration count).
// Geometry uses our own verified TraceShape wrapper: forward trace finds each wall's entry face,
// a reversed trace from the target side gives the exit face. With more than one parallel wall the
// first iteration spans them all (conservative - may reject some multi-layer shots, never accepts
// a shot that would not penetrate).
class Autowall {
public:
    // `damageAtPoint` = unarmored, range-falloff-applied damage at the impact point (the caller's
    // estimate); armor/hitgroup scaling stays with the caller so this stays context-free.
    // Returns {} when the point is not reachable through walls.
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
            // The game's FireBullets mask (0x1C300B @ 0x152A0ED) - includes CONTENTS_GRATE so grates
            // are traced and their near-zero thickness handled by the loss formula, like the server.
            const auto fwd = Tracing::traceLine(pos, endPoint, skipEntity, kBulletMask);
            if (!fwd.didHit || fwd.hitEntity == targetEntity)
                return damage;                       // reached the target (this segment is clear)
            if (fwd.hitEntity != nullptr)
                return {};                           // another player blocks - never penetrable

            // Reversed trace from the target side: its first world hit is the exit face of the
            // nearest-to-target wall along the remaining path.
            const auto rev = Tracing::traceLine(endPoint, pos, skipEntity, kBulletMask);
            if (!rev.didHit || rev.hitEntity != nullptr)
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

            // Continue from just past the exit face (epsilon along the shot direction).
            pos = offset(rev.endPos, dir, kExitEpsilon);
        }
        return {};                                   // too many layers
    }

    // Live value of ff_damage_bullet_penetration, read exactly the way the game's own readers do
    // it: a value-slot helper (re-derived 0x20F3E10 for the 2026-08-26 build) returns a POINTER
    // to the current float, or null when the cvar is unknown; -1 means "local split". The caller
    // then dereferences and falls back to a static default on null - same shape as the reader
    // cluster at 0x150C3xx in the new binary.
    [[nodiscard]] static float ffBulletPenetration() noexcept
    {
        using GetValuePtrFn = const float* (*)(const void* cvar, int splitSlot);
        const auto base = clientBase();
        if (!base)
            return kDefaultFfPenetration;

        static std::atomic<int> state{0};   // 0 unknown, 1 ok, 2 failed
        auto cached = state.load(std::memory_order_acquire);
        if (cached == 0) {
            constexpr std::array<std::uint8_t, 13> kGetterSignature{
                0x55, 0x48, 0x89, 0xE5, 0x41, 0x54, 0x53, 0x48, 0x8B, 0x57, 0x08, 0x89, 0xF3};
            const auto getter = base + kConVarValuePointerOffset;
            const bool intact = std::memcmp(reinterpret_cast<const void*>(getter), kGetterSignature.data(), kGetterSignature.size()) == 0
                && kFfPenetrationCvarOffset < Tracing::kMaxModuleExtent;
            if (!intact) {
                StatusReport::record("Autowall: convar getter signature drifted - ffBulletPenetration defaults", false);
                state.store(2, std::memory_order_release);
                return kDefaultFfPenetration;
            }
            state.store(1, std::memory_order_release);
            StatusReport::record("Autowall", true);
            cached = 1;
        }
        if (cached == 2)
            return kDefaultFfPenetration;

        const auto fn = reinterpret_cast<GetValuePtrFn>(base + kConVarValuePointerOffset);
        const auto* valuePtr = fn(reinterpret_cast<const void*>(base + kFfPenetrationCvarOffset), -1);
        if (!valuePtr)
            return kDefaultFfPenetration;
        const auto value = *valuePtr;
        if (!(value > 0.1f))                         // matches the game's own tiny-value branch guard
            return kDefaultFfPenetration;
        return value;
    }

private:
    static constexpr int kMaxLayers = 4;             // the game's default penetration count
    static constexpr float kExitEpsilon = 2.0f;      // step past the exit face before the next segment
    static constexpr float kDefaultFfPenetration = 1.0f;

    static constexpr std::uintptr_t kConVarValuePointerOffset = 0x20F3E10; // const float*(const void* cvar, int splitSlot)
    static constexpr std::uintptr_t kFfPenetrationCvarOffset = 0x47E9000;  // ff_damage_bullet_penetration
    static constexpr std::uint64_t kBulletMask = 0x1C300B;               // FireBullets mask (incl. CONTENTS_GRATE)

    [[nodiscard]] static float maxPositive(float v) noexcept
    {
        return v > 0.0f ? v : 0.0f;
    }

    [[nodiscard]] static float squareRoot(float v) noexcept
    {
        // Newton iterations (no libm in this project), same idea as Utils/Trig.h.
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

    [[nodiscard]] static std::uintptr_t clientBase() noexcept
    {
        const DynamicLibrary client{cs2::CLIENT_DLL};
        const auto linkMap = client.getLinkMap();
        return linkMap ? static_cast<std::uintptr_t>(linkMap->l_addr) : 0;
    }
};
