// ============================================================================
// Hook A — CInput::CreateMove (per-tick UserCmd builder).
//
// Reproduces the FVA handler `sub_7FFBE80C9D8C` (client.dll+0xACEF90 target).
//
// FVA Hook A runs the original CreateMove first (CS2 builds the base
// CBaseUserCmdPB with real user input) then runs an in-place mutation pass
// on the CUserCmd wrapper CS2 owns for this tick.  The mutation writes into
// the CBaseUserCmdPB* at raw_cmd->base — the same instance Hook C later
// snapshots into scratch.
//
// The mutation body is derived from FVA disasm — see the .cpp for the
// verified per-block source of every field write.  There is NO external
// "aim scheduler" — FVA computes mutation deltas inline from game state.
// ============================================================================
#pragma once

namespace fva::hooks
{

bool install_create_move_hook();
void uninstall_create_move_hook();

// FVA 1:1 — Hook B tail (sub_7FFBE80C8E68 +0x69C) writes
// `byte_7FFBE823A97C = 0` so Handler A re-snapshots fire_flag_probe on the
// next tick.  This mirrors that write: clear the g_latched/g_target/
// g_snapshot atomics so the next `init_and_snapshot()` re-samples.
void fire_flag_reset() noexcept;

} // namespace fva::hooks
