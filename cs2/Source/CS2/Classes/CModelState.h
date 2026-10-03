#pragma once

#include <cstdint>

namespace cs2
{

// Reached from a weapon entity via the schema chain C_BaseEntity::m_CBodyComponent ->
// CBodyComponentSkeletonInstance.m_skeletonInstance (+0x80) -> CSkeletonInstance.m_modelState
// (+0x140) - all schema-resolved since 2026-09-27 (the pre-update path was a "vtable+112"
// virtual call, empirically identified, that the 5GB update reshuffled into a static-pointer
// getter - see ModelStateOffsets.h). m_MeshGroupMask's offset (520 = 0x248) was confirmed
// directly from two decompiled KV3 (de)serializer functions for this class; m_hModel = 160
// (0xA0) per the fresh dumper.
struct CModelState {
    using m_MeshGroupMask = std::uint64_t;
    using m_hModel = std::uint64_t;
};

}
