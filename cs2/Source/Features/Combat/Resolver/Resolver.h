#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Combat/Resolver/ResolverConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/PlayerSlotLookup.h>
#include <HookContext/HookContextMacros.h>

// Miss-detection bruteforce resolver (velocity mindset, AByteCheat's state
// machine, CS2 data): the aimbot stages a shot at a victim; a player_hurt /
// player_death where we are the attacker means the shot landed (reset); no
// damage within MissTicks of the fire tick means the yaw'd target dodged -
// advance that victim's bruteforce yaw state, and the next scan rotates its
// candidate points around the record origin (Aimbot::bestPointOnSkeleton).
// CS2 resolvers have richer per-input subtick data (Stage 0 research), which
// makes shot-outcome feedback the right design vs CSGO anim-layer estimation.
namespace resolver
{

// Brute cycle: widen the yaw estimate each miss (classic skeet/AByteCheat family).
constexpr float kBruteOffsets[]{0.0f, -35.0f, 35.0f, -60.0f, 60.0f, -90.0f, 90.0f};
constexpr int kBruteStateCount = static_cast<int>(sizeof(kBruteOffsets) / sizeof(kBruteOffsets[0]));
constexpr int kMaxTrackedVictims = 64;

struct VictimState {
    std::uint32_t handle{0};   // pawn CEntityHandle value (index|serial)
    int bruteState{0};
    int misses{0};
};

struct PendingShot {
    bool active{false};
    std::uint32_t victimHandle{0};
    int firedTick{0};
};

inline VictimState victims[kMaxTrackedVictims];
inline PendingShot pendingShot;

[[nodiscard]] inline VictimState* findVictim(std::uint32_t handle) noexcept
{
    if (!handle)
        return nullptr;
    for (auto& victim : victims)
        if (victim.handle == handle)
            return &victim;
    return nullptr;
}

[[nodiscard]] inline VictimState& victimSlot(std::uint32_t handle) noexcept
{
    for (auto& victim : victims)
        if (victim.handle == handle)
            return victim;
    for (auto& victim : victims)
        if (victim.handle == 0) {
            victim = VictimState{handle, 0, 0};
            return victim;
        }
    // Table full: recycle the first slot (bounded, game-thread only).
    victims[0] = VictimState{handle, 0, 0};
    return victims[0];
}

// Aimbot query: the bruteforce yaw rotation for the victim's scan points.
[[nodiscard]] inline float bruteOffsetFor(std::uint32_t victimHandle) noexcept
{
    if (!victimHandle)
        return 0.0f;
    if (const auto* victim = findVictim(victimHandle))
        return kBruteOffsets[victim->bruteState % kBruteStateCount];
    return 0.0f;
}

// Aimbot stages a shot at this victim this tick (the fire happens on the same
// command at WriteMoveCrc; staging is our best fire signal).
inline void onShotStaged(std::uint32_t victimHandle, int tick) noexcept
{
    if (!victimHandle)
        return;
    pendingShot = PendingShot{true, victimHandle, tick};
}

// Miss window check; call once per CreateMove with the predicted server tick.
inline void onTick(int currentTick, int missTicks) noexcept
{
    if (!pendingShot.active)
        return;
    if (currentTick - pendingShot.firedTick < missTicks)
        return;
    // No damage registered in the window: miss. Advance the brute state.
    auto& victim = victimSlot(pendingShot.victimHandle);
    victim.bruteState = (victim.bruteState + 1) % kBruteStateCount;
    ++victim.misses;
    pendingShot = PendingShot{};
}

inline void resetAll() noexcept
{
    for (auto& victim : victims)
        victim = VictimState{};
    pendingShot = PendingShot{};
}

// Game events (FireEventClientSide, game thread): player_hurt/player_death with
// us as the attacker = the shot landed (reset); round_start = fresh table.
template <typename HookContext>
void onFireEventClientSide(HookContext& hookContext, cs2::IGameEvent* event) noexcept
{
    if (!event)
        return;

    if (game_events::is(event, "round_start")) {
        if (GET_CONFIG_VAR(resolver_vars::ResetOnRound))
            resetAll();
        return;
    }

    const bool hurt = game_events::is(event, "player_hurt");
    const bool death = game_events::is(event, "player_death");
    if (!hurt && !death)
        return;
    if (!game_events::localPlayerIsAttacker(hookContext, event))
        return;

    const auto victimSlotIndex = game_events::entityForKey(event, "userid");
    auto&& lookup = hookContext.template make<PlayerSlotLookup>();
    if (!lookup.isValidSlot(victimSlotIndex))
        return;

    auto&& victimPawn = lookup.pawnBySlot(victimSlotIndex);
    if (!victimPawn)
        return;
    const std::uint32_t victimHandle = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(victimPawn.baseEntity())).handle().value;
    if (!victimHandle)
        return;

    // Landed: clear the pending shot and restart the victim's brute cycle.
    if (pendingShot.active && pendingShot.victimHandle == victimHandle)
        pendingShot = PendingShot{};
    if (auto* victim = findVictim(victimHandle)) {
        victim->bruteState = 0;
        victim->misses = 0;
    }
}

}