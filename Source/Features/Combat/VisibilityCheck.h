#pragma once

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/Tracing/Tracing.h>
#include <Utils/Trig.h>

// Shared wall/visibility measurement for the combat features - ONE copy of the two-trace logic that
// used to live only in Triggerbot::passesVisibility, now also used by the rage target scan (AimTarget).
// Context-free on purpose: Tracing::traceLine is static, so this needs no HookContext and can be called
// from anywhere (per-candidate inside a target scan, or per-tick in a gate).
//
// Semantics (identical to the triggerbot's verified behavior):
//   Clear         - the trace reached the point unblocked, or its first hit was the target itself
//   WorldBlocked  - world geometry is in the way; thickness = distance between the wall's near face
//                   (forward trace) and far face (trace back from the target) - the penetrable depth
//   EntityBlocked - another ENTITY (a player) blocks the shot; never penetrable
//   Unknown       - the back-trace hit nothing (degenerate geometry); treat as blocked (fail closed)
struct VisibilityResult {
    enum class State { Clear, WorldBlocked, EntityBlocked, Unknown };
    State state;
    float thickness; // world units; meaningful only for WorldBlocked
};

class VisibilityCheck {
public:
    [[nodiscard]] static VisibilityResult measure(const cs2::Vector& eye, const void* targetEntity,
                                                  const cs2::Vector& targetPoint, void* skipEntity) noexcept
    {
        const auto forward = Tracing::traceLine(eye, targetPoint, skipEntity);
        // Clear line of sight, or the trace reached/hit the target itself: visible.
        if (!forward.didHit || forward.hitEntity == targetEntity)
            return {VisibilityResult::State::Clear, 0.0f};

        // Only WORLD geometry is penetrable - never shoot through a player.
        if (forward.hitEntity != nullptr)
            return {VisibilityResult::State::EntityBlocked, 0.0f};

        // Measure the wall: tracing back from the target, its first hit is the wall's far (exit) face.
        // The gap between the near and far face is the thickness the bullet would have to penentrate.
        const auto back = Tracing::traceLine(targetPoint, eye, skipEntity);
        if (!back.didHit)
            return {VisibilityResult::State::Unknown, 0.0f};

        const float dx = forward.endPos.x - back.endPos.x;
        const float dy = forward.endPos.y - back.endPos.y;
        const float dz = forward.endPos.z - back.endPos.z;
        return {VisibilityResult::State::WorldBlocked, trig::squareRoot(dx * dx + dy * dy + dz * dz)};
    }

    // The shared decision rule: a shot through a world wall is taken only when an autowall allowance
    // is configured (maxThickness > 0) and the wall is no thicker than it. The game's own FireBullet
    // performs the real penetration and damage - this only decides whether to pull the trigger.
    [[nodiscard]] static bool passes(const VisibilityResult& result, int maxThickness) noexcept
    {
        switch (result.state) {
        case VisibilityResult::State::Clear:
            return true;
        case VisibilityResult::State::WorldBlocked:
            return maxThickness > 0 && result.thickness <= static_cast<float>(maxThickness);
        case VisibilityResult::State::EntityBlocked:
        case VisibilityResult::State::Unknown:
        default:
            return false;
        }
    }
};
