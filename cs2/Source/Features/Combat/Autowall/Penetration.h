#pragma once

#include <cmath>
#include <algorithm>
#include <Features/Combat/HitboxGeometry.h>
#include <Utils/Optional.h>

namespace penetration {
// Conservative geometric estimate. Trace exits locally so air between two
// walls is not treated as wall thickness. Material modifiers are unavailable
// through the current trace adapter; do not claim engine-exact damage.
template <typename Trace>
Optional<float> estimate(const cs2::Vector& start, const cs2::Vector& end, void* skip, void* target,
                         float damage, float power, Trace&& trace) noexcept
{
    using namespace hitbox_geometry;
    if (!finite(start) || !finite(end) || !std::isfinite(damage) || damage<1
        || !std::isfinite(power) || power<=0)
        return {};
    const auto path=subtract(end,start);
    const float length=trig::squareRoot(dot(path,path));
    if (length<1) return {};
    const auto direction=scale(path,1.0f/length);
    auto position=start;
    for (int layer=0;layer<=4;++layer) {
        const auto entry=trace(position,end,skip);
        if (!entry.valid) return {};
        if (entry.reaches(target)) return damage;
        if (layer==4 || entry.hitEntity || !finite(entry.endPos)) return {};
        const float remaining=dot(subtract(end,entry.endPos),direction);
        Optional<cs2::Vector> exit;
        // Probe at most 90 units, then trace back across THIS surface. Inside-
        // solid hits have no outward normal and cannot be accepted as an exit.
        for (float step=2;step<=std::min(90.0f,remaining-0.25f);step+=2) {
            const auto probe=add(entry.endPos,scale(direction,step));
            const auto reverse=trace(probe,add(entry.endPos,scale(direction,-0.25f)),skip);
            if (!reverse.valid) return {};
            if (!reverse.didHit || reverse.hitEntity || !finite(reverse.endPos)
                || !finite(reverse.normal) || dot(reverse.normal,direction)<=0.01f)
                continue;
            const float thickness=dot(subtract(reverse.endPos,entry.endPos),direction);
            if (thickness<=0 || thickness>step+0.01f) continue;
            exit=reverse.endPos;
            break;
        }
        if (!exit.hasValue()) return {};
        const auto thicknessVector=subtract(exit.value(),entry.endPos);
        const float thicknessSquared=dot(thicknessVector,thicknessVector);
        damage-=11.25f/power+0.16f*damage+thicknessSquared/24.0f;
        if (!std::isfinite(damage) || damage<1) return {};
        const auto next=add(exit.value(),scale(direction,0.25f));
        if (dot(subtract(next,position),direction)<=0.25f) return {};
        position=next;
    }
    return {};
}
}
