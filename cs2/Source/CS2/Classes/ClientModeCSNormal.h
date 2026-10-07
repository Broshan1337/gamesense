#pragma once

#include <Platform/Macros/PlatformSpecific.h>

struct ViewSetup;

namespace cs2
{

struct ClientModeCSNormal {
    using GetViewmodelFov = float(ClientModeCSNormal* thisptr);
    
    
    
    
    
    
    static constexpr int kGetViewmodelFovVtableSlot = WIN64_LINUX(27, 29);

    using OverrideView = void(ClientModeCSNormal* thisptr, ViewSetup* viewSetup);
    
    
    static constexpr int kOverrideViewVtableSlot = WIN64_LINUX(15, 16);
};

}
