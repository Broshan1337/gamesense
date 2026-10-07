#pragma once

#include <Platform/Macros/PlatformSpecific.h>

struct ViewSetup;

namespace cs2
{

struct ClientModeCSNormal {
    using GetViewmodelFov = float(ClientModeCSNormal* thisptr);
    // vtable slot WIN64_LINUX(27, 29). Linux 29 verified on 1.41.8.8 and 1.41.8.9 (RTTI vtable:
    // slot 29 = float(this) reading the viewmodel_fov convar wrapper, weapon-RTTI branch,
    // 20.0/54.0 fallbacks; slot 28 is void(this, StatsStruct*) - a byte + five floats written
    // through its second argument, NOT a getter). The Windows value is inherited from the
    // original port and NOT re-verified against a current client.dll - re-derive before
    // trusting it on a Windows build.
    static constexpr int kGetViewmodelFovVtableSlot = WIN64_LINUX(27, 29);

    using OverrideView = void(ClientModeCSNormal* thisptr, ViewSetup* viewSetup);
    // vtable slot WIN64_LINUX(15, 16) - verified offline against the current build (the slot-16
    // function fills a ~0x500-byte view setup through the per-player camera singleton getters).
    static constexpr int kOverrideViewVtableSlot = WIN64_LINUX(15, 16);
};

}
