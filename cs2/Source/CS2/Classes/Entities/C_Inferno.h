#pragma once

#include <cstdint>

#include "C_BaseModelEntity.h"

namespace cs2
{




struct C_Inferno : C_BaseModelEntity {
    using m_nFireEffectTickBegin = std::int32_t; 
    using m_nFireLifetime = float;               
};

}
