#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <GameClient/Tracing/TracingSigs.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>

// World/entity ray trace, wrapping CS2's CGameTraceManager::TraceShape. This is the primitive autowall
// (min-damage) and collision-aware target extrapolation are built on. Full RE trail (how every
// struct offset and the filter byte layout were found and CONFIRMED against the real binary) is in the
// project's reference-cs2-trace-re note. All anchors are resolved at runtime through the signatures
// in TracingSigs.h (the Sep-10 update stale-d every recorded RVA, which is why this file no longer
// trusts raw offsets): pattern match first, last-known RVA fallback second, fail-closed on top.
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

    // Diagnostic snapshot of the runtime anchor state (the [tbtrc] lines). Addresses are
    // module-relative (X - moduleBase) so a paste can be diffed against the offline-verified
    // RVAs directly.
    struct DebugState {
        bool anchorsOk;
        int failStep;                 // tracing_sigs resolution stop-point (0 = fully resolved)
        std::uintptr_t moduleBase;
        std::uintptr_t traceShape;    // absolute (0 when unresolved)
        std::uintptr_t entityToHandle;
        std::uintptr_t managerQword;  // address OF the manager-pointer qword
        std::uintptr_t filterVtable;
        void* manager;                // live deref after the in-module guard
        bool managerGuardRejected;    // non-null but outside the module range
        void* managerSubObject;       // *(manager) - TraceShape bails when this is null
        std::uint8_t readinessByte;   // TraceShape's top-of-function query gate
    };

    [[nodiscard]] static DebugState debugState() noexcept
    {
        DebugState out{};
        // Bypass the validation latch on purpose: this probe must see the RAW resolution state
        // (the latch is one-shot, so once it fails every later read looks like the defaults and
        // the actual failing step stays invisible). This call is free after the first scan.
        const auto& anchors = tracing_sigs::resolved();
        out.anchorsOk = anchors.ok;
        out.failStep = anchors.failStep;
        out.moduleBase = static_cast<std::uintptr_t>(anchors.moduleBase);
        out.traceShape = static_cast<std::uintptr_t>(anchors.traceShape);
        out.entityToHandle = static_cast<std::uintptr_t>(anchors.entityToHandle);
        out.managerQword = static_cast<std::uintptr_t>(anchors.managerQword);
        out.filterVtable = static_cast<std::uintptr_t>(anchors.filterVtable);

        if (anchors.managerQword != 0) {
            void* manager{};
            std::memcpy(&manager, reinterpret_cast<const void*>(anchors.managerQword), sizeof(manager));
            const auto asAddress = reinterpret_cast<std::uintptr_t>(manager);
            const auto moduleBase = static_cast<std::uintptr_t>(anchors.moduleBase);
            if (!manager)
                out.manager = nullptr;
            else if (asAddress < moduleBase || asAddress >= moduleBase + tracing_sigs::kMaxModuleExtent)
                out.managerGuardRejected = true;
            else {
                out.manager = manager;
                std::memcpy(&out.managerSubObject, manager, sizeof(out.managerSubObject));
            }
        }
        // TraceShape's own readiness gate (byte read at its top; verified 0x480D590 on 14181)
        if (out.moduleBase != 0)
            out.readinessByte = *reinterpret_cast<const volatile std::uint8_t*>(out.moduleBase + 0x480D590);
        return out;
    }

    // A line trace from `start` to `end`. `skipEntity` (may be null) is excluded from the trace - pass the
    // local pawn for an eye->target visibility/penetration ray. `mask` is the CONTENTS mask of what counts
    // as solid (default = velocity's standard shot mask). Returns a clear Result (fraction 1, didHit false)
    // if the trace manager or functions are unavailable, so a resolve failure never reads as "blocked".
    [[nodiscard]] static Result traceLine(const cs2::Vector& start, const cs2::Vector& end, void* skipEntity = nullptr, std::uint64_t mask = kDefaultMask) noexcept
    {
        Result out{false, 1.0f, end, cs2::Vector{}, nullptr};

        const auto anchors = resolvedAnchors();
        if (!anchors)
            return out;

        void* const manager = managerPointer();
        if (!manager)
            return out;

        const auto traceShape = reinterpret_cast<TraceShapeFn>(anchors->traceShape);

        // Ray: a line trace is a fully-zeroed ray (mins/maxs 0, type @ +0x28 = 0).
        alignas(16) std::byte ray[kRaySize]{};

        // Filter: byte-exact transcription of sub_F107A0's world-solid filter.
        alignas(16) std::byte filter[kFilterSize]{};
        writeAt<std::uintptr_t>(filter, 0x00, anchors->filterVtable); // CTraceFilter vtable
        writeAt<std::uint64_t>(filter, 0x08, mask);                        // contents mask
        // skip-handle array @ +0x20: all 0xFF = skip nothing. If a skip entity is given, put its packed
        // handle in the first slot and terminate the rest with 0xFFFFFFFF.
        std::memset(filter + 0x20, 0xFF, 0x10);
        if (skipEntity) {
            const auto toHandle = reinterpret_cast<EntityToHandleFn>(anchors->entityToHandle);
            writeAt<std::uint32_t>(filter, 0x20, static_cast<std::uint32_t>(toHandle(skipEntity)));
        }
        // flags/collision region @ +0x30: BYTE-EXACT replica of the 14181 game caller's final
        // filter state (decoded write-by-write, ORDER MATTERS): dword+0x30 = 0, word+0x34 =
        // FFFF, byte+0x37 = 0x0F (TraceShape branches on this: != 1 -> it runs the collision-
        // attribute prep itself), byte+0x38 = 0x03 (from edx=0x30f), byte+0x39 = 0x49 (from the
        // 0x4900 word write, whose low half is later overwritten by the +0x37 word), +0x3A = 0.
        // The 14177-era transcription (0x01 at +0x37) made every trace return untouched results
        // (fraction 1 = "no hit") on 14181 - the triggerbot's "confirm trace misses everything".
        writeAt<std::uint64_t>(filter, 0x30, 0x0000FFFF00000000ull);
        writeAt<std::uint16_t>(filter, 0x37, 0x030F);
        writeAt<std::uint8_t>(filter, 0x39, 0x49);

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

        const auto anchors = resolvedAnchors();
        if (!anchors)
            return out;

        void* const manager = managerPointer();
        if (!manager)
            return out;

        const auto traceShape = reinterpret_cast<TraceShapeFn>(anchors->traceShape);

        // Ray: hull sweep = mins @ +0, maxs @ +12, type @ +0x28 = 2.
        alignas(16) std::byte ray[kRaySize]{};
        writeAt<cs2::Vector>(ray, 0x00, mins);
        writeAt<cs2::Vector>(ray, 0x0C, maxs);
        writeAt<std::uint8_t>(ray, 0x28, 2);

        alignas(16) std::byte filter[kFilterSize]{};
        writeAt<std::uintptr_t>(filter, 0x00, anchors->filterVtable);
        writeAt<std::uint64_t>(filter, 0x08, mask);
        std::memset(filter + 0x20, 0xFF, 0x10);
        if (skipEntity) {
            const auto toHandle = reinterpret_cast<EntityToHandleFn>(anchors->entityToHandle);
            writeAt<std::uint32_t>(filter, 0x20, static_cast<std::uint32_t>(toHandle(skipEntity)));
        }
        // Same byte-exact 14181 flags replica as traceLine above.
        writeAt<std::uint64_t>(filter, 0x30, 0x0000FFFF00000000ull);
        writeAt<std::uint16_t>(filter, 0x37, 0x030F);
        writeAt<std::uint8_t>(filter, 0x39, 0x49);

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
    // Anchor resolution + validation (see TracingSigs.h). One-shot, cached; the previous
    // generation validated hardcoded RVAs against recorded prologue bytes - the same idea, but
    // the addresses now come from signatures with RVA fallbacks.
    // ---------------------------------------------------------------------------

    static constexpr int kValidationUnknown = 0;
    static constexpr int kValidationOk = 1;
    static constexpr int kValidationFailed = 2;

    static inline std::atomic<int> validationState{kValidationUnknown};

    // Sanity: TraceShape must start with its recorded prologue (the signature contains the same
    // bytes; this re-check catches a resolve that landed on a coincidental sibling).
    static constexpr std::array<std::uint8_t, 16> kTraceShapeSignature{
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x49, 0x89, 0xCF, 0x41, 0x56, 0x49, 0x89, 0xF6, 0x41, 0x55};
    static constexpr std::array<std::uint8_t, 16> kEntityToHandleSignature{
        0x48, 0x85, 0xFF, 0x74, 0x3B, 0x48, 0x8B, 0x57, 0x10, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x85};

    [[nodiscard]] static bool bytesMatch(std::uintptr_t address, const std::uint8_t* signature, std::size_t length) noexcept
    {
        return std::memcmp(reinterpret_cast<const void*>(address), signature, length) == 0;
    }

    [[nodiscard]] static bool runValidation(const tracing_sigs::Anchors& anchors) noexcept
    {
        static constexpr std::array<std::uint8_t, 4> kPrologue{0x55, 0x48, 0x89, 0xE5};
        const bool intact = std::memcmp(reinterpret_cast<const void*>(anchors.traceShape), kTraceShapeSignature.data(), kTraceShapeSignature.size()) == 0
            && std::memcmp(reinterpret_cast<const void*>(anchors.entityToHandle), kEntityToHandleSignature.data(), kEntityToHandleSignature.size()) == 0;
        if (!intact) {
            StatusReport::record("Tracing: signatures drifted - traces fail closed", false);
            return false;
        }
        StatusReport::record("Tracing", true);
        return true;
    }

    [[nodiscard]] static const tracing_sigs::Anchors* resolvedAnchors() noexcept
    {
        auto state = validationState.load(std::memory_order_acquire);
        if (state == kValidationOk)
            return &tracing_sigs::resolved();
        if (state == kValidationFailed)
            return nullptr;
        const auto& anchors = tracing_sigs::resolved();
        state = anchors.ok && runValidation(anchors) ? kValidationOk : kValidationFailed;
        validationState.store(state, std::memory_order_release);
        return state == kValidationOk ? &anchors : nullptr;
    }

    [[nodiscard]] static void* managerPointer() noexcept
    {
        const auto* anchors = resolvedAnchors();
        if (!anchors || anchors->managerQword == 0)
            return nullptr;
        void* manager{};
        std::memcpy(&manager, reinterpret_cast<const void*>(anchors->managerQword), sizeof(manager));
        // Garbage guard: after an update this qword may hold any non-null value; require it to
        // point back into the client module like the real manager singleton does. The module base
        // is recovered from the resolved anchors: they are absolute runtime addresses inside it.
        const std::uintptr_t moduleBase = static_cast<std::uintptr_t>(anchors->moduleBase);
        const auto asAddress = reinterpret_cast<std::uintptr_t>(manager);
        if (!manager || asAddress < moduleBase || asAddress >= moduleBase + tracing_sigs::kMaxModuleExtent)
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