#pragma once

#include <cstdint>

#include "CEconItemAttribute.h"

namespace cs2
{





struct EconAttributeArray {
    std::uint64_t size;
    CEconItemAttribute* data;
};

struct CAttributeList {
    using m_Attributes = EconAttributeArray;
};

}
