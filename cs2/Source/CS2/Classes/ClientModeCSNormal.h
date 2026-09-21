#pragma once

struct ViewSetup;

namespace cs2
{

struct ClientModeCSNormal {
    using GetViewmodelFov = float(ClientModeCSNormal* thisptr);
    // vtable slot WIN64_LINUX(15, 16) - verified offline against the current build (the slot-16
    // function fills a ~0x500-byte view setup through the per-player camera singleton getters).
    using OverrideView = void(ClientModeCSNormal* thisptr, ViewSetup* viewSetup);
};

}
