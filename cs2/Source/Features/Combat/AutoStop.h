#pragma once
#include <cmath>
#include <optional>
#include <algorithm>
#include <CS2/Classes/Vector.h>
#include <GameClient/UserCmd.h>
#include <Utils/Trig.h>

namespace auto_stop {
inline constexpr std::uint64_t forward=1ull<<3, back=1ull<<4, left=1ull<<9, right=1ull<<10;
inline constexpr auto mask=forward|back|left|right;

// Sim inputs (velocity misc.cpp autostop). All optional: missing values fall back
// to the friction-less unit-magnitude stop so unresolved cvars/schema never
// disable the feature.
struct StopInputs {
    std::optional<float> svFriction;
    std::optional<float> svStopspeed;
    std::optional<float> surfaceFriction;
    std::optional<float> weaponMaxSpeed;
};

// Counter-wish autostop, velocity's model: the counter-wish is the unit direction
// against the current velocity, scaled by post-friction speed / weapon max speed
// (clamped to 1) instead of the raw unit wish. The friction step
// (control = max(speed, stopspeed); drop = friction * surface * control * tick;
// post = max(speed - drop, 0)) decides whether a counter-wish is needed at all -
// when friction alone stops this tick, the scalars zero out.
inline void apply(const UserCmd& command, const cs2::Vector& velocity, float yaw,
                  const StopInputs& inputs = {}) noexcept
{
    if (!command || !std::isfinite(yaw) || !std::isfinite(velocity.x) || !std::isfinite(velocity.y))
        return;
    const float c=trig::cosine(yaw*trig::kDegreesToRadians), s=trig::sine(yaw*trig::kDegreesToRadians);
    const float f=velocity.x*c+velocity.y*s;
    const float l=-velocity.x*s+velocity.y*c;
    const float rawSpeed=trig::squareRoot(f*f+l*l);

    const auto zeroOut=[&]() noexcept {
        command.replaceButtons(mask,0);
        command.setForwardMove(0.0f);
        command.setLeftMove(0.0f);
    };

    if (rawSpeed<=5.0f) {
        zeroOut();
        return;
    }

    float speed=1.0f;
    bool needWish=true;
    if (inputs.svFriction.has_value() && inputs.svStopspeed.has_value() && inputs.surfaceFriction.has_value()) {
        const float control=std::fmax(rawSpeed, *inputs.svStopspeed);
        const float drop=(*inputs.svFriction)*(*inputs.surfaceFriction)*control*(1.0f/64.0f);
        const float post=std::fmax(rawSpeed-drop, 0.0f);
        speed=post>0.0f?std::clamp(post/(inputs.weaponMaxSpeed.value_or(250.0f)), 0.0f, 1.0f):0.0f;
        needWish=post>0.0f;
    } else {
        speed=std::clamp(rawSpeed/inputs.weaponMaxSpeed.value_or(rawSpeed), 0.0f, 1.0f);
    }

    if (!needWish || speed<=0.0f) {
        zeroOut();
        return;
    }

    const float forwardMove=-f/rawSpeed*speed;
    const float leftMove=-l/rawSpeed*speed;
    command.replaceButtons(mask,(forwardMove>0 ? forward : forwardMove<0 ? back : 0)
                                |(leftMove>0 ? left : leftMove<0 ? right : 0));
    command.setForwardMove(forwardMove);
    command.setLeftMove(leftMove);
}
}