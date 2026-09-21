#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <CS2/Classes/Vector.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Route A spread predictor - calls CS2's OWN two spread functions so the prediction is bit-exact with
// what the server will compute for the shot, instead of re-deriving the RNG/cone math ourselves.
//
//   - sub_1ADCAA0 (PointerToSpreadSeedFunction): the per-shot SEED = hash(round(pitch, 0.5deg),
//     round(yaw, 0.5deg), tick).
//   - sub_1ADCC00 (PointerToCalculateSpreadFunction): the per-bullet CONE, i.e. the tangent-plane
//     deflection (x, y) added to the aim's forward vector.
//
// This is the approach velocity-cs2 uses (get_tick_view_angles / weapon_calculate_spread); see the
// pattern-type comments (WeaponPatternTypes.h) for the calling conventions and the RE trail. A
// no-runtime-dependency reimplementation of the same math lives in SpreadPredictor.h as a fallback,
// but calling the game functions is preferred because it cannot drift from the game across updates.
//
// Consumers:
//   - the aimbot's spread COMPENSATION: findSpreadCorrection() returns an aim angle (including ROLL)
//     that cancels the predicted deflection, so a silently-redirected shot lands on target while the
//     player is moving (the dominant source of missed silent-aim shots).
//   - the triggerbot's hitchance gate (to come): spreadOffset() sampled over many seeds.
template <typename HookContext>
class SpreadSolver {
public:
    explicit SpreadSolver(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Everything the cone generator needs about the active weapon's current state, gathered once so a
    // sampling loop does not re-read it per iteration.
    struct WeaponSpreadParams {
        std::int16_t itemDefinitionIndex;
        int numBullets;
        float inaccuracy;
        float spread;
        float recoilIndex;
    };

    // An angle triple in the engine's QAngle order (pitch, yaw, roll), degrees. A distinct type from a
    // position Vector even though the layout is identical, to keep the two from being mixed up.
    struct Angles {
        float pitch;
        float yaw;
        float roll;
    };

    // Reads the weapon's live spread inputs, or {} if any is unavailable (a required function/pointer
    // did not resolve, VData is not ready on a freshly spawned weapon, etc.). Callers must treat {} as
    // "cannot predict - do not compensate / do not gate on hitchance".
    [[nodiscard]] Optional<WeaponSpreadParams> weaponParams(auto&& weapon) const noexcept
    {
        // Bring the accuracy penalty + recoil index current BEFORE reading them - CS2 decays both
        // lazily, so a raw read lags the real cone (see BaseWeapon::updateAccuracyPenalty). This is
        // what makes fast-firing spray weapons predictable rather than always missing.
        weapon.updateAccuracyPenalty();

        const auto itemDef = weapon.itemDefinitionIndex();
        const auto bullets = weapon.numBullets();
        const auto inacc = weapon.inaccuracy();
        const auto spr = weapon.spread();
        const auto recoil = weapon.recoilIndex();
        if (!itemDef.hasValue() || !bullets.hasValue() || !inacc.hasValue() || !spr.hasValue() || !recoil.hasValue())
            return {};
        return WeaponSpreadParams{
            static_cast<std::int16_t>(itemDef.value()),
            bullets.value(),
            inacc.value(),
            spr.value(),
            recoil.value(),
        };
    }

    // Same as weaponParams, but the inaccuracy is predicted at `velocity` instead of the pawn's real
    // current velocity (via BaseWeapon::inaccuracyAtVelocity - velocity-cs2's get_inaccuracy_at_velocity).
    // spread/recoil/bullets are read from the live weapon exactly as weaponParams does (only the movement
    // penalty is velocity-dependent). Use this to build a cone for the velocity the shot will actually
    // fire at (post-counter-strafe stop, mid-track, etc.) rather than the instantaneous one. {} on the
    // same conditions as weaponParams, plus if the velocity prediction could not resolve.
    [[nodiscard]] Optional<WeaponSpreadParams> weaponParamsAtVelocity(auto&& weapon, cs2::C_BaseEntity* pawn, const cs2::Vector& velocity) const noexcept
    {
        // Bring the penalty current first so the base spread read (and the backup inaccuracyAtVelocity
        // makes) reflects this tick, then predict inaccuracy at the hypothetical velocity.
        weapon.updateAccuracyPenalty();

        const auto itemDef = weapon.itemDefinitionIndex();
        const auto bullets = weapon.numBullets();
        const auto spr = weapon.spread();
        const auto recoil = weapon.recoilIndex();
        const auto inacc = weapon.inaccuracyAtVelocity(pawn, velocity);
        if (!itemDef.hasValue() || !bullets.hasValue() || !inacc.hasValue() || !spr.hasValue() || !recoil.hasValue())
            return {};
        return WeaponSpreadParams{
            static_cast<std::int16_t>(itemDef.value()),
            bullets.value(),
            inacc.value(),
            spr.value(),
            recoil.value(),
        };
    }

    // The 32-bit seed the game will derive for a shot taken at `angles` on `tick`. The pawn argument
    // is passed as nullptr - the function ignores it (confirmed by decompile). {} if the seed function
    // did not resolve.
    [[nodiscard]] Optional<std::uint32_t> seed(const Angles& angles, int tick) const noexcept
    {
        const auto seedFn = hookContext.patternSearchResults().template get<PointerToSpreadSeedFunction>();
        if (!seedFn)
            return {};
        const cs2::Vector asVector{angles.pitch, angles.yaw, angles.roll};
        return seedFn(nullptr, &asVector, tick);
    }

    // The tangent-plane deflection the game will apply given `seed` and the weapon's params, returned
    // as {x, y, 0}: bullet_dir = forward + left*x + up*y. The game calls the cone generator with
    // seed+1 (its own pre-increment), so mirror that here to keep the sequence aligned. Returns {0,0,0}
    // if the function did not resolve (i.e. "no predicted deflection") or the weapon state is nonsense.
    //
    // OUTPUT CONTRACT (from the 14177 disassembly, and the cause of the 2026-08-25 kill-shot crash):
    // outX/outY are ARRAYS - the generator writes one (x, y) pair per simulated bullet, up to
    // numBullets * (1 + int(recoilIndex)) pairs (the `cvttss2si eax, xmm2 / imul eax, ecx` bound at
    // the top of the write loop). Passing a single float worked only while that count was 1 (first
    // shot of a spray); mid-spray the extra pairs were written past it into the CALLER's frame -
    // corrupting WeaponSpreadParams between Monte-Carlo samples, so a later sample read a garbage
    // loop count and the generator wrote off the thread stack (SIGSEGV at libclient+0x1AF9B2D
    // `movss [r14-4], xmm0`). We only ever consume the FIRST pellet's deflection, so give the game
    // the full buffer it demands and read index 0.
    [[nodiscard]] cs2::Vector spreadOffset(std::uint32_t seed, const WeaponSpreadParams& params) const noexcept
    {
        cs2::Vector out{};
        const auto spreadFn = hookContext.patternSearchResults().template get<PointerToCalculateSpreadFunction>();
        if (!spreadFn)
            return out;

        // Sanity-gate the values that size the output buffer (and would multiply into the game's
        // write loop). recoilIndex is a float read off the weapon entity; NaN fails both range
        // comparisons and is rejected. Fail CLOSED: nonsense state = "no predicted deflection".
        const int recoilSteps = static_cast<int>(params.recoilIndex);
        if (params.numBullets <= 0 || params.numBullets > kMaxNumBullets
            || !(params.recoilIndex >= 0.0f && params.recoilIndex <= kMaxRecoilIndex))
            return out;
        const int steps = params.numBullets * (recoilSteps + 1);
        if (steps > kMaxSpreadSteps)
            return out;

        float outX[kMaxSpreadSteps];
        float outY[kMaxSpreadSteps];
        spreadFn(params.itemDefinitionIndex, params.numBullets, 0, seed + 1u, params.inaccuracy, params.spread, params.recoilIndex, outX, outY);
        out.x = outX[0];
        out.y = outY[0];
        return out;
    }

    // MOVING-TARGET SPREAD CANCELLATION (deviation from velocity-cs2, documented):
    //
    // velocity sweeps PITCH buckets only and needs a self-consistent fixed point: it writes
    // aim.pitch + atan(deflection) and requires that angle to round back into the candidate's
    // 0.5deg seed bucket (|bucketDistance - deflection| <= 0.25deg). Standing, deflections are
    // tiny and a fixed point nearly always exists. MOVING, the cone grows to multiple degrees,
    // matches become rare, the sweep returns {} - and the shot leaves with the RAW angle: random
    // cone scatter around the head ("misses + body hits + lucky headshots while running").
    //
    // This version searches the 2D bucket neighbourhood directly. Geometry that makes it work:
    // the game's forward vector IGNORES roll (f = f(pitch, yaw) only), so roll rotates the spread
    // offset anywhere in the tangent plane without moving the fire axis. For a written angle W the
    // bullet lands exactly on the aim when the offset magnitude equals the angular distance from
    // f(W) to the aim direction, with roll rotating the offset onto the connecting direction:
    //   roll = atan2(y, x) - atan2(e.up0, e.right0)
    // (verified to reduce to velocity's formula for their pure-pitch displacement). Every candidate
    // (pitchBucket, yawBucket) pair is tested as-is - no forced pitch bump, no self-consistency
    // condition left to fail - so the best candidate of a ~2D pool gives a sub-quarter-degree match
    // virtually every shot, standing or moving. The search window is sized from the CURRENT cone
    // (inaccuracy + spread), so standing shots now cost ~25 candidates instead of velocity's 720.
    [[nodiscard]] Optional<Angles> findSpreadCorrection(const Angles& aim, int tick, const WeaponSpreadParams& params) const noexcept
    {
        // (Near-)zero cone: there is NOTHING to cancel - the raw aim direction already lands within
        // a few thousandths of a degree. Returning the aim unchanged (instead of {}) is what makes
        // the spread gate FIRE these ticks: the first bullets of a spray / a rested scout have the
        // smallest cone of the whole engagement, and treating "no offset to steer" as "cannot
        // predict" suppressed exactly the most accurate shots (measured in-game: AK/Scout/Deagle
        // never landing their first bullets, [gate] diag showing inac=0.0000 spr=0.0000).
        if (params.inaccuracy + params.spread < kNegligibleCone)
            return Angles{aim.pitch, aim.yaw, 0.0f};

        const auto targetDir = forwardVector(aim.pitch, aim.yaw);

        // The widest possible tangent offset this tick, as an angle: atan(inaccuracy + spread). The
        // search window follows it, capped by a per-axis BUCKET count rather than a degree span, so
        // huge cones - deagle on the run, any weapon in the air - still get searched out to their
        // reach instead of being clipped by an arbitrary degree limit.
        const auto maxDeflectionDeg = trig::arcTangent2(params.inaccuracy + params.spread, 1.0f) * trig::kRadiansToDegrees;
        const auto spanDeg = (maxDeflectionDeg + kMarginDegrees > kMaxBucketsPerAxis * kBucketDegrees * 0.5f)
            ? kMaxBucketsPerAxis * kBucketDegrees * 0.5f
            : maxDeflectionDeg + kMarginDegrees;
        const int pitchMin = static_cast<int>(floorF((aim.pitch - spanDeg) / kBucketDegrees));
        const int pitchMax = static_cast<int>(ceilF((aim.pitch + spanDeg) / kBucketDegrees));
        const int yawMin = static_cast<int>(floorF((aim.yaw - spanDeg) / kBucketDegrees));
        const int yawMax = static_cast<int>(ceilF((aim.yaw + spanDeg) / kBucketDegrees));

        bool found{};
        float bestResidual = kAcceptResidualDegrees;
        Angles best{};
        // Best-candidate details for the exact-match refinement below.
        float bestPitch{}, bestYaw{}, bestOffsetX{}, bestOffsetY{}, bestLength{}, bestChord{};

        for (int j = yawMin; j <= yawMax; ++j) {
            const auto writtenYaw = static_cast<float>(j) * kBucketDegrees;
            for (int i = pitchMin; i <= pitchMax; ++i) {
                const auto writtenPitch = static_cast<float>(i) * kBucketDegrees;

                const auto candidateSeed = seed(Angles{writtenPitch, writtenYaw, 0.0f}, tick);
                if (!candidateSeed.hasValue())
                    return {};

                const auto offset = spreadOffset(candidateSeed.value(), params);
                const auto length = trig::squareRoot(offset.x * offset.x + offset.y * offset.y);
                if (length < kMinOffsetLength)
                    continue;

                // Angular distance from the written forward axis to the aim direction. e is tangent
                // by construction (both are unit directions), so |e| is the chord; atan converts it
                // to the same angle convention the game's tangent offset uses.
                const auto fw = forwardVector(writtenPitch, writtenYaw);
                const cs2::Vector e{targetDir.x - fw.x, targetDir.y - fw.y, targetDir.z - fw.z};
                const auto chord = trig::squareRoot(e.x * e.x + e.y * e.y + e.z * e.z);
                const auto distanceDeg = trig::arcTangent2(chord, 1.0f) * trig::kRadiansToDegrees;
                const auto lengthDeg = trig::arcTangent2(length, 1.0f) * trig::kRadiansToDegrees;

                const auto residual = lengthDeg > distanceDeg ? lengthDeg - distanceDeg : distanceDeg - lengthDeg;
                if (residual >= bestResidual)
                    continue;

                // Roll aligning the offset with -e in the tangent plane at the written angles.
                // Basis at roll 0: right0 = (sy, -cy, 0), up0 = (sp*cy, sp*sy, cp). Verified
                // numerically against Valve AngleVectors over 20k randomized cases: every matched
                // candidate lands within 0.22deg of the aim. Reduces to velocity's proven
                // -atan2(x, y) for their pure-pitch displacement.
                const auto pitchRad = writtenPitch * trig::kDegreesToRadians;
                const auto yawRad = writtenYaw * trig::kDegreesToRadians;
                const auto sp = trig::sine(pitchRad), cp = trig::cosine(pitchRad);
                const auto sy = trig::sine(yawRad), cy = trig::cosine(yawRad);
                const auto eUp = e.x * (sp * cy) + e.y * (sp * sy) + e.z * cp;
                const auto eRight = e.x * sy - e.y * cy;
                const auto rollDeg = (trig::arcTangent2(offset.y, offset.x) - trig::arcTangent2(eUp, eRight)) * trig::kRadiansToDegrees;

                found = true;
                bestResidual = residual;
                best = Angles{writtenPitch, writtenYaw, normalizeRoll(rollDeg)};
                bestPitch = writtenPitch;
                bestYaw = writtenYaw;
                bestOffsetX = offset.x;
                bestOffsetY = offset.y;
                bestLength = length;
                bestChord = chord;
                if (residual <= kExactResidualDegrees)
                    return best; // already exact within noise - skip the refinement cost
            }
        }

        if (!found)
            return {};

        // CONTINUOUS EXACT-MATCH REFINEMENT. Bucket centers quantize the achievable displacement,
        // which leaves up to +-0.25deg (~13cm at 30m - head-sized) of residual on every shot. But the
        // seed only reads the ROUNDED pitch/yaw: any written angle inside a bucket's rounding window
        // hashes identically while its actual forward axis moves continuously. Sliding the written
        // angle along the great circle toward the aim tunes the angular distance to EQUAL the offset
        // magnitude exactly, eliminating the quantization residual whenever the match fits in the
        // window. (chord c of an angle a satisfies c = 2*sin(a/2), i.e. a = acos(1 - c*c/2).)
        const auto rho0 = trig::arcCosine(1.0f - bestChord * bestChord * 0.5f); // center-to-aim angle, rad
        const auto slide = rho0 - trig::arcCosine(1.0f - bestLength * bestLength * 0.5f);
        const auto sinRho = trig::sine(rho0);
        if (trig::absolute(slide) > kMaxSlideDegrees * trig::kDegreesToRadians || sinRho < 1e-6f)
            return best;

        const auto cHat = forwardVector(bestPitch, bestYaw);
        const auto cosRho = trig::cosine(rho0);
        const auto dX = targetDir.x - cHat.x * cosRho;
        const auto dY = targetDir.y - cHat.y * cosRho;
        const auto dZ = targetDir.z - cHat.z * cosRho;
        const auto nLen = trig::squareRoot(dX * dX + dY * dY + dZ * dZ);
        if (nLen < kMinOffsetLength)
            return best;
        const cs2::Vector nHat{dX / nLen, dY / nLen, dZ / nLen};
        const cs2::Vector newDir{cHat.x * trig::cosine(slide) + nHat.x * trig::sine(slide),
                                 cHat.y * trig::cosine(slide) + nHat.y * trig::sine(slide),
                                 cHat.z * trig::cosine(slide) + nHat.z * trig::sine(slide)};

        const auto refined = dirToAngles(newDir);
        // Must round back into the SAME seed buckets, or everything above was computed for the wrong seed.
        const auto bucketOf = [](float angle) { return static_cast<int>(floorF(angle / kBucketDegrees + 0.5f)); };
        if (bucketOf(refined.pitch) != bucketOf(bestPitch)
            || bucketOf(refined.yaw) != bucketOf(bestYaw))
            return best;

        const auto refFw = forwardVector(refined.pitch, refined.yaw);
        const cs2::Vector refE{targetDir.x - refFw.x, targetDir.y - refFw.y, targetDir.z - refFw.z};
        const auto refChord = trig::squareRoot(refE.x * refE.x + refE.y * refE.y + refE.z * refE.z);
        const auto refResidual = bestLength > refChord ? bestLength - refChord : refChord - bestLength;
        if (refResidual >= bestResidual)
            return best; // refinement did not help (degenerate geometry) - keep the bucket center

        const auto pitchRad = refined.pitch * trig::kDegreesToRadians;
        const auto yawRad = refined.yaw * trig::kDegreesToRadians;
        const auto sp = trig::sine(pitchRad), cp = trig::cosine(pitchRad);
        const auto sy = trig::sine(yawRad), cy = trig::cosine(yawRad);
        const auto eUp = refE.x * (sp * cy) + refE.y * (sp * sy) + refE.z * cp;
        const auto eRight = refE.x * sy - refE.y * cy;
        const auto rollDeg = (trig::arcTangent2(bestOffsetY, bestOffsetX) - trig::arcTangent2(eUp, eRight)) * trig::kRadiansToDegrees;
        return Angles{refined.pitch, refined.yaw, normalizeRoll(rollDeg)};
    }

private:
    // spreadOffset output-buffer limits. Real weapons: numBullets is 1 (rifles/pistols) up to ~10
    // (shotguns); recoilIndex is a per-shot float counter that stays under ~40 in the wildest
    // spray. Anything outside these bounds is weapon state we don't trust - see spreadOffset.
    static constexpr int kMaxNumBullets = 32;
    static constexpr float kMaxRecoilIndex = 100.0f;
    // Covers numBullets * (1 + int(recoilIndex)) for every real weapon state (worst realistic
    // case ~10 pellets * low recoil, or 1 bullet * ~40 recoil steps). 2 * 4 KiB of stack per
    // call - the sampling loops call this hundreds of times per tick, but sequentially.
    static constexpr int kMaxSpreadSteps = 512;

    // Written angles are absolute 0.5deg seed buckets - the same rounding the game's spread-seed
    // hash applies to pitch and yaw.
    static constexpr float kBucketDegrees = 0.5f;
    // Slack around [aim, aim + cone] so mid-bucket aim directions are covered on both axes.
    static constexpr float kMarginDegrees = 1.0f;
    // Hard cap on the half-search-span expressed as a per-axis bucket count (~4 * span buckets per
    // axis candidate). 64 buckets = up to a 16 degree half-span, enough for deagle-on-the-run and
    // airborne cones while bounding the pool at ~4k candidates.
    static constexpr int kMaxBucketsPerAxis = 64;
    // A correction is only used when the mismatch between the candidate's offset magnitude and its
    // angular distance to the aim is under this - anything larger lands further off than writing
    // the raw angle would.
    static constexpr float kAcceptResidualDegrees = 0.25f;
    // Residual below which a candidate is taken immediately without finishing the scan.
    static constexpr float kExactResidualDegrees = 0.02f;
    // How far the exact-match refinement may slide the written angle off its bucket center, in
    // degrees; the seed-rounding window allows +-0.25deg per axis, i.e. ~0.35deg diagonally - stay
    // comfortably inside it.
    static constexpr float kMaxSlideDegrees = 0.33f;
    // Effectively-zero offsets: matching them against a nonzero distance makes no sense.
    static constexpr float kMinOffsetLength = 1e-5f;
    // Cone (inaccuracy + spread, a tangent slope) below which the whole solver is skipped: the
    // deflection atan(1e-4) ~= 0.0057deg - sub-millimeter at 10m, far below hitbox relevance. The
    // aim itself is returned as the correction so the spread gate fires instead of suppressing.
    static constexpr float kNegligibleCone = 1e-4f;

    // The game's forward direction for view angles in degrees. Roll-independent by definition -
    // that property is what lets roll steer the offset without moving the fire axis.
    [[nodiscard]] static cs2::Vector forwardVector(float pitchDeg, float yawDeg) noexcept
    {
        const auto pitchRad = pitchDeg * trig::kDegreesToRadians;
        const auto yawRad = yawDeg * trig::kDegreesToRadians;
        const auto sp = trig::sine(pitchRad), cp = trig::cosine(pitchRad);
        const auto sy = trig::sine(yawRad), cy = trig::cosine(yawRad);
        return cs2::Vector{cp * cy, cp * sy, -sp};
    }

    // Inverse of forwardVector: unit direction -> (pitch, yaw) in degrees (roll 0).
    [[nodiscard]] static Angles dirToAngles(const cs2::Vector& dir) noexcept
    {
        const auto horizontal = trig::squareRoot(dir.x * dir.x + dir.y * dir.y);
        return Angles{
            trig::arcTangent2(-dir.z, horizontal) * trig::kRadiansToDegrees,
            trig::arcTangent2(dir.y, dir.x) * trig::kRadiansToDegrees,
            0.0f};
    }

    // libm-free floor/ceil (the project links -nostdlib; floorf/ceilf are not available). Inputs here
    // are small view-angle expressions, far from the int overflow range.
    [[nodiscard]] static constexpr float floorF(float value) noexcept
    {
        const auto truncated = static_cast<int>(value);
        return (value >= 0.0f || static_cast<float>(truncated) == value) ? static_cast<float>(truncated) : static_cast<float>(truncated - 1);
    }

    [[nodiscard]] static constexpr float ceilF(float value) noexcept
    {
        return -floorF(-value);
    }

    [[nodiscard]] static float normalizeRoll(float rollDeg) noexcept
    {
        auto roll = rollDeg;
        while (roll > 180.0f)
            roll -= 360.0f;
        while (roll < -180.0f)
            roll += 360.0f;
        return roll;
    }

    HookContext& hookContext;
};
