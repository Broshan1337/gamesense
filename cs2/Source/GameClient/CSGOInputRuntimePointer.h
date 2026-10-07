#pragma once

#include <optional>

#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/MemorySection.h>
#include <Utils/StatusReport.h>

#include <MemorySearch/TypeinfoVtableResolver.h>

#include <CS2/Constants/DllNames.h>
#include <CS2/Classes/CCSGOInput.h>

















namespace csgo_input_runtime
{

struct VtableAndObject {
    typeinfo_vtable::VtableCandidate vtable;
    cs2::CCSGOInput* object{nullptr};
};




template <typename Sink>
[[nodiscard]] bool forEachAnonymousRwRegion(Sink&& sink) noexcept
{
    const int fd = LinuxPlatformApi::open("/proc/self/maps", 0 );
    if (fd < 0)
        return false;

    constexpr std::size_t kMaxCarry = 512;
    char chunk[4096];
    char carry[kMaxCarry];
    std::size_t carryLength = 0;
    off_t position = 0;
    bool any = false;

    auto processLine = [&](const char* line, std::size_t length) {
        
        std::size_t dash = 0;
        while (dash < length && line[dash] != '-')
            ++dash;
        if (dash == 0 || dash + 1 >= length)
            return;
        std::uintptr_t low = 0, high = 0;
        for (std::size_t i = 0; i < dash; ++i) {
            const char c = line[i];
            if (c < '0' || (c > '9' && c < 'a') || c > 'f') return;
            low = low * 16 + static_cast<std::uintptr_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        std::size_t spaceAfterHigh = dash + 1;
        while (spaceAfterHigh < length && line[spaceAfterHigh] != ' ')
            ++spaceAfterHigh;
        if (spaceAfterHigh - dash - 1 < 2 || spaceAfterHigh + 5 >= length)
            return;
        for (std::size_t i = dash + 1; i < spaceAfterHigh; ++i) {
            const char c = line[i];
            if (c < '0' || (c > '9' && c < 'a') || c > 'f') return;
            high = high * 16 + static_cast<std::uintptr_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        const char* perms = line + spaceAfterHigh + 1;
        if (perms[0] != 'r' || perms[1] != 'w' || perms[2] != '-' || perms[3] != 'p')
            return;
        
        int spaces = 0;
        std::size_t i = spaceAfterHigh + 1;
        for (; i < length; ++i) {
            if (line[i] == ' ' && ++spaces == 4)
                break;
        }
        if (spaces < 4)
            return;
        ++i;
        while (i < length && line[i] == ' ')
            ++i;
        if (i < length && line[i] != '\0')
            return;   
        
        
        if (high > low && high - low < (std::uintptr_t{256} << 20)) {
            sink(low, high);
            any = true;
        }
    };

    while (true) {
        const auto got = LinuxPlatformApi::pread(fd, chunk, sizeof(chunk), position);
        if (got <= 0)
            break;
        position += static_cast<off_t>(got);
        std::size_t lineBegin = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(got); ++i) {
            if (chunk[i] != '\n')
                continue;
            if (carryLength > 0) {
                const auto room = kMaxCarry - carryLength;
                const auto part = (i - lineBegin) < room ? (i - lineBegin) : room;
                char merged[kMaxCarry];
                std::memcpy(merged, carry, carryLength);
                std::memcpy(merged + carryLength, chunk + lineBegin, part);
                processLine(merged, carryLength + part);
                carryLength = 0;
            } else {
                processLine(chunk + lineBegin, i - lineBegin);
            }
            lineBegin = i + 1;
        }
        carryLength = 0;
        const std::size_t remaining = static_cast<std::size_t>(got) - lineBegin;
        if (remaining > 0 && remaining < kMaxCarry) {
            std::memcpy(carry, chunk + lineBegin, remaining);
            carryLength = remaining;
        }
    }
    LinuxPlatformApi::close(fd);
    return any;
}

struct VptrScanResult {
    std::uintptr_t address{0};
    int hits{0};
};

[[nodiscard]] inline VptrScanResult scanForVptr(std::uintptr_t vptrValue) noexcept
{
    VptrScanResult result;
    std::uintptr_t firstHit = 0;
    forEachAnonymousRwRegion([&](std::uintptr_t low, std::uintptr_t high) {
        for (std::uintptr_t addr = (low + 7) & ~std::uintptr_t{7}; addr + 8 <= high; addr += 8) {
            std::uintptr_t value{};
            std::memcpy(&value, reinterpret_cast<const void*>(addr), sizeof(value));
            if (value != vptrValue)
                continue;
            ++result.hits;
            if (result.hits == 1)
                firstHit = addr;
            if (result.hits > 1)
                return;   
        }
    });
    if (result.hits == 1)
        result.address = firstHit;
    return result;
}

[[nodiscard]] inline VtableAndObject resolve(cs2::CCSGOInput* patternValue) noexcept
{
    const DynamicLibrary clientDLL{cs2::CLIENT_DLL};
    VtableAndObject result;
    if (!clientDLL) {
        StatusReport::record("CSGOInput: libclient not mapped at context build - input features disabled", false);
        return result;
    }

    const auto params = clientDLL.getVmtFinderParams();
    const auto vtable = typeinfo_vtable::findPrimaryVtable(params.rodataSection, params.dataRelRoSection, clientDLL.getCodeSection(), "CCSGOInput");
    if (!vtable) {
        StatusReport::record("CSGOInput: could not resolve the CCSGOInput vtable from the live binary - input features disabled", false);
        return result;
    }
    result.vtable = *vtable;

    
    if (patternValue) {
        std::uintptr_t vptr{};
        std::memcpy(&vptr, patternValue, sizeof(vptr));
        if (vptr == vtable->addressPoint) {
            result.object = patternValue;
            return result;
        }
    }

    
    const auto scan = scanForVptr(vtable->addressPoint);
    if (scan.hits == 1) {
        result.object = static_cast<cs2::CCSGOInput*>(reinterpret_cast<void*>(scan.address));
        StatusReport::record("CSGOInput: pattern value invalid (update moved the singleton to the heap) - recovered by vtable scan", true);
        if (patternValue)
            StatusReport::record("CSGOInput: the CSGOInputPointer pattern is stale and needs re-forging", false);
    } else {
        StatusReport::record(scan.hits == 0
            ? "CSGOInput: no heap instance carries the CCSGOInput vtable - input features disabled"
            : "CSGOInput: multiple vtable matches, refusing an ambiguous object - input features disabled", false);
    }
    return result;
}

} 




struct CSGOInputRuntimePointer {
    CSGOInputRuntimePointer(cs2::CCSGOInput* patternValue) noexcept
        : pointer{csgo_input_runtime::resolve(patternValue).object}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept { return pointer != nullptr; }
    [[nodiscard]] cs2::CCSGOInput* get() const noexcept { return pointer; }

private:
    cs2::CCSGOInput* pointer{nullptr};
};
