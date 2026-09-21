#pragma once

// Pure math of the FVA view-angle emitter, split out of Features/Game/FvaEmulator.h so it can be
// unit-tested without touching any game memory.
//
// Philosophy of the thing being ported (FORFUTURETESTS/mytest - the FVA reconstruction):
// instead of snapping the server's belief about your view from angle A to angle B in one tick,
// the wire is made to describe a SMOOTH rotation chain A -> f1(A,B) -> f2(A,B) -> ... -> B spread
// over subtick fractions of a single tick. Anti-cheat rewind logic that walks those entries
// backwards then re-traces shots against angles that never actually existed as a rendered view.
//
// Everything here is dependency-free on purpose: this project links -nostdlib and has no libm,
// which also rules out std::remainderf(dx, 360) - the exact function the reference used to fold
// angle deltas. fold180() below reproduces remainderf's contract (result interval AND endpoint
// parity) with truncation arithmetic only.

namespace fva_math
{

inline constexpr float kFullCircle = 360.0f;
inline constexpr float kHalfCircle = 180.0f;

// The interpolation window the reference stamps into every emitted entry, expressed as
// when(fraction) = kWhenBase + fraction * kWhenSpan. Both constants are lifted verbatim from the
// decompiled emitter data pool, so a capture diff against the Windows original stays meaningful.
inline constexpr float kWhenBase = 0.023590f;
inline constexpr float kWhenSpan = 0.908453f;

// Fold `angle` into (-180, 180], matching the reference's
//     m = std::remainderf(dx, 360);
//     if (m > 180) m -= 360; else if (m < -180) m += 360;
// without libm. Trunc-toward-zero division folds once into (-360, 360), the second branch maps
// it onto [-180, 180], and the final nudge restores the reference's asymmetric interval:
// exactly -180 reads back as +180 while exactly +180 stays +180.
[[nodiscard]] inline float fold180(float delta) noexcept
{
    if (delta != delta) // NaN: nothing sane to interpolate towards, treat as zero rotation.
        return 0.0f;

    auto v = delta - kFullCircle * static_cast<float>(static_cast<int>(delta / kFullCircle));
    if (v > kHalfCircle)
        v -= kFullCircle;
    else if (v < -kHalfCircle)
        v += kFullCircle;

    if (v == -kHalfCircle)
        v = kHalfCircle;
    return v;
}

// Reference-cache semantics: the emitter must remember what angle CHAIN it published last tick so
// today's chain starts where yesterday's ended instead of teleporting between chains. A reference
// invalidated by anything else than a plain wrap (map change / teleport spin larger than half a
// circle against a stale cache) snaps rather than interpolates through fantasy angles, so the
// caller resets the cache when the fold cannot produce a sane short path. NaN samples count as
// discontinuities - snapping is the safe answer to corrupt state.
[[nodiscard]] inline bool isDiscontinuity(float previousAngle, float currentAngle) noexcept
{
    if (previousAngle != previousAngle || currentAngle != currentAngle)
        return true;
    const float d = fold180(currentAngle - previousAngle);
    return !(d < kHalfCircle && d > -kHalfCircle);
}

// Number of chain entries this tick given the requested step count and how much head-room the
// history still has. The reference caps at its fixed Rep capacity and skips silently when full -
// we never invent head-room at this layer; allocation decisions live in the field wrapper.
[[nodiscard]] inline int plannedEntries(int requested, int freeSlots) noexcept
{
    if (requested <= 0 || freeSlots <= 0)
        return 0;
    return requested < freeSlots ? requested : freeSlots;
}

// Interpolated angle for chain entry `index` (0-based) out of `count`, walking from `from`
// towards `to` following the shortest wrapped path. fraction(count,index) spans (0..1] exactly
// like the reference loop - entry i carries angle from + ((i+1)/count)*delta, so the LAST entry
// lands precisely on `to` and no entry ever overshoots it even for paths that cross +-180.
// fold180 happens BEFORE scaling so a -179 -> +179 turn takes the 2-degree hop, not the 358 one.
[[nodiscard]] inline float interpolated(float from, float to, int index, int count) noexcept
{
    if (count <= 0 || index >= count)
        return to;
    if (index < 0)
        index = 0;
    const float fraction = static_cast<float>(index + 1) / static_cast<float>(count);
    return from + fraction * fold180(to - from);
}

// Subtick timestamp of chain entry `index` out of `count` - same shape guarantee as the angles:
// strictly increasing across entries, last one pinned at when(1.0).
[[nodiscard]] inline float whenAt(int index, int count) noexcept
{
    if (count <= 0)
        return kWhenBase;
    if (index >= count)
        index = count - 1;
    if (index < 0)
        index = 0;
    const float fraction = static_cast<float>(index + 1) / static_cast<float>(count);
    return kWhenBase + fraction * kWhenSpan;
}

// Sub-degree turns do not need chains; without an armed target they would only grow cost.
[[nodiscard]] inline float foldedMagnitude(float delta) noexcept
{
    const float folded = fold180(delta);
    return folded < 0.0f ? -folded : folded;
}

} // namespace fva_math
