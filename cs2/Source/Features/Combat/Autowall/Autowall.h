#pragma once

#include <Features/Combat/Autowall/Penetration.h>
#include <GameClient/Tracing/Tracing.h>

class Autowall {
public:
    static Optional<float> penetratedDamage(const cs2::Vector& start, const cs2::Vector& end,
                                            void* skip, void* target, float damage, float power) noexcept
    {
        return penetration::estimate(start,end,skip,target,damage,power,
            [](const auto& from,const auto& to,void* entity) {
                return Tracing::traceLine(from,to,entity,0x1C300B);
            });
    }
};
