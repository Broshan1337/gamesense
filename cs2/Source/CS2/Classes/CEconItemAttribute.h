#pragma once

#include <cstdint>

namespace cs2
{




struct CEconItemAttribute {
    void* vtable;
    void* owner;
    std::uint8_t pad_0010[32];
    std::uint16_t definitionIndex;
    std::uint16_t pad_0032;
    float value;
    float initValue;
    std::int32_t refundableCurrency;
    bool setBonus;
    std::uint8_t pad_0041[7];
};

static_assert(sizeof(CEconItemAttribute) == 0x48);

enum class EconItemAttributeDefinitionIndex : std::uint16_t {
    Paint = 6,
    Pattern = 7,
    Wear = 8,
};

}
