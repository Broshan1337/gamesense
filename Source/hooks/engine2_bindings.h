// ============================================================================
// engine2.dll bindings — sig-scan resolvers for game state functions.
//
// FVA doesn't wire these (its IAT initializer remains VMP-obfuscated), but
// hooking IsInGame / IsConnected / LevelShutdown as a stability gate gives
// map-change / server-exit safety without the ambiguity of a tick-counting
// grace period.
// ============================================================================
#pragma once

#include <cstdint>

namespace fva::hooks::engine2
{

// One-shot resolver — call once during hook install.  Idempotent.
bool init() noexcept;

// True iff engine2!IsInGame() returns true.  If resolution failed, returns
// false (fail-safe: disable view-angle emitter mutation when we can't verify state).
bool is_in_game() noexcept;

// True iff engine2!IsConnected() returns true.  Same fail-safe as above.
bool is_connected() noexcept;

// LevelShutdown hook: sets internal g_shutting_down flag which forces
// is_stable_gameplay() to return false until the next LevelInit fires.
// Returns true on install success, false otherwise.
bool install_level_shutdown_hook() noexcept;
void uninstall_level_shutdown_hook() noexcept;

// LevelInit tail — clears the shutdown flag once map is up.
void notify_level_init_complete() noexcept;

// Composite gate.  Returns true ONLY when all of:
//   1. engine2!IsInGame()  == true
//   2. engine2!IsConnected() == true
//   3. g_shutting_down == false (LevelShutdown not yet balanced by LevelInit)
//
// view-angle emitter mutation should skip on false and fall back to SELF-ECHO.
bool is_stable_gameplay() noexcept;

// Resolved RVA cache — exposed for logging & diagnostic.  Zero if unresolved.
struct ResolvedRVAs {
    std::uintptr_t get_aspect_ratio;
    std::uintptr_t is_in_game;
    std::uintptr_t is_connected;
    std::uintptr_t run_command;
    std::uintptr_t get_level_name;
    std::uintptr_t get_level_name_short;
    std::uintptr_t connect;
    std::uintptr_t level_shutdown;
};

const ResolvedRVAs& resolved() noexcept;

// ---- client.dll resolvers (Category 4-6) ---------------------------------
// FVA touches pb->random_seed; ComputeRandomSeed is CS2's helper that
// returns a validated per-tick seed.  Called from view_angle_spoofer when we want
// the wire to carry a plausible seed (server-side validation).
using ComputeRandomSeedFn = std::uint32_t(__fastcall*)(void*, void*, int);
ComputeRandomSeedFn compute_random_seed() noexcept;

// ForceButtonsDown — used by cheats to override input; FVA does NOT hook
// this but we expose the pointer + first-call diagnostic.
using ForceButtonsDownFn = void(__fastcall*)(void*, void*);
ForceButtonsDownFn force_buttons_down() noexcept;

// GetViewAngles / SetViewAngles — engine-side view state.  FVA reads pb->
// viewangles (protobuf field) directly; these are the equivalent CS2 API.
// Exposed for future feature parity.
using GetViewAnglesFn = void*(__fastcall*)(void*, int);
using SetViewAnglesFn = void(__fastcall*)(void*, int, void*);
GetViewAnglesFn get_view_angles() noexcept;
SetViewAnglesFn set_view_angles() noexcept;

} // namespace fva::hooks::engine2
