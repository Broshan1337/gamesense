#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include <Platform/Linux/LinuxPlatformApi.h>













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
        (void)setWritable(address, false); 

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
            (void)setWritable(patchedAddress, false); 
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
