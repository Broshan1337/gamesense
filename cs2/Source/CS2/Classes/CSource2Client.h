#pragma once

#include <cstdint>

namespace cs2 {








struct CSource2Client {
    using OnFrameStageNotify = std::int64_t(CSource2Client* thisptr, int frameStage);

    
    
    
    
    
    
    
    
    using GetCommandIndex = int(CSource2Client* thisptr, int slot);
    static constexpr int kGetCommandIndexVtableSlot = 18;
};

}
