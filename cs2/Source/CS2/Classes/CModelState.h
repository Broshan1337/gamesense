#pragma once

#include <cstdint>

namespace cs2
{

// Reached from a weapon entity via C_BaseEntity::m_CBodyComponent -> vtable+112 (a virtual
// call, functionally GetSkeletonInstance() - identified empirically, not by name, via two
// independent decompiled callers reaching the exact same object type before operating on
// model-state fields directly). The returned object either is, or directly embeds at offset
// 0, a CModelState. m_MeshGroupMask's offset (520) was confirmed directly from two decompiled
// KV3 (de)serializer functions for this class (both reference the field at the same offset).
struct CModelState {
    using m_MeshGroupMask = std::uint64_t;
};

}
