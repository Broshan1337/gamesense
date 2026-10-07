#pragma once

#include <algorithm>
#include <Features/Combat/HitboxGeometry.h>
#include <Features/Combat/ShotGeometry.h>

class MultiPoint {
public:
    static constexpr int kMaxPoints=12;
    struct Point { cs2::Vector position; bool isCenter; };
    static cs2::Vector rotateVector(const float* q,const cs2::Vector& v) noexcept { return hitbox_geometry::rotate(q,v); }

    static int generate(const Hitboxes::Entry& hitbox,const cs2::Vector& boneOrigin,const float* rotation,
                        float percent,const cs2::Vector& eye,float inaccuracy,bool dynamic,Point* out,
                        float boneScale=1.0f) noexcept
    {
        using namespace hitbox_geometry;
        if (!out || !std::isfinite(percent) || !finite(eye)) return 0;
        const auto shape=from(hitbox,boneOrigin,rotation,boneScale);
        if (!shape.valid) return 0;
        const auto center=shape.center();
        int count=0;
        out[count++]={center,true};
        float fraction=std::clamp(percent,0.0f,95.0f)/100.0f;
        if (dynamic && std::isfinite(inaccuracy) && inaccuracy>0 && shape.radius>0) {
            const auto path=subtract(center,eye);
            const float distance=trig::squareRoot(dot(path,path));
            fraction/=1.0f+inaccuracy*distance/shape.radius;
        }
        if (fraction<=0.01f) return count;
        const auto push=[&](const cs2::Vector& point) { if (count<kMaxPoints) out[count++]={point,false}; };
        if (shape.box) {
            const auto half=scale(subtract(shape.maxs,shape.mins),0.5f*fraction);
            const cs2::Vector offsets[]{{half.x,0,0},{-half.x,0,0},{0,half.y,0},{0,-half.y,0},{0,0,half.z},{0,0,-half.z}};
            for (const auto& offset:offsets) push(add(center,rotate(shape.rotation,offset)));
        } else {
            const auto a=add(shape.origin,rotate(shape.rotation,shape.mins));
            const auto b=add(shape.origin,rotate(shape.rotation,shape.maxs));
            const auto axis=subtract(b,a);
            const float length=trig::squareRoot(dot(axis,axis));
            if (length>0.01f) {
                push(add(center,scale(axis,0.5f*fraction)));
                push(add(center,scale(axis,-0.5f*fraction)));
            }
            const auto angles=shot_geometry::anglesTo(eye,center);
            const auto basis=shot_geometry::angleVectors(angles.pitch,angles.yaw);
            const float radius=shape.radius*fraction;
            // Every radial point lies inside the capsule, including tilted bones.
            push(add(center,scale(basis.left,radius)));
            push(add(center,scale(basis.left,-radius)));
            push(add(center,scale(basis.up,radius)));
            push(add(center,scale(basis.up,-radius)));
        }
        return count;
    }
};
