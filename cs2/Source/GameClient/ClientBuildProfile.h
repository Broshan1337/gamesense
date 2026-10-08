#pragma once

#include <array>
#include <cstdint>
#include <Platform/Linux/LinuxPlatformApi.h>

namespace client_build_profile {
// GNU ELF build ID of the Linux client whose native ABIs were inspected.
[[nodiscard]] inline bool supported(std::uintptr_t base) noexcept
{
    constexpr std::array<std::uint8_t, 36> expected{
        4,0,0,0,20,0,0,0,3,0,0,0,0x47,0x4e,0x55,0,
        0xea,0x57,0xd8,0x83,0x3a,0xb2,0x97,0xb6,0x22,0x69,
        0x99,0x25,0xdb,0xc8,0x3b,0xf6,0xaa,0x1a,0xa6,0x15};
    std::array<std::uint8_t, 36> note{};
    return base && LinuxPlatformApi::safeRead(reinterpret_cast<void*>(base + 0x2a8),
        note.data(), note.size()) && note == expected;
}
}
