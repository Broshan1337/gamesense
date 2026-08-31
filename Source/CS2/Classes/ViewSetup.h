#pragma once

#include <CS2/Classes/Vector.h>

// The per-frame camera setup the game fills in ClientModeCSNormal::OverrideView (vtable slot
// WIN64_LINUX(15, 16)) and consumes for the view projection.
//
// Offsets were derived offline for the current build by disassembling the OverrideView function
// (libclient 0x1AD19A0): it stores a float @0x498 (fov) and two vec3s @0x4A0/0x4B8 through the
// per-player camera singleton getters, with the same 0x18 origin->angles spacing as FrameworkCS2's
// 0x4E0/0x4F8 on its older build (a uniform +0x40 struct shift). Runtime consumers (third person,
// view punch removal) validate angle ranges before trusting them and fail closed otherwise - see
// Features/Visuals/ThirdPerson and Features/Visuals/Removals.
struct ViewSetup {
    static constexpr auto kFovOffset = 0x498;
    static constexpr auto kCameraPositionOffset = 0x4A0;
    static constexpr auto kViewAnglesOffset = 0x4B8;

    [[nodiscard]] static cs2::Vector& cameraPosition(ViewSetup* viewSetup) noexcept
    {
        return *reinterpret_cast<cs2::Vector*>(reinterpret_cast<std::byte*>(viewSetup) + kCameraPositionOffset);
    }

    [[nodiscard]] static cs2::Vector& viewAngles(ViewSetup* viewSetup) noexcept
    {
        return *reinterpret_cast<cs2::Vector*>(reinterpret_cast<std::byte*>(viewSetup) + kViewAnglesOffset);
    }
};
