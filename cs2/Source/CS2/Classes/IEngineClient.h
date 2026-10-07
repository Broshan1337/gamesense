#pragma once

namespace cs2
{





struct IEngineClient {
    
    
    
    
    
    
    
    
    using ExecuteClientCommand = void(IEngineClient* thisptr, int unknown0, const char* command, char unknown1);

    
    
    
    
    
    static constexpr int kExecuteClientCommandVtableSlot = 51;
};

}
