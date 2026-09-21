#pragma once

#include <cstdint>

#include "CEconItemAttribute.h"

namespace cs2
{

// What we need of C_UtlVectorEmbeddedNetworkVar<CEconItemAttribute>: a size followed by
// a pointer to the backing element array. Whatever real padding/NetworkVar bookkeeping
// precedes those two fields at the schema-resolved offset doesn't matter here - we only
// read/write these 16 bytes at that offset, never the whole struct.
struct EconAttributeArray {
    std::uint64_t size;
    CEconItemAttribute* data;
};

struct CAttributeList {
    using m_Attributes = EconAttributeArray;
};

}
