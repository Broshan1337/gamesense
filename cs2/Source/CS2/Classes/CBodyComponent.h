#pragma once

#include "CModelState.h"

namespace cs2
{

struct CBodyComponent {};

// Schema-documented chain (fresh dumper, build 68f386a6): CBodyComponentSkeletonInstance
// embeds CSkeletonInstance at m_skeletonInstance (+0x80), which embeds CModelState at
// m_modelState (+0x140). This replaces the OLD "vtable+112" call to the body component
// (empirically slot 14, reshuffled in the 2026-09-25/26 5GB update - the crash trail is in
// BaseWeapon.h / ModelStateOffsets.h).
struct CSkeletonInstance {
    using m_modelState = CModelState;
};

struct CBodyComponentSkeletonInstance {
    using m_skeletonInstance = CSkeletonInstance;
};

}
