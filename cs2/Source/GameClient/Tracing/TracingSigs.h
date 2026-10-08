#pragma once














#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

namespace tracing_sigs
{



inline constexpr std::uint64_t kMaxModuleExtent = 0x4C00000;












inline constexpr const char* kEntityToHandleSig =
    "48 85 FF 74 3B 48 8B 57 10 B8 FF FF FF FF 48 85";




inline constexpr const char* kCallerSig =
    "48 B8 ? ? ? ? ? ? ? ? 48 89 45 80 E8 ? ? ? ? BA 0F 03 00 00 31 FF 48 B8 ? ? ? ? ? ? ? ? "
    "48 89 85 00 FF FF FF B8 00 49 00 00 66 0F EF C0 66 89 85 08 FF FF FF 48 8D 05 ? ? ? ? "
    "66 89 95 07 FF FF FF 0F 29 85 E0 FE FF FF 66 0F 76 C0 48 89 85 D0 FE FF FF 0F 29 85 F0 FE FF FF "
    "C6 85 0A FF FF FF 00 48 C7 85 D8 FE FF FF 42 00 00 00 E8 ? ? ? ? 31 FF 89 85 F0 FE FF FF "
    "E8 ? ? ? ? 48 8D 4B 0C 4D 89 E1 48 89 DA 66 89 85 00 FF FF FF 48 8D 05 ? ? ? ?";
inline constexpr std::size_t kCallerVtableDispOffset = 62;
inline constexpr std::size_t kCallerManagerDispOffset = 154;
inline constexpr std::size_t kCallerEntityToHandleCallOffset = 74;


inline constexpr std::size_t kCallerTraceCallOffset = 0xF3;

inline constexpr const char* kFfReaderSig =
    "48 8D 1D ? ? ? ? BE FF FF FF FF 4C 89 8D 48 FE FF FF 48 89 DF E8 ? ? ? ?";
inline constexpr std::size_t kFfReaderObjectDispOffset = 3;
inline constexpr std::size_t kFfReaderGetterCallOffset = 22;

inline constexpr const char* kFfGetterSig =
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 E8 5F 61 BE 00 48 85 C0";


inline constexpr std::uint64_t kRvaTraceShape = 0x16F69C0;
inline constexpr std::uint64_t kRvaEntityToHandle = 0x16C1AE0;
inline constexpr std::uint64_t kRvaManagerQword = 0x458BB70;
inline constexpr std::uint64_t kRvaFilterVtable = 0x42EAF38;

struct Anchors {
    bool ok = false;    
    bool ffOk = false;  
    std::uint64_t moduleBase = 0;
    std::uint64_t traceShape = 0;
    std::uint64_t entityToHandle = 0;
    std::uint64_t managerQword = 0;
    std::uint64_t filterVtable = 0;
    std::uint64_t ffGetter = 0;
    std::uint64_t ffObject = 0;
    
    
    
    int failStep = 0;   
};

namespace detail
{


struct ParsedSig {
    std::array<unsigned char, 160> values{};
    std::array<unsigned char, 160> mask{}; 
    std::size_t length = 0;
};

inline ParsedSig parse(const char* pattern) noexcept
{
    ParsedSig out{};
    const char* p = pattern;
    while (*p != '\0' && out.length < out.values.size()) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (*p == '?') {
            out.values[out.length] = 0;
            out.mask[out.length] = 0;
            ++out.length;
            ++p;
            if (*p == '?')
                ++p;
            continue;
        }
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            const char lower = c | 32;
            if (lower >= 'a' && lower <= 'f')
                return lower - 'a' + 10;
            return -1;
        };
        const int high = nibble(*p);
        ++p;
        if (high < 0)
            return {};
        int low = 0;
        if (*p != ' ' && *p != '\0') {
            low = nibble(*p);
            if (low < 0)
                return {};
            ++p;
        }
        out.values[out.length] = static_cast<unsigned char>((high << 4) | low);
        out.mask[out.length] = 1;
        ++out.length;
    }
    return out;
}



inline const std::byte* scan(std::span<const std::byte> haystack, const ParsedSig& sig) noexcept
{
    if (haystack.size() < sig.length || sig.length == 0)
        return nullptr;
    const std::byte* begin = haystack.data();
    const std::size_t end = haystack.size() - sig.length;
    const unsigned char first = sig.values[0];
    const bool firstFixed = sig.mask[0] != 0;
    for (std::size_t i = 0; i <= end; ++i) {
        if (firstFixed && begin[i] != std::byte{first})
            continue;
        bool ok = true;
        for (std::size_t k = 1; k < sig.length; ++k) {
            if (sig.mask[k] && begin[i + k] != std::byte{sig.values[k]}) {
                ok = false;
                break;
            }
        }
        if (ok)
            return begin + i;
    }
    return nullptr;
}

inline std::int32_t readDisp32(const std::byte* at) noexcept
{
    std::int32_t value{};
    std::memcpy(&value, at, sizeof(value));
    return value;
}

} 

inline const Anchors& resolved() noexcept
{
    static Anchors anchors;
    static std::atomic<int> state{0}; 
    if (state.load(std::memory_order_acquire) != 0)
        return anchors;
    if (state.exchange(1, std::memory_order_acq_rel) != 0)
        return anchors; 

    const LinuxDynamicLibrary client{cs2::CLIENT_DLL};
    if (!client) {
        anchors.failStep = 1; 
        return anchors; 
    }
    const auto code = client.getCodeSection();
    const std::span<const std::byte> haystack = code.raw();
    const link_map* const map = client.getLinkMap();
    if (haystack.empty() || !map || !map->l_addr) {
        anchors.failStep = 2 + (haystack.empty() ? 0 : 1); 
        return anchors;
    }
    const std::uint64_t base = static_cast<std::uint64_t>(map->l_addr);
    anchors.moduleBase = base;
    const auto inModule = [base](std::uint64_t address) {
        return address >= base && address < base + kMaxModuleExtent;
    };

    
    
    
    if (const auto* match = detail::scan(haystack, detail::parse(kEntityToHandleSig)))
        anchors.entityToHandle = reinterpret_cast<std::uint64_t>(match);

    
    
    if (const auto* callerMatch = detail::scan(haystack, detail::parse(kCallerSig))) {
        const auto matchAddress = reinterpret_cast<std::uint64_t>(callerMatch);
        const auto readDisp = [callerMatch](std::size_t offset) {
            std::int32_t value{};
            std::memcpy(&value, callerMatch + offset, sizeof(value));
            return value;
        };
        anchors.filterVtable = matchAddress + kCallerVtableDispOffset + 4 + readDisp(kCallerVtableDispOffset);
        anchors.managerQword = matchAddress + kCallerManagerDispOffset + 4 + readDisp(kCallerManagerDispOffset);
        anchors.traceShape = matchAddress + kCallerTraceCallOffset + 5 + readDisp(kCallerTraceCallOffset + 1);
    }

    
    
    if (const auto* getterMatch = detail::scan(haystack, detail::parse(kFfGetterSig))) {
        const auto getterAddress = reinterpret_cast<std::uint64_t>(getterMatch);
        if (const auto* readerMatch = detail::scan(haystack, detail::parse(kFfReaderSig))) {
            const auto readerAddress = reinterpret_cast<std::uint64_t>(readerMatch);
            std::int32_t callDisp{};
            std::memcpy(&callDisp, readerMatch + kFfReaderGetterCallOffset + 1, sizeof(callDisp));
            if (readerAddress + kFfReaderGetterCallOffset + 5 + callDisp == getterAddress) {
                std::int32_t objDisp{};
                std::memcpy(&objDisp, readerMatch + kFfReaderObjectDispOffset, sizeof(objDisp));
                anchors.ffObject = readerAddress + kFfReaderObjectDispOffset + 4 + objDisp;
                anchors.ffGetter = getterAddress;
            }
        }
    }

    
    const auto fallback = [&base](std::uint64_t& target, std::uint64_t rva) {
        if (target == 0)
            target = base + rva;
    };
    fallback(anchors.traceShape, kRvaTraceShape);
    fallback(anchors.entityToHandle, kRvaEntityToHandle);
    fallback(anchors.filterVtable, kRvaFilterVtable);
    fallback(anchors.managerQword, kRvaManagerQword);

    
    
    
    
    
    
    
    static constexpr std::array<unsigned char, 4> kTraceShapeLeaPrefix{0x55, 0x48, 0x8D, 0x05};
    static constexpr std::array<unsigned char, 3> kTraceShapeAfterDisp{0x48, 0x89, 0xE5};
    static constexpr std::array<unsigned char, 4> kTestRdiPrologue{0x48, 0x85, 0xFF, 0x74};
    const auto prologueOk = [&](std::uint64_t address, const std::array<unsigned char, 4>& expected) {
        return inModule(address)
            && std::memcmp(reinterpret_cast<const void*>(address), expected.data(), 4) == 0;
    };
    const auto traceShapeOk = [&](std::uint64_t address) {
        return inModule(address)
            && std::memcmp(reinterpret_cast<const void*>(address), kTraceShapeLeaPrefix.data(), 4) == 0
            && std::memcmp(reinterpret_cast<const void*>(address + 8), kTraceShapeAfterDisp.data(), 3) == 0;
    };
    const bool functionsOk = traceShapeOk(anchors.traceShape)
        && prologueOk(anchors.entityToHandle, kTestRdiPrologue);
    const bool dataOk = inModule(anchors.filterVtable) && inModule(anchors.managerQword);
    anchors.ok = functionsOk && dataOk;
    if (!functionsOk)
        anchors.failStep = 4;
    else if (!dataOk)
        anchors.failStep = 5;

    
    if (!inModule(anchors.ffGetter))
        anchors.ffGetter = 0;
    if (!inModule(anchors.ffObject))
        anchors.ffObject = 0;
    return anchors;
}

} 