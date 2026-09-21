#pragma once

#include <cstdint>

namespace cs2
{

struct CAttributeList;

struct CEconItemView {
    using m_AttributeList = CAttributeList;
    using m_NetworkedDynamicAttributes = CAttributeList;
    using m_iItemDefinitionIndex = std::uint16_t;
    using m_iEntityQuality = std::int32_t;
    using m_iAccountID = std::uint32_t;
    using m_bDisallowSOC = bool;
    using m_bRestoreCustomMaterialAfterPrecache = bool;
    using m_iItemIDHigh = std::uint32_t;
    using m_iItemIDLow = std::uint32_t;
    using m_bInitialized = bool;

    // The real internal attribute-write path (found via string xref to "set item texture
    // wear"/"prefab"/"seed", the same names the Andromeda-CS2-Base reference uses). Unlike
    // a raw write into m_Attributes, this resolves the attribute definition by name, updates
    // an existing entry in place or grows the array through the item's own allocator, and
    // invalidates a per-item cached object at +0x1108 whenever any attribute changes - all
    // bookkeeping this project's earlier raw-write approach skipped entirely. See project
    // notes: that gap is the leading suspect for a real, symbol-confirmed crash (a near-null
    // read in the scene system's LOD-selection code) that a raw write could never explain.
    using SetAttributeValueByName = void(CEconItemView* thisptr, const char* attributeName, float value);
};

}
