#pragma once

#include <cstdint>

#include <Utils/Pad.h>

namespace cs2
{

struct GlobalVars {
    using frametime = float; 

    PAD(16); 
    std::int32_t maxClients; 
    PAD(8); 
    
    
    
    
    float intervalPerTick; 
    PAD(16); 
    float curtime; 
    PAD(12); 
    
    
    
    float intervalPerTickLegacy; 
    std::int32_t tickCount; 
};

}
