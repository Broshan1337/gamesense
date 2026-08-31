#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>

// World/entity ray trace, wrapping CS2's CGameTraceManager::TraceShape. This is the primitive autowall
// (min-damage) and collision-aware target extrapolation are built on. Full RE trail (how every address,
// struct offset and the filter byte layout were found and CONFIRMED against the real binary) is in the
// project's reference-cs2-trace-re note. All addresses are libclient.so module-relative, resolved off
// the client load base at runtime.
// The filter is NOT guessed: it is transcribed byte-for-byte from a real world-solid trace caller
// (sub_F107A0). The one subtle field is flags @ +0x37 = 1, which makes TraceShape take its simple
// single-result path and skip the heavy collision-group prep (sub_18DEAC0) - exactly what the game's own
// world trace does. Get that wrong and the trace either does far more work or returns nothing.
class Tracing {
public:
    struct Result {
        bool didHit;         // the ray hit something before reaching `end` (fraction < 1)
        float fraction;      // 0..1 of the way from start to end where it stopped (1 = clear)
        cs2::Vector endPos;  // where the ray stopped (== end if clear)
        cs2::Vector normal;  // surface normal at the hit point
        void* hitEntity;     // nullptr = world/nothing; otherwise the C_BaseEntity* that was hit
    };

    // A line trace from `start` to `end`. `skipEntity` (may be null) is excluded from the trace - pass the
    // local pawn for an eye->target visibility/penetration ray. `mask` is the CONTENTS mask of what counts
    // as solid (default = velocity's standard shot mask). Returns a clear Result (fraction 1, didHit false)
    // if the trace manager or functions are unavailable, so a resolve failure never reads as "blocked".
    [[nodiscard]] static Result traceLine(const cs2::Vector& start, const cs2::Vector& end, void* skipEntity = nullptr, std::uint64_t mask = kDefaultMask) noexcept
    {
        Result out{false, 1.0f, end, cs2::Vector{}, nullptr};

        const auto base = clientBase();
        if (!base)
            return out;

        void* const manager = managerPointer(base);
        if (!manager)
            return out;

        const auto traceShape = reinterpret_cast<TraceShapeFn>(base + kTraceShapeOffset);

        // Ray: a line trace is a fully-zeroed ray (mins/maxs 0, type @ +0x28 = 0).
        alignas(16) std::byte ray[kRaySize]{};

        // Filter: byte-exact transcription of sub_F107A0's world-solid filter.
        alignas(16) std::byte filter[kFilterSize]{};
        writeAt<std::uintptr_t>(filter, 0x00, base + kFilterVtableOffset); // CTraceFilter vtable
        writeAt<std::uint64_t>(filter, 0x08, mask);                        // contents mask
        // skip-handle array @ +0x20: all 0xFF = skip nothing. If a skip entity is given, put its packed
        // handle in the first slot and terminate the rest with 0xFFFFFFFF.
        std::memset(filter + 0x20, 0xFF, 0x10);
        if (skipEntity) {
            const auto toHandle = reinterpret_cast<EntityToHandleFn>(base + kEntityToHandleOffset);
            writeAt<std::uint32_t>(filter, 0x20, static_cast<std::uint32_t>(toHandle(skipEntity)));
        }
        // flags/collision region @ +0x30: 0x0100FFFF00000000 - the 0x01 at +0x37 selects the simple path.
        writeAt<std::uint64_t>(filter, 0x30, 0x0100FFFF00000000ull);

        // Result: zeroed buffer; TraceShape re-initialises it (sub_245E840) but we defensively set the
        // fraction default to 1.0 so a no-op still reads as "clear".
        alignas(16) std::byte result[kResultSize]{};
        writeAt<float>(result, kFractionOffset, 1.0f);

        traceShape(manager, ray, &start, &end, filter, result);

        out.fraction = readAt<float>(result, kFractionOffset);
        out.didHit = out.fraction < 1.0f;
        out.endPos = readAt<cs2::Vector>(result, kEndPosOffset);
        out.normal = readAt<cs2::Vector>(result, kNormalOffset);
        out.hitEntity = readAt<void*>(result, kHitEntityOffset);
        return out;
    }

    // A hull (box) sweep from `start` to `end` with the box `mins`/`maxs` (an OBB in local space). Same
    // filter/manager/result as traceLine; only the ray differs (type 2 + mins/maxs). This is what movement
    // prediction (target extrapolation) sweeps with - the player's collision box, not a thin line.
    [[nodiscard]] static Result traceHull(const cs2::Vector& start, const cs2::Vector& end, const cs2::Vector& mins, const cs2::Vector& maxs, void* skipEntity = nullptr, std::uint64_t mask = kDefaultMask) noexcept
    {
        Result out{false, 1.0f, end, cs2::Vector{}, nullptr};

        const auto base = clientBase();
        if (!base)
            return out;

        void* const manager = managerPointer(base);
        if (!manager)
            return out;

        const auto traceShape = reinterpret_cast<TraceShapeFn>(base + kTraceShapeOffset);

        // Ray: hull sweep = mins @ +0, maxs @ +12, type @ +0x28 = 2.
        alignas(16) std::byte ray[kRaySize]{};
        writeAt<cs2::Vector>(ray, 0x00, mins);
        writeAt<cs2::Vector>(ray, 0x0C, maxs);
        writeAt<std::uint8_t>(ray, 0x28, 2);

        alignas(16) std::byte filter[kFilterSize]{};
        writeAt<std::uintptr_t>(filter, 0x00, base + kFilterVtableOffset);
        writeAt<std::uint64_t>(filter, 0x08, mask);
        std::memset(filter + 0x20, 0xFF, 0x10);
        if (skipEntity) {
            const auto toHandle = reinterpret_cast<EntityToHandleFn>(base + kEntityToHandleOffset);
            writeAt<std::uint32_t>(filter, 0x20, static_cast<std::uint32_t>(toHandle(skipEntity)));
        }
        writeAt<std::uint64_t>(filter, 0x30, 0x0100FFFF00000000ull);

        alignas(16) std::byte result[kResultSize]{};
        writeAt<float>(result, kFractionOffset, 1.0f);

        traceShape(manager, ray, &start, &end, filter, result);

        out.fraction = readAt<float>(result, kFractionOffset);
        out.didHit = out.fraction < 1.0f;
        out.endPos = readAt<cs2::Vector>(result, kEndPosOffset);
        out.normal = readAt<cs2::Vector>(result, kNormalOffset);
        out.hitEntity = readAt<void*>(result, kHitEntityOffset);
        return out;
    }

    // velocity-cs2's is_visible (simplified): true if a ray from `start` reaches `end` (nearly) unobstructed,
    // skipping `skipEntity`. If `targetEntity` is given, hitting exactly it also counts as visible (the ray
    // reached the target). Use eye->bone with the local pawn as skip for a line-of-sight check.
    [[nodiscard]] static bool isVisible(const cs2::Vector& start, const cs2::Vector& end, void* skipEntity, void* targetEntity = nullptr) noexcept
    {
        const auto result = traceLine(start, end, skipEntity);
        return result.fraction > 0.97f || (targetEntity != nullptr && result.hitEntity == targetEntity);
    }

private:
    using TraceShapeFn = bool (*)(void* manager, void* ray, const cs2::Vector* start, const cs2::Vector* end, void* filter, void* result);
    using EntityToHandleFn = std::uint64_t (*)(void* entity);

    // ---------------------------------------------------------------------------
    // Runtime validation of every hardcoded module-relative address this module calls.
    // Null-checks alone cannot catch an update: a shifted .data qword reads as some unrelated
    // non-null value and we would CALL it (wild call -> SIGSEGV). So before the first use we
    // verify the function prologues still match the bytes recorded from the build these offsets
    // were measured on (0x45d14d8-extent libclient), and that data-derived pointers land inside
    // the module. On any mismatch the trace primitive fails CLOSED for the rest of the session -
    // consumers see "clear" results exactly like a resolve failure, and the reason shows up in
    // the [status] init report.
    // ---------------------------------------------------------------------------

    static constexpr int kValidationUnknown = 0;
    static constexpr int kValidationOk = 1;
    static constexpr int kValidationFailed = 2;

    static inline std::atomic<int> validationState{kValidationUnknown};

    // Function prologue signatures extracted from libclient.so at their recorded offsets
    // (TraceShape @ kTraceShapeOffset, entity->handle converter @ kEntityToHandleOffset).
    static constexpr std::array<std::uint8_t, 16> kTraceShapeSignature{
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x49, 0x89, 0xCF, 0x41, 0x56, 0x49, 0x89, 0xF6, 0x41, 0x55};
    static constexpr std::array<std::uint8_t, 16> kEntityToHandleSignature{
        0x48, 0x85, 0xFF, 0x74, 0x3B, 0x48, 0x8B, 0x57, 0x10, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x85};

public:
    // Upper bound of libclient.so mapped extent (file-backed + bss) on the measured build; used only to
    // sanity-check runtime pointer values read from .data against "plausible client-module address".
    static constexpr std::uintptr_t kMaxModuleExtent = 0x4C00000;

private:

    [[nodiscard]] static bool bytesMatch(std::uintptr_t address, const std::uint8_t* signature, std::size_t length) noexcept
    {
        return std::memcmp(reinterpret_cast<const void*>(address), signature, length) == 0;
    }

    [[nodiscard]] static bool runValidation(std::uintptr_t base) noexcept
    {
        const bool functionsIntact =
            bytesMatch(base + kTraceShapeOffset, kTraceShapeSignature.data(), kTraceShapeSignature.size())
            && bytesMatch(base + kEntityToHandleOffset, kEntityToHandleSignature.data(), kEntityToHandleSignature.size());
        if (!functionsIntact) {
            StatusReport::record("Tracing: function signatures drifted - traces fail closed", false);
            return false;
        }
        StatusReport::record("Tracing", true);
        return true;
    }

    [[nodiscard]] static bool validated(std::uintptr_t base) noexcept
    {
        auto state = validationState.load(std::memory_order_acquire);
        if (state == kValidationOk)
            return true;
        if (state == kValidationFailed)
            return false;
        state = runValidation(base) ? kValidationOk : kValidationFailed;
        validationState.store(state, std::memory_order_release);
        return state == kValidationOk;
    }

    [[nodiscard]] static void* managerPointer(std::uintptr_t base) noexcept
    {
        if (!validated(base))
            return nullptr;
        void* manager{};
        std::memcpy(&manager, reinterpret_cast<const void*>(base + kTraceManagerOffset), sizeof(manager));
        // Garbage guard: after an update this qword may hold any non-null value; require it to
        // point back into the client module like the real manager singleton does.
        const auto asAddress = reinterpret_cast<std::uintptr_t>(manager);
        if (!manager || asAddress < base || asAddress >= base + kMaxModuleExtent)
            return nullptr;
        return manager;
    }

    template <typename T>
    static void writeAt(std::byte* buffer, std::size_t offset, const T& value) noexcept
    {
        std::memcpy(buffer + offset, &value, sizeof(T));
    }

    template <typename T>
    [[nodiscard]] static T readAt(const std::byte* buffer, std::size_t offset) noexcept
    {
        T value{};
        std::memcpy(&value, buffer + offset, sizeof(T));
        return value;
    }

    [[nodiscard]] static std::uintptr_t clientBase() noexcept
    {
        // Cached: the module base is immutable after load, and constructing a DynamicLibrary +
        // walking its link map on EVERY trace call takes the loader lock on a hot path.
        // Benign race - all threads compute and store the same value.
        static std::atomic<std::uintptr_t> cachedBase{0};
        if (const auto cached = cachedBase.load(std::memory_order_relaxed))
            return cached;
        const DynamicLibrary client{cs2::CLIENT_DLL};
        const auto linkMap = client.getLinkMap();
        if (!linkMap)
            return 0;
        const auto base = static_cast<std::uintptr_t>(linkMap->l_addr);
        cachedBase.store(base, std::memory_order_relaxed);
        return base;
    }

    // libclient.so module-relative addresses. RE-DERIVED for the 2026-08-29 update by re-running the
    // fingerprinted-caller method on the live binary (movabs $0x700000000000000 + 0x4900 mask
    // immediates, caller at vaddr 0xf081xx - the same function every time): the two FUNCTIONS did not
    // move and their prologues still match, but BOTH data anchors drifted (the same Aug-29 data-layout
    // shift class as GlobalVars' interval_per_tick):
    //   filter vtable lea -> 0x42E5718 (was 0x434D718 - .rodata reorganized, -0x68000)
    //   entity->handle call -> 0x16BB560 (unchanged, prologue byte-identical)
    //   manager load *(rip)->0x45867D0 (was 0x45847D0, +0x2000 with the .data segment)
    //   TraceShape call -> 0x16BCD40 (unchanged)
    // The stale manager address is the worst failure mode: a shifted .data qword still reads some
    // non-null in-module value, validation passes, and every trace returns garbage - which read as
    // "everything blocked" and silently disabled WallCheck/Autowall autoshoot (measured in-game).
    static constexpr std::uintptr_t kTraceShapeOffset = 0x16BCD40;    // CGameTraceManager::TraceShape
    static constexpr std::uintptr_t kTraceManagerOffset = 0x45867D0;  // qword holding the manager pointer
    static constexpr std::uintptr_t kFilterVtableOffset = 0x42E5718;  // generic CTraceFilter vtable
    static constexpr std::uintptr_t kEntityToHandleOffset = 0x16BB560; // C_BaseEntity* -> packed handle

    // velocity's standard shot/solid contents mask.
    static constexpr std::uint64_t kDefaultMask = 0x1C3003;

    static constexpr std::size_t kRaySize = 48;
    static constexpr std::size_t kFilterSize = 0x50;
    static constexpr std::size_t kResultSize = 0x140;

    // GameTrace (result) field offsets, confirmed against the binary.
    static constexpr std::size_t kHitEntityOffset = 0x08;
    static constexpr std::size_t kEndPosOffset = 0x84;   // 132
    static constexpr std::size_t kNormalOffset = 0x90;   // 144
    static constexpr std::size_t kFractionOffset = 0xAC; // 172
};
