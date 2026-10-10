#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/StatusReport.h>

namespace team_select_guard
{

inline constexpr std::uint8_t kExpectedOriginal[11]{0x48, 0x8B, 0x7B, 0x08, 0x45, 0x0F, 0xB6, 0xFC, 0x44, 0x89, 0xFE};
inline constexpr std::uint8_t kExpectedEsiLoad[3]{0x44, 0x89, 0xFE};

inline std::uint8_t originalBytes[11]{};
inline std::uint8_t originalEsiLoad[3]{};
inline std::uintptr_t patchSite{0};
inline bool active{false};

[[nodiscard]] inline bool writeBytes(std::uintptr_t address, const void* source, std::size_t size) noexcept
{
    const auto firstPage = address & ~std::uintptr_t{0xFFF};
    const auto lastPage = (address + size - 1) & ~std::uintptr_t{0xFFF};
    const auto length = lastPage - firstPage + 0x1000;
    if (LinuxPlatformApi::mprotect(reinterpret_cast<void*>(firstPage), length, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    std::memcpy(reinterpret_cast<void*>(address), source, size);
    return LinuxPlatformApi::mprotect(reinterpret_cast<void*>(firstPage), length, PROT_READ | PROT_EXEC) == 0;
}

inline void uninstall() noexcept
{
    if (!active)
        return;
    (void)writeBytes(patchSite, originalBytes, sizeof(originalBytes));
    (void)writeBytes(patchSite + 0x18, originalEsiLoad, sizeof(originalEsiLoad));
    active = false;
    patchSite = 0;
}

[[nodiscard]] inline bool install(std::uintptr_t site) noexcept
{
    if (active || !site)
        return false;
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(site);
    if (std::memcmp(bytes, kExpectedOriginal, sizeof(kExpectedOriginal)) != 0)
        return false;
    if (std::memcmp(bytes + 0x18, kExpectedEsiLoad, sizeof(kExpectedEsiLoad)) != 0)
        return false;
    if (bytes[-2] != 0x75)
        return false;
    const auto originalRel = static_cast<std::int8_t>(bytes[-1]);

    std::uint8_t patched[11]{};
    patched[0] = 0x48;
    patched[1] = 0x8B;
    patched[2] = 0x7B;
    patched[3] = 0x08;
    patched[4] = 0x85;
    patched[5] = 0xFF;
    patched[6] = 0x74;
    patched[7] = static_cast<std::uint8_t>(static_cast<std::int8_t>(originalRel - 8));
    patched[8] = 0x44;
    patched[9] = 0x89;
    patched[10] = 0xE6;

    std::memcpy(originalBytes, bytes, sizeof(originalBytes));
    std::memcpy(originalEsiLoad, bytes + 0x18, sizeof(originalEsiLoad));
    if (!writeBytes(site, patched, sizeof(patched))) {
        patchSite = 0;
        return false;
    }
    const std::uint8_t esiPatch[3]{0x44, 0x89, 0xE6};
    if (!writeBytes(site + 0x18, esiPatch, sizeof(esiPatch))) {
        (void)writeBytes(site, originalBytes, sizeof(originalBytes));
        return false;
    }
    patchSite = site;
    active = true;
    return true;
}

}
