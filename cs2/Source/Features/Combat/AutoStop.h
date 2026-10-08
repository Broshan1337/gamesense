#pragma once
#include <cmath>
#include <algorithm>
#include <CS2/Classes/Vector.h>
#include <GameClient/UserCmd.h>
#include <Utils/Trig.h>

namespace auto_stop {
inline constexpr std::uint64_t forward=1ull<<3, back=1ull<<4, left=1ull<<9, right=1ull<<10;
inline constexpr auto mask=forward|back|left|right;
inline void apply(const UserCmd& command, const cs2::Vector& velocity, float yaw) noexcept
{
    if (!command || !std::isfinite(yaw) || !std::isfinite(velocity.x) || !std::isfinite(velocity.y))
        return;
    const float c=trig::cosine(yaw*trig::kDegreesToRadians), s=trig::sine(yaw*trig::kDegreesToRadians);
    const float f=velocity.x*c+velocity.y*s;
    const float l=-velocity.x*s+velocity.y*c;
    const float speed=trig::squareRoot(f*f+l*l);
    const float forwardMove=speed>5.0f ? -f/speed : 0.0f;
    const float leftMove=speed>5.0f ? -l/speed : 0.0f;
    command.replaceButtons(mask,(forwardMove>0 ? forward : forwardMove<0 ? back : 0)
                                |(leftMove>0 ? left : leftMove<0 ? right : 0));
    command.setForwardMove(forwardMove);
    command.setLeftMove(leftMove);
}
}
