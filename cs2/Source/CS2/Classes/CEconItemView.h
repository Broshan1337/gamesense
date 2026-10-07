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

    
    
    
    
    
    
    
    
    using SetAttributeValueByName = void(CEconItemView* thisptr, const char* attributeName, float value);
};

}
