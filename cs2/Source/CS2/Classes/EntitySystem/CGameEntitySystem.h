#pragma once

#include <cstdint>
#include "CEntityClass.h"

namespace cs2
{

struct CGameEntitySystem {
    
    
    
    
    
    struct EntityClassNode {
        std::uint16_t left;
        std::uint16_t right;
        std::uint16_t parent;
        std::uint16_t type;
        const char* key;
        CEntityClass* value;
    };

    
    
    
    
    
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
