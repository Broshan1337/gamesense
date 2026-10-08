#pragma once

#include <algorithm>
#include <cmath>
#include <CS2/Classes/Vector.h>
#include <GameClient/Hitboxes.h>
#include <Utils/Trig.h>

namespace hitbox_geometry {
inline float dot(const cs2::Vector& a, const cs2::Vector& b) noexcept { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline cs2::Vector subtract(const cs2::Vector& a, const cs2::Vector& b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline cs2::Vector add(const cs2::Vector& a, const cs2::Vector& b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline cs2::Vector scale(const cs2::Vector& a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
inline bool finite(const cs2::Vector& a) noexcept { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline cs2::Vector rotate(const float* q, const cs2::Vector& v) noexcept
{
    const cs2::Vector t{2*(q[1]*v.z-q[2]*v.y),2*(q[2]*v.x-q[0]*v.z),2*(q[0]*v.y-q[1]*v.x)};
    return {v.x+q[3]*t.x+q[1]*t.z-q[2]*t.y,
            v.y+q[3]*t.y+q[2]*t.x-q[0]*t.z,
            v.z+q[3]*t.z+q[0]*t.y-q[1]*t.x};
}

struct Shape {
    cs2::Vector origin{}, mins{}, maxs{};
    float rotation[4]{0,0,0,1};
    float radius{};
    bool box{};
    bool valid{};

    cs2::Vector center() const noexcept { return add(origin, rotate(rotation, scale(add(mins,maxs),0.5f))); }

    // Unit ray versus capsule or oriented box. The ray is half-infinite:
    // shapes behind the eye cannot count as hits.
    bool intersects(const cs2::Vector& eye, const cs2::Vector& direction) const noexcept
    {
        if (!valid || !finite(eye) || !finite(direction))
            return false;
        const float inverse[4]{-rotation[0],-rotation[1],-rotation[2],rotation[3]};
        const auto p = rotate(inverse, subtract(eye,origin));
        const auto d = rotate(inverse,direction);
        const float dd = dot(d,d);
        if (dd < 1e-8f)
            return false;
        if (box) {
            float near = 0.0f, far = 1.0e30f;
            const float ps[]{p.x,p.y,p.z}, ds[]{d.x,d.y,d.z};
            const float lo[]{mins.x,mins.y,mins.z}, hi[]{maxs.x,maxs.y,maxs.z};
            for (int i=0;i<3;++i) {
                if (trig::absolute(ds[i]) < 1e-8f) {
                    if (ps[i] < lo[i] || ps[i] > hi[i]) return false;
                } else {
                    float a=(lo[i]-ps[i])/ds[i], b=(hi[i]-ps[i])/ds[i];
                    if (a>b) std::swap(a,b);
                    near=std::max(near,a); far=std::min(far,b);
                    if (near>far) return false;
                }
            }
            return true;
        }
        const auto v = subtract(maxs,mins);
        const auto w = subtract(p,mins);
        const float vv=dot(v,v), dv=dot(d,v), dw=dot(d,w), vw=dot(v,w);
        const float denominator=dd*vv-dv*dv;
        float u = denominator > 1e-8f ? std::clamp((dd*vw-dv*dw)/denominator,0.0f,1.0f) : 0.0f;
        float t = std::max(0.0f,(dv*u-dw)/dd);
        if (t==0.0f) u=vv>1e-8f ? std::clamp(vw/vv,0.0f,1.0f) : 0.0f;
        const auto separation = subtract(add(w,scale(d,t)),scale(v,u));
        return dot(separation,separation) <= radius*radius;
    }
};

inline Shape from(const Hitboxes::Entry& entry, const cs2::Vector& origin, const float* rotation, float boneScale=1.0f) noexcept
{
    Shape result;
    if (!finite(origin) || !finite(entry.mins) || !finite(entry.maxs)
        || !std::isfinite(entry.radius) || entry.radius<0 || !std::isfinite(boneScale) || boneScale<=0)
        return result;
    result.origin=origin; result.mins=scale(entry.mins,boneScale); result.maxs=scale(entry.maxs,boneScale);
    result.radius=entry.radius*boneScale; result.box=entry.boxShape;
    if (!entry.translationOnly) {
        float norm=0;
        for (int i=0;i<4;++i) { if (!std::isfinite(rotation[i])) return {}; norm+=rotation[i]*rotation[i]; }
        if (norm<1e-8f) return {};
        const float inverse=1.0f/trig::squareRoot(norm);
        for (int i=0;i<4;++i) result.rotation[i]=rotation[i]*inverse;
    }
    if (result.box && (result.mins.x>result.maxs.x || result.mins.y>result.maxs.y || result.mins.z>result.maxs.z))
        return {};
    result.valid=true;
    return result;
}
}
