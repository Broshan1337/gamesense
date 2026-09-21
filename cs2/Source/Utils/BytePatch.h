#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include <Platform/Linux/LinuxPlatformApi.h>

// Writes bytes over live executable code, remembering the originals so the change can be undone.
//
// This is the only place in this project that modifies the game's CODE rather than hooking it or
// reading its data, so it is deliberately conservative:
//  - it refuses to apply twice, and to restore when not applied, so the saved originals can never
//    be overwritten with already-patched bytes (which would make the change permanent for the
//    session);
//  - it restores on unload, like the project's hooks do, rather than leaving the game modified;
//  - mprotect works on whole pages, so the address is rounded DOWN to a page boundary and the
//    length extended to cover a patch that straddles two pages. Passing an unaligned address to
//    mprotect fails outright, which would otherwise turn into a silent no-patch or a segfault on
//    the memcpy.
template <std::size_t Size>
class BytePatch {
public:
    [[nodiscard]] bool apply(void* address, const std::uint8_t (&bytes)[Size]) noexcept
    {
        if (applied || !address)
            return false;

        if (!setWritable(address, true))
            return false;

        std::memcpy(originalBytes, address, Size);
        std::memcpy(address, bytes, Size);
        (void)setWritable(address, false); // re-protecting is best-effort; the patched bytes are in place

        patchedAddress = address;
        applied = true;
        return true;
    }

    void restore() noexcept
    {
        if (!applied || !patchedAddress)
            return;

        if (setWritable(patchedAddress, true)) {
            std::memcpy(patchedAddress, originalBytes, Size);
            (void)setWritable(patchedAddress, false); // best-effort, same as in apply()
        }

        applied = false;
        patchedAddress = nullptr;
    }

    [[nodiscard]] bool isApplied() const noexcept { return applied; }

private:
    [[nodiscard]] static bool setWritable(void* address, bool writable) noexcept
    {
        const auto pageSize = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
        if (pageSize == 0 || pageSize == static_cast<std::uintptr_t>(-1))
            return false;

        const auto start = reinterpret_cast<std::uintptr_t>(address) & ~(pageSize - 1);
        const auto length = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(address) + Size - start);
        const auto protection = writable ? (PROT_READ | PROT_WRITE | PROT_EXEC) : (PROT_READ | PROT_EXEC);
        return LinuxPlatformApi::mprotect(reinterpret_cast<void*>(start), length, protection) == 0;
    }

    std::uint8_t originalBytes[Size]{};
    void* patchedAddress{nullptr};
    bool applied{false};
};
