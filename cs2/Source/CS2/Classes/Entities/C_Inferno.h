#pragma once

#include <cstdint>

#include "C_BaseModelEntity.h"

namespace cs2
{

// Molotov / incendiary fire area (client side). Only the fields our grenade burn timers read are
// declared as type tags for the pattern-resolved offsets - this is NOT a full struct layout.
// Base class per cs2-dumper (Parent: C_BaseModelEntity) so BaseEntity::as<Inferno>() can downcast.
struct C_Inferno : C_BaseModelEntity {
    using m_nFireEffectTickBegin = std::int32_t; // tick the fire was ignited (0 until burning)
    using m_nFireLifetime = float;               // total burn seconds (molotov vs incendiary differ)
};

}
