#pragma once

#include <cstdint>

#include "CUtlStringToken.h"

namespace cs2 {



struct IGameEvent;














struct IGameEventManager2 {
    using FireEventClientSide = bool(IGameEventManager2* thisptr, IGameEvent* event);
};








struct GameEventAccessors {
    
    
    
    
    static constexpr int kGetNameVtableSlot = 2;
    using GetName = const char*(IGameEvent* thisptr);

    
    
    
    
    
    
    
    
    
    static constexpr int kGetEntityForKeyVtableSlot = 13;
    using GetEntityForKey = std::int64_t(IGameEvent* thisptr, const CUtlStringToken* key, std::int64_t defaultValue);
};

}
