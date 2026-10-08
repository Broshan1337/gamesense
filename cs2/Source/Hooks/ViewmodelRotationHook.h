#pragma once

#include <array>
#include <cstring>
#include <limits>

#include <CS2/Constants/DllNames.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/ClientBuildProfile.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>

namespace viewmodel_rotation_hook {
using UpdatePose = void (*)(void*, cs2::Vector*, cs2::Vector*);
void onUpdatePose(void* model, cs2::Vector* position, cs2::Vector* angles) noexcept;
inline UpdatePose original{};
inline void* relay{};
inline std::uintptr_t callSite{};
inline std::int32_t oldDisplacement{};

inline bool writeDisplacement(std::int32_t displacement) noexcept
{
    const auto page = callSite & ~std::uintptr_t{0xfff};
    if (LinuxPlatformApi::mprotect(reinterpret_cast<void*>(page), 0x1000,
            PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    // Only replace the rel32 operand. The CALL opcode and surrounding code stay intact.
    std::memcpy(reinterpret_cast<void*>(callSite + 1), &displacement, sizeof(displacement));
    static_cast<void>(LinuxPlatformApi::mprotect(reinterpret_cast<void*>(page), 0x1000,
        PROT_READ | PROT_EXEC));
    return true;
}

inline void uninstall() noexcept
{
    if (callSite && writeDisplacement(oldDisplacement))
        callSite = 0;
}

// Called only after existing callbacks have drained during module unload.
inline void releaseRelay() noexcept
{
    if (relay && !callSite) {
        static_cast<void>(LinuxPlatformApi::munmap(relay, 0x1000));
        relay = nullptr;
    }
}

[[nodiscard]] inline bool install() noexcept
{
    if (callSite)
        return true;
    const LinuxDynamicLibrary client{cs2::CLIENT_DLL};
    const auto* map = client.getLinkMap();
    const auto base = map ? static_cast<std::uintptr_t>(map->l_addr) : 0;
    if (!client_build_profile::supported(base)) {
        StatusReport::record("Viewmodel rotation: unsupported client build", false);
        return false;
    }
    // C_CS2HudModelArms::UpdateAndSetupView copies camera origin/angles into
    // private locals, calls its bob/offset update, then commits the pose to both
    // arms and weapon and caches a quaternion. Hook this single call, before
    // all three consumers, rather than editing camera angles or scene-node fields.
    constexpr std::uintptr_t siteRva = 0x1faa198;
    constexpr std::uintptr_t updateRva = 0x1fa9d20;
    constexpr std::array<std::uint8_t, 8> expectedCall{0xe8,0x83,0xfb,0xff,0xff,0x48,0x8b,0x85};
    constexpr std::array<std::uint8_t, 14> expectedPrologue{
        0x55,0x48,0x89,0xe5,0x41,0x55,0x49,0x89,0xf5,0x41,0x54,0x49,0x89,0xd4};
    std::array<std::uint8_t, 8> call{};
    std::array<std::uint8_t, 14> prologue{};
    if (!LinuxPlatformApi::safeRead(reinterpret_cast<void*>(base + siteRva), call.data(), call.size())
        || call != expectedCall
        || !LinuxPlatformApi::safeRead(reinterpret_cast<void*>(base + updateRva), prologue.data(), prologue.size())
        || prologue != expectedPrologue) {
        StatusReport::record("Viewmodel rotation: pose ABI validation failed", false);
        return false;
    }
    // The module can be farther than 2 GiB away. Allocate a small RX relay near
    // the client; no stolen instructions or instruction relocation are involved.
    const auto site = base + siteRva;
    for (unsigned attempt = 0; attempt < 32 && !relay; ++attempt) {
        const auto hint = (site + (attempt + 1) * 0x2000000ull) & ~std::uintptr_t{0xfff};
        void* candidate = LinuxPlatformApi::mmap(reinterpret_cast<void*>(hint), 0x1000,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (candidate == MAP_FAILED)
            continue;
        const auto distance = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(candidate))
            - static_cast<std::int64_t>(site + 5);
        if (distance >= std::numeric_limits<std::int32_t>::min()
            && distance <= std::numeric_limits<std::int32_t>::max())
            relay = candidate;
        else
            static_cast<void>(LinuxPlatformApi::munmap(candidate, 0x1000));
    }
    if (!relay) {
        StatusReport::record("Viewmodel rotation: near relay allocation failed", false);
        return false;
    }
    // jmp [rip+0], followed by the absolute callback address (preserves registers).
    const std::array<std::uint8_t, 6> jump{0xff,0x25,0,0,0,0};
    std::memcpy(relay, jump.data(), jump.size());
    const auto target = reinterpret_cast<std::uintptr_t>(&onUpdatePose);
    std::memcpy(static_cast<std::byte*>(relay) + jump.size(), &target, sizeof(target));
    if (LinuxPlatformApi::mprotect(relay, 0x1000, PROT_READ | PROT_EXEC) != 0) {
        releaseRelay();
        return false;
    }
    original = reinterpret_cast<UpdatePose>(base + updateRva);
    std::memcpy(&oldDisplacement, call.data() + 1, sizeof(oldDisplacement));
    callSite = site;
    const auto displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(relay) - (site + 5));
    if (!writeDisplacement(displacement)) {
        callSite = 0;
        releaseRelay();
        return false;
    }
    StatusReport::record("Viewmodel rotation: HUD model pose hook installed", true);
    return true;
}
}
