#pragma once

#include <CS2/Classes/Vector.h>










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
