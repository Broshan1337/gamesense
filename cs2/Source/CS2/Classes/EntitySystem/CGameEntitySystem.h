#pragma once

#include <cstdint>
#include "CEntityClass.h"

namespace cs2
{

struct CGameEntitySystem {
    // Entity-class map node (live-verified on build dce58989): 24 bytes =
    // {u16 left, right, parent, type; char* key; CEntityClass* value}. The 5GB
    // 2026-09-25 update shrank the node from the 32-byte {4x int, key@16, value@24}
    // layout to this one; the old shape made the classifier walk garbage and silently
    // killed every classifier-gated feature (knife skins, ESP, glow, grenade timers).
    struct EntityClassNode {
        std::uint16_t left;
        std::uint16_t right;
        std::uint16_t parent;
        std::uint16_t type;
        const char* key;
        CEntityClass* value;
    };

    // The map ADDRESSED AT ITS MEMORY-POINTER SLOT: the compiled tree walkers load
    // [entitySystem+0xAA0] directly and that slot is what OffsetToEntityClasses resolves
    // (the CUtlMap base sits 8 bytes lower and no code reads it). Live-verified on
    // dce58989: +10 (u16) = numElements (314 on the menu background map, matching the
    // dense pool nodes 0..313); +8/+12 not used here.
    struct EntityClasses {
        EntityClassNode* memory;
        std::uint16_t allocCount;
        std::uint16_t numElements;
        std::uint32_t rootOrFlags;
    };
};

static_assert(sizeof(CGameEntitySystem::EntityClassNode) == 24);
static_assert(sizeof(CGameEntitySystem::EntityClasses) == 16);

}
