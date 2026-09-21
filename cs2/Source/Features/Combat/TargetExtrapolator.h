#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <GameClient/Tracing/Tracing.h>
#include <Utils/Trig.h>

// Predicts where a moving enemy will be a few ticks from now - velocity-cs2's shared::lagcomp
// predict_movement + extrapolate (extrapolation.cpp). Steps the target's origin forward using its
// velocity, gravity and hull traces against the world (so it slides along walls and stops at them rather
// than walking through), letting the aimbot LEAD a strafing/running target instead of aiming where they
// currently are. Returns just the origin delta (predicted - current); the caller offsets the aim point by
// it - velocity shifts the whole skeleton by the same delta, which is a good approximation over the few
// ticks we predict.
//
// Simplifications vs velocity (deliberate, first cut): no lag-comp record history, so no turn-rate
// (direction_change) prediction - we extrapolate along the CURRENT velocity heading. gravity is the CS2
// default 800 rather than read from sv_gravity. Both are fine for leading a target a handful of ticks.
template <typename HookContext>
class TargetExtrapolator {
public:
    explicit TargetExtrapolator(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // The predicted displacement of `target` (a PlayerPawn wrapper) after `ticks` ticks, or {0,0,0} if it
    // can't be predicted (ticks<=0, target stationary, or a required field/origin is unreadable).
    [[nodiscard]] cs2::Vector predictedDelta(auto&& target, int ticks) const noexcept
    {
        const cs2::Vector zero{0.0f, 0.0f, 0.0f};
        if (ticks <= 0)
            return zero;

        auto* const entity = static_cast<cs2::C_BaseEntity*>(target.baseEntity());
        if (!entity)
            return zero;

        auto&& schema = hookContext.schemaSystem();
        const auto velocityOffset = schema.getFieldOffset("C_BaseEntity", "m_vecVelocity");
        const auto flagsOffset = schema.getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!velocityOffset.has_value() || *velocityOffset <= 0 || !flagsOffset.has_value() || *flagsOffset <= 0)
            return zero;

        const auto originOpt = target.absOrigin();
        if (!originOpt.hasValue())
            return zero;

        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(entity) + *velocityOffset, sizeof(velocity));
        if (trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y) < 0.1f)
            return zero; // stationary - nothing to lead

        std::uint32_t flags{};
        std::memcpy(&flags, reinterpret_cast<const std::byte*>(entity) + *flagsOffset, sizeof(flags));

        cs2::Vector mins{-16.0f, -16.0f, 0.0f};
        cs2::Vector maxs{16.0f, 16.0f, 72.0f};
        readCollisionBounds(entity, schema, mins, maxs);

        cs2::Vector origin = originOpt.value();
        const cs2::Vector startOrigin = origin;
        for (int i = 0; i < ticks; ++i)
            predictMovement(origin, velocity, flags, mins, maxs, entity);

        return cs2::Vector{origin.x - startOrigin.x, origin.y - startOrigin.y, origin.z - startOrigin.z};
    }

private:
    // Reads the target's OBB from its CCollisionProperty; leaves the passed-in standing-player defaults if
    // the collision object or the fields don't resolve.
    void readCollisionBounds(cs2::C_BaseEntity* entity, auto&& schema, cs2::Vector& mins, cs2::Vector& maxs) const noexcept
    {
        const auto collisionOffset = schema.getFieldOffset("C_BaseEntity", "m_pCollision");
        if (!collisionOffset.has_value() || *collisionOffset <= 0)
            return;
        void* collision{};
        std::memcpy(&collision, reinterpret_cast<const std::byte*>(entity) + *collisionOffset, sizeof(collision));
        if (!collision)
            return;
        const auto minsOffset = schema.getFieldOffset("CCollisionProperty", "m_vecMins");
        const auto maxsOffset = schema.getFieldOffset("CCollisionProperty", "m_vecMaxs");
        if (!minsOffset.has_value() || *minsOffset <= 0 || !maxsOffset.has_value() || *maxsOffset <= 0)
            return;
        std::memcpy(&mins, reinterpret_cast<const std::byte*>(collision) + *minsOffset, sizeof(mins));
        std::memcpy(&maxs, reinterpret_cast<const std::byte*>(collision) + *maxsOffset, sizeof(maxs));
    }

    // One tick of movement: gravity (or grounded), a hull sweep to the next position with up to two wall
    // slides, then a short down-trace to refresh the on-ground flag. Faithful to velocity's predict_movement.
    void predictMovement(cs2::Vector& origin, cs2::Vector& velocity, std::uint32_t& flags, const cs2::Vector& mins, const cs2::Vector& maxs, void* skip) const noexcept
    {
        if (flags & kFlOnGround)
            velocity.z = 0.0f;
        else
            velocity.z -= kGravity * kTickInterval;

        const cs2::Vector moveEnd{
            origin.x + velocity.x * kTickInterval,
            origin.y + velocity.y * kTickInterval,
            origin.z + velocity.z * kTickInterval,
        };

        auto trace = Tracing::traceHull(origin, moveEnd, mins, maxs, skip);

        if (trace.fraction != 1.0f) {
            for (int i = 0; i < 2; ++i) {
                clipVelocity(velocity, trace.normal);
                const float remaining = 1.0f - trace.fraction;
                const cs2::Vector clipEnd{
                    trace.endPos.x + velocity.x * kTickInterval * remaining,
                    trace.endPos.y + velocity.y * kTickInterval * remaining,
                    trace.endPos.z + velocity.z * kTickInterval * remaining,
                };
                trace = Tracing::traceHull(trace.endPos, clipEnd, mins, maxs, skip);
                if (trace.fraction == 1.0f)
                    break;
            }
        }

        origin = (trace.fraction == 1.0f) ? moveEnd : trace.endPos;

        const cs2::Vector groundEnd{origin.x, origin.y, origin.z - 2.0f};
        const auto ground = Tracing::traceHull(origin, groundEnd, mins, maxs, skip);
        flags &= ~kFlOnGround;
        if (ground.fraction != 1.0f && ground.normal.z > 0.7f)
            flags |= kFlOnGround;
    }

    // Removes the component of `velocity` going into a surface with `normal` (and a second pass if it still
    // points inward) - the standard slide-along-wall clip.
    static void clipVelocity(cs2::Vector& velocity, const cs2::Vector& normal) noexcept
    {
        const float dot = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z;
        velocity.x -= normal.x * dot;
        velocity.y -= normal.y * dot;
        velocity.z -= normal.z * dot;

        const float adjust = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z;
        if (adjust < 0.0f) {
            velocity.x -= normal.x * adjust;
            velocity.y -= normal.y * adjust;
            velocity.z -= normal.z * adjust;
        }
    }

    static constexpr std::uint32_t kFlOnGround = 1u << 0;
    static constexpr float kTickInterval = 0.015625f; // 1/64 (CS2 is 64 tick)
    static constexpr float kGravity = 800.0f;         // sv_gravity default

    HookContext& hookContext;
};
