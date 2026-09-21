#pragma once

#include "C_BaseEntity.h"

namespace cs2
{

// Map-placed cubemap fog (Parent: C_BaseEntity per schema dump) - the fog type several maps
// actually drive their atmosphere with (the C_GradientFog entities on those maps are dormant).
// Fields are accessed through runtime schema offsets; this is only the classifier tag.
struct C_EnvCubemapFog : C_BaseEntity {
};

}
