// ============================================================================
// HookManager — orchestrates install/uninstall of the three FVA hooks.
//
// Matches sub_7FFBE80CDAC8 (HookManager::add_and_enable) semantics:
//   - keeps a descriptor list of {target, detour, trampoline, enabled}
//   - installs via MinHook, adapts jcc/short instructions with Hde64Disasm
//   - falls back to nothing (no VEH, no HW-BP) — same design as FVA
// ============================================================================
#pragma once

namespace fva::hooks
{

bool install_all();
void uninstall_all();

} // namespace fva::hooks
