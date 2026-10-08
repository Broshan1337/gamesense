#pragma once

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/Tracing/Tracing.h>
#include <Utils/Trig.h>












struct VisibilityResult {
    enum class State { Clear, WorldBlocked, EntityBlocked, Unknown };
    State state;
    float thickness; 
};

class VisibilityCheck {
public:
    [[nodiscard]] static VisibilityResult measure(const cs2::Vector& eye, const void* targetEntity,
                                                  const cs2::Vector& targetPoint, void* skipEntity) noexcept
    {
        const auto forward = Tracing::traceLine(eye, targetPoint, skipEntity);
        
        if (!forward.valid)
            return {VisibilityResult::State::Unknown, 0.0f};
        if (forward.reaches(targetEntity))
            return {VisibilityResult::State::Clear, 0.0f};

        
        if (forward.hitEntity != nullptr)
            return {VisibilityResult::State::EntityBlocked, 0.0f};

        
        
        const auto back = Tracing::traceLine(targetPoint, eye, skipEntity);
        if (!back.valid || !back.didHit || back.hitEntity != nullptr)
            return {VisibilityResult::State::Unknown, 0.0f};

        const float dx = forward.endPos.x - back.endPos.x;
        const float dy = forward.endPos.y - back.endPos.y;
        const float dz = forward.endPos.z - back.endPos.z;
        return {VisibilityResult::State::WorldBlocked, trig::squareRoot(dx * dx + dy * dy + dz * dz)};
    }

    
    
    
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
