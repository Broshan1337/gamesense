#pragma once

#include <cstdint>

namespace cs2
{

// Fake attribute entries written into a weapon's CAttributeList::m_Attributes to drive
// the live gun-skin render path. Layout reverse engineered (independently, matching
// between our own RE and a public reference implementation - see project memory).
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
