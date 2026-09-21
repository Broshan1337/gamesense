#pragma once

#include "C_BaseEntity.h"

namespace cs2
{

// Map-placed gradient fog controller (Parent: C_BaseEntity per schema dump). We only need the
// type as a classifier tag - fields are read through runtime schema offsets.
struct C_GradientFog : C_BaseEntity {
};

}
