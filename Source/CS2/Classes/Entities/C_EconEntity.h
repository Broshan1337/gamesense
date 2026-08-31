#pragma once

#include <cstdint>

#include <CS2/Classes/CAttributeContainer.h>

#include "C_BaseModelEntity.h"

namespace cs2
{

struct C_EconEntity : C_BaseModelEntity {
    using m_nFallbackPaintKit = int;
    using m_nFallbackSeed = int;
    using m_flFallbackWear = float;
    using m_nFallbackStatTrak = int;
    using m_AttributeManager = CAttributeContainer;
    using m_OriginalOwnerXuidLow = std::uint32_t;
};

}
