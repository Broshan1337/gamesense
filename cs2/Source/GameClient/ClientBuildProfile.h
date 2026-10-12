#pragma once

#include <array>
#include <cstdint>
#include <Platform/Linux/LinuxPlatformApi.h>

namespace client_build_profile {
using BuildId = std::array<std::uint8_t, 20>;

// True when the loaded Linux client carries this GNU ELF build ID.
[[nodiscard]] inline bool is(std::uintptr_t base, const BuildId& id) noexcept
{
    constexpr std::array<std::uint8_t, 16> header{4,0,0,0,20,0,0,0,3,0,0,0,0x47,0x4e,0x55,0};
    std::array<std::uint8_t, 36> note{};
    if (!base || !LinuxPlatformApi::safeRead(reinterpret_cast<void*>(base + 0x2a8), note.data(), note.size()))
        return false;
    for (std::size_t i = 0; i < header.size(); ++i)
        if (note[i] != header[i]) return false;
    for (std::size_t i = 0; i < id.size(); ++i)
        if (note[header.size() + i] != id[i]) return false;
    return true;
}

// GNU ELF build ID of the Linux client whose native ABIs were inspected.
// Features keyed on this one build (viewmodel rotation, chams status) stay
// tied to it; a feature that was re-inspected on a later build carries its
// own per-build table and asks is() directly (see bullet_simulation).
inline constexpr BuildId kInspected{
    0xea,0x57,0xd8,0x83,0x3a,0xb2,0x97,0xb6,0x22,0x69,
    0x99,0x25,0xdb,0xc8,0x3b,0xf6,0xaa,0x1a,0xa6,0x15};

[[nodiscard]] inline bool supported(std::uintptr_t base) noexcept
{
    return is(base, kInspected);
}
}
