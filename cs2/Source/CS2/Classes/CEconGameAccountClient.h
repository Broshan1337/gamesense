#pragma once

#include <cstdint>

namespace cs2
{










struct CEconGameAccountClient {
    
    
    
    
    static constexpr int kEconClientOffset = 0x12C848;

    
    
    static constexpr int kSharedObjectCacheOffset = 0x68;

    static constexpr int kElevatedStateOffset = 48;

    
    
    static constexpr std::uint32_t kElevatedStatePrime = 5;
};

}
