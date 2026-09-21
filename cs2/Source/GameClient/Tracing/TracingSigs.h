#pragma once

// Runtime signature resolution for the libclient.so anchors that used to be hardcoded RVAs.
// Rationale: a CS2 update recompiles and relocates the module, so hardcoded RVAs go stale every
// time (the Sep-10 update broke ALL four recorded Tracing/Autowall RVAs again). These signatures
// wildcard every rip-relative displacement/imm64 and match on the surrounding register-movement
// structure instead, so they survive relocation. Each signature was verified exactly-once against
// the shipped module's .text with the scratchpad byte toolkit, and each resolution was
// cross-checked (the caller pattern's EntityToHandle call lands on the independently matched
// EntityToHandle prologue; all 21 fingerprinted trace callers agree on the same TraceShape).
//
// A resolution failure degrades gracefully: every anchor falls back to its last-known RVA, and
// only when THAT fails validation too do the consumers fail closed (features disable themselves,
// nothing ever points at garbage).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

namespace tracing_sigs
{

// Plausible libclient.so mapped extent (file-backed + bss) - used to sanity-check resolved
// pointers the same way the game's own manager singletons are checked.
inline constexpr std::uint64_t kMaxModuleExtent = 0x4C00000;

// --- signatures (offsets are byte offsets from the match start) ------------------------------

// CGameTraceManager::TraceShape: prologue + register shuffle (unique across .text)
inline constexpr const char* kTraceShapeSig =
    "55 48 89 E5 41 57 49 89 CF 41 56 49 89 F6 41 55 4D 89 C5";
// C_BaseEntity* -> packed entity handle converter: prologue (unique)
inline constexpr const char* kEntityToHandleSig =
    "48 85 FF 74 3B 48 8B 57 10 B8 FF FF FF FF 48 85";
// The fingerprinted trace-caller block. Captures (byte offsets from match start):
//   +62  lea disp32 -> generic CTraceFilter VTABLE (.data.rel.ro)
//   +154 lea disp32 -> qword holding the CGameTraceManager pointer (.bss)
//   +74  call rel32 -> EntityToHandle (cross-validation against the prologue match)
inline constexpr const char* kCallerSig =
    "48 B8 ? ? ? ? ? ? ? ? 48 89 45 80 E8 ? ? ? ? BA 0F 03 00 00 31 FF 48 B8 ? ? ? ? ? ? ? ? "
    "48 89 85 00 FF FF FF B8 00 49 00 00 66 0F EF C0 66 89 85 08 FF FF FF 48 8D 05 ? ? ? ? "
    "66 89 95 07 FF FF FF 0F 29 85 E0 FE FF FF 66 0F 76 C0 48 89 85 D0 FE FF FF 0F 29 85 F0 FE FF FF "
    "C6 85 0A FF FF FF 00 48 C7 85 D8 FE FF FF 42 00 00 00 E8 ? ? ? ? 31 FF 89 85 F0 FE FF FF "
    "E8 ? ? ? ? 48 8D 4B 0C 4D 89 E1 48 89 DA 66 89 85 00 FF FF FF 48 8D 05 ? ? ? ?";
inline constexpr std::size_t kCallerVtableDispOffset = 62;
inline constexpr std::size_t kCallerManagerDispOffset = 154;
inline constexpr std::size_t kCallerEntityToHandleCallOffset = 74;
// ff_damage_bullet_penetration READER: lea rbx,[obj] / mov esi,-1 / mov [rbp-1B8h],r9 / mov rdi,rbx / call
inline constexpr const char* kFfReaderSig =
    "48 8D 1D ? ? ? ? BE FF FF FF FF 4C 89 8D 48 FE FF FF 48 89 DF E8 ? ? ? ?";
inline constexpr std::size_t kFfReaderObjectDispOffset = 3;
inline constexpr std::size_t kFfReaderGetterCallOffset = 22;
// The getter itself (validated = the reader's call target)
inline constexpr const char* kFfGetterSig =
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 E8 5F 61 BE 00 48 85 C0";

// Legacy RVAs (2026-09-10 build) - per-anchor fallback when a signature drifts.
inline constexpr std::uint64_t kRvaTraceShape = 0x16C32C0;
inline constexpr std::uint64_t kRvaEntityToHandle = 0x16C1AE0;
inline constexpr std::uint64_t kRvaManagerQword = 0x458BB70;
inline constexpr std::uint64_t kRvaFilterVtable = 0x42EAF38;

struct Anchors {
    bool ok = false;    // trace primitive usable
    bool ffOk = false;  // autowall inputs usable (getter + cvar object address)
    std::uint64_t moduleBase = 0;
    std::uint64_t traceShape = 0;
    std::uint64_t entityToHandle = 0;
    std::uint64_t managerQword = 0;
    std::uint64_t filterVtable = 0;
    std::uint64_t ffGetter = 0;
    std::uint64_t ffObject = 0;
    // Where resolution stopped (the 2026-09-13 runtime-vs-inject mismatch probe: the SAME
    // resolution works during global-context init but fails when called from a hook, and the
    // failing step was invisible). 0 = fully resolved.
    int failStep = 0;   // 1 client not found, 2 empty code section, 3 no link map, 4 functions missing, 5 data missing
};

namespace detail
{

// One parsed signature: byte values with a fixed/wildcard mask.
struct ParsedSig {
    std::array<unsigned char, 160> values{};
    std::array<unsigned char, 160> mask{}; // 1 = fixed, 0 = wildcard
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

// First match of the (value, mask) signature inside the haystack, or nullptr. Scans the LIVE
// module image (the spans handed in are runtime-mapped .text), so results are usable addresses.
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

} // namespace detail

inline const Anchors& resolved() noexcept
{
    static Anchors anchors;
    static std::atomic<int> state{0}; // 0 = not resolved, 1 = resolved
    if (state.load(std::memory_order_acquire) != 0)
        return anchors;
    if (state.exchange(1, std::memory_order_acq_rel) != 0)
        return anchors; // another thread won the race and is finalizing the result

    const LinuxDynamicLibrary client{cs2::CLIENT_DLL};
    if (!client) {
        anchors.failStep = 1; // dlopen(RTLD_NOLOAD, "libclient.so") failed at runtime
        return anchors; // fail closed - nothing resolved
    }
    const auto code = client.getCodeSection();
    const std::span<const std::byte> haystack = code.raw();
    const link_map* const map = client.getLinkMap();
    if (haystack.empty() || !map || !map->l_addr) {
        anchors.failStep = 2 + (haystack.empty() ? 0 : 1); // 2 empty code section, 3 no link map
        return anchors;
    }
    const std::uint64_t base = static_cast<std::uint64_t>(map->l_addr);
    anchors.moduleBase = base;
    const auto inModule = [base](std::uint64_t address) {
        return address >= base && address < base + kMaxModuleExtent;
    };

    // 1. Functions by their own prologue patterns.
    if (const auto* match = detail::scan(haystack, detail::parse(kTraceShapeSig)))
        anchors.traceShape = reinterpret_cast<std::uint64_t>(match);
    if (const auto* match = detail::scan(haystack, detail::parse(kEntityToHandleSig)))
        anchors.entityToHandle = reinterpret_cast<std::uint64_t>(match);

    // 2. The fingerprinted trace-caller block: filter vtable + manager qword disp32 captures.
    if (const auto* callerMatch = detail::scan(haystack, detail::parse(kCallerSig))) {
        const auto matchAddress = reinterpret_cast<std::uint64_t>(callerMatch);
        const auto readDisp = [callerMatch](std::size_t offset) {
            std::int32_t value{};
            std::memcpy(&value, callerMatch + offset, sizeof(value));
            return value;
        };
        anchors.filterVtable = matchAddress + kCallerVtableDispOffset + 4 + readDisp(kCallerVtableDispOffset);
        anchors.managerQword = matchAddress + kCallerManagerDispOffset + 4 + readDisp(kCallerManagerDispOffset);
    }

    // 3. ff_damage_bullet_penetration: getter by its own sig, cvar object from the reader's lea
    //    disp32 (the reader's call target must BE the getter - ties the two matches together).
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

    // Per-anchor fallback to the recorded RVAs (validated below before use).
    const auto fallback = [&base](std::uint64_t& target, std::uint64_t rva) {
        if (target == 0)
            target = base + rva;
    };
    fallback(anchors.traceShape, kRvaTraceShape);
    fallback(anchors.entityToHandle, kRvaEntityToHandle);
    fallback(anchors.filterVtable, kRvaFilterVtable);
    fallback(anchors.managerQword, kRvaManagerQword);

    // Validation: functions must start with their OWN recorded prologues and data anchors must
    // point in-module. NOTE: EntityToHandle does NOT start with push rbp/mov rbp,rsp (it opens
    // with test rdi,rdi / je) - a generic 55-48-89-E5 check on it fails forever and latched the
    // whole trace primitive fail-closed (the 2026-09-13 "confirms trace misses everything" root
    // cause, found via the [tba] ok=0 step=4 probe).
    static constexpr std::array<unsigned char, 4> kRbpPrologue{0x55, 0x48, 0x89, 0xE5};
    static constexpr std::array<unsigned char, 4> kTestRdiPrologue{0x48, 0x85, 0xFF, 0x74};
    const auto prologueOk = [&](std::uint64_t address, const std::array<unsigned char, 4>& expected) {
        return inModule(address)
            && std::memcmp(reinterpret_cast<const void*>(address), expected.data(), 4) == 0;
    };
    const bool functionsOk = prologueOk(anchors.traceShape, kRbpPrologue)
        && prologueOk(anchors.entityToHandle, kTestRdiPrologue);
    const bool dataOk = inModule(anchors.filterVtable) && inModule(anchors.managerQword);
    anchors.ok = functionsOk && dataOk;
    if (!functionsOk)
        anchors.failStep = 4;
    else if (!dataOk)
        anchors.failStep = 5;

    // ff inputs are optional (Autowall-only); they only count when they point in-module.
    if (!inModule(anchors.ffGetter))
        anchors.ffGetter = 0;
    if (!inModule(anchors.ffObject))
        anchors.ffObject = 0;
    return anchors;
}

} // namespace tracing_sigs