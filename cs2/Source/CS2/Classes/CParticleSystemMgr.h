#pragma once

namespace cs2
{

// Two-float aggregate - passed in a single SSE register per the SysV ABI,
// matching the packed xmm argument pairs the game's fx callers pass.
struct ParticlePair {
    float x, y;
};

// Client-side particle spawn primitive (libclient): resolves systemName
// through the particle manager (blocking load if not precached), builds the
// internal effect spec and creates the particle collection. start/end become
// control points 0/1 of the collection. Owner null + attachment -1 = an
// unattached world-space system. Missing name/def fails closed inside.
struct CParticleSystemMgr {
    using SpawnEffect = void(
        const char* systemName,
        void* owner,
        int flags,
        int attachment,
        int unknown5,
        int unknown6,
        ParticlePair startXY,
        float startZ,
        ParticlePair endXY,
        float endZ,
        ParticlePair extraXY,
        float extraZ);
};

}