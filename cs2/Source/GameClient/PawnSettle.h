#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>

// MAP-TRANSITION SESSION GATE for per-frame work that CALLS INTO the game on the local pawn
// (AGENTS.md rule 0). Crash class 2026-09-27 (crashes 02:14 + 10:12, both on map load, both
// right after the fresh pawn spawned): GetAimPunch(services, 0, 0) on the freshly-spawned
// pawn walked the aim-punch history array at [services+0x28] before the game had built it
// (SIGSEGV at a garbage 0x3940f1b7400 inside libclient+0x151794e, the interpolation helper
// under the real GetAimPunch at 0x1518020); AgentChanger's SetModel on the fresh pawn raced
// the model-load machinery the same way on 2026-09-19. The curtime gate
// (schema_readiness::kMinMapTime) is BLIND during the map-join window - it reads the OLD
// map's large curtime until the GlobalVars swap (the 2026-09-12 NameAnimator lesson) - so
// this gate is anchored to CLOCK_MONOTONIC instead: a pawn IDENTITY change (map join,
// respawn, team switch) re-arms a settle window, and work only flows after the same pawn
// has been the local pawn for kSettleSeconds. Mid-round respawns re-arm it too - a short
// cosmetic delay for features that re-apply on respawn, which is exactly what rule 0
// demands ("MUST NOT re-fire work just because a pawn identity changed (respawn/transition)
// until the session gate passes"). Consumers: PlayerPawn::aimPunchAngle (all five readers),
// HookContext::localPlayerBulletInaccuracy (GetInaccuracy/GetSpread/UpdateAccuracyPenalty
// on the fresh weapon - the same call-into-the-game shape), AgentChanger::run (SetModel).
namespace pawn_settle {

inline std::atomic<void*> trackedPawn{nullptr};
inline std::atomic<std::int64_t> firstSeenNs{0};

// How long the current local pawn must have been stable before game-function calls on it
// are safe. 3s from pawn spawn clears the map-join build window (the pawn spawns late in
// the load; the game finishes the entity's animation/services state well inside this).
inline constexpr std::int64_t kSettleNs = 3'000'000'000;

[[nodiscard]] inline std::int64_t nowNs() noexcept
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
}

// True when `pawn` has been the local pawn for at least kSettleNs. A pawn identity change
// re-arms the settle window (last-wins on a bouncing identity). Null never passes.
[[nodiscard]] inline bool ready(const void* pawn) noexcept
{
    if (!pawn)
        return false;
    if (trackedPawn.load(std::memory_order_acquire) != pawn) {
        trackedPawn.store(const_cast<void*>(pawn), std::memory_order_release);
        firstSeenNs.store(nowNs(), std::memory_order_release);
        return false;
    }
    return nowNs() - firstSeenNs.load(std::memory_order_acquire) >= kSettleNs;
}

} // namespace pawn_settle