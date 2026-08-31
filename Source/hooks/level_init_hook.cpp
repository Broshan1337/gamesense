// ============================================================================
// Hook B body — CS2 IGameSystem::LevelInit dispatch.
//
// Target = client.dll+0xAFDFB0 (identified as CS2's client-side LevelInit
// via workflow w3v96qwdb TargetMap agent 2026-07-10).  Function fires the
// `game_newmap` and `transition` game events via IGameEventManager2.
//
// FVA H2 slot name decodes to "CreateSwapChain" — decoy label; actual
// target is LevelInit per client.dll IDA session f01ad8c5 xrefs
// (IGameSystem vtable data-refs at 0x181ab9138 et al).
//
// arg2 is `const CGameSystemInitInfo*` (opaque pointer to map info
// struct), NOT a C-string.  We pass through opaque.
// ============================================================================
#include "level_init_hook.h"
#include "capture_buffer.h"
#include "create_move_hook.h"       // for fire_flag_reset
#include "engine2_bindings.h"       // for engine2::notify_level_init_complete
#include "game_state_resolver.h"
#include "view_angle_spoofer.h"               // for view_angle_spoofer::notify_level_init
#include "../core/signature_scanner.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <MinHook.h>
#include <cstdio>
#include <cstring>

namespace fva::hooks
{

namespace {

using LevelInitFn = void*(__fastcall*)(void*, void*);
LevelInitFn g_original = nullptr;

// Depot 24134959: LevelInit @ RVA 0xB3A600.  User-supplied LEVELINIT_PATTERN.
// Entry prologue is `48 89 74 24 10 57 48 83 EC 30 48 8B 0D ? ? ? ? 48 8B FA`.
constexpr std::string_view k_target_anchor =
    "48 89 74 24 10 57 48 83 EC 30 48 8B 0D ? ? ? ? 48 8B FA";

// FVA 1:1 Hook B tail (workflow wq0rgtslf: 2026-07-10 audit revealed):
//   1. FVA does NOT memset unk_7FFBE8234C90 (Hook C scratch) on LevelInit
//      — the vtable in client.dll .rdata never moves within a process,
//      so scratch stays valid.  Removed the invented memset.
//   2. FVA guards ALL THREE resolves behind `if (!qword_7FFBE823A980)` —
//      they run ONCE per process, NOT once per level.  Replaced
//      `force_reresolve()` with plain `resolve()` (which respects the
//      g_tried latch).
//   3. FVA writes `byte_7FFBE823A97C = 0` at the tail (sub_7FFBE80C8E68
//      +0x69C) which triggers Handler A to re-snapshot fire_flag_probe on
//      next tick.  Ported as `fire_flag_reset()`.
void __fastcall detour(void* client_state, void* init_info) noexcept
{
    // FVA 1:1 (mass-decompile agent 2026-07-10 finding):
    // `sub_7FFBE80DBB0C` uses TWO hash-resolved offsets + generation-bit
    // check — CS2 recycles interface handles across map loads.  A cached
    // interface pointer from the previous level dereferences into freed
    // memory on the next map.
    //
    // FVA safeguards via generation check inside sub_7FFBE80DBB0C every
    // call.  Our port hard-codes offset 0x5E8 without generation-bit
    // (would require porting sub_7FFBE80FB1C4 schema-hash-to-offset +
    // sub_7FFBE80DBB0C dispatcher — non-trivial vs the stability payoff).
    //
    // Pragmatic 1:1 equivalent: force_reresolve() on every LevelInit so
    // the interface pointer is re-derived after CS2 rebuilds it.  Same
    // end-state as FVA's generation check for the map-transition case.
    fva::game_state::force_reresolve();

    // Reset fire_flag_probe snapshot so Handler A re-snapshots on next tick.
    fva::hooks::fire_flag_reset();

    // Balance the LevelShutdown flag — engine2 gate now sees the world
    // as loaded again.  Grace period was removed 2026-07-11; the composite
    // engine2 gate (IsInGame && IsConnected && !g_shutting_down) is the
    // authoritative crash guard.
    fva::hooks::engine2::notify_level_init_complete();

    if (g_original) (void)g_original(client_state, init_info);
}

void* resolve_target()
{
    // Sig anchor `48 8D 6C 24 B9 ...` matches 5 bytes AFTER the real entry
    // — entry has hotpatch-friendly `push rbp; push rsi; push r14` (5 bytes)
    // before the anchor.  Apply -5 so MinHook trampoline includes the
    // pushes and runs in same context as the original function; installing
    // at anchor would leave `lea rbp, [rsp-47h]` in trampoline using
    // detour's RSP, corrupting caller stack frame.
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;
    // New pattern is at function entry — no -5 shift needed.
    if (void* p = fva::scanner::find_in_module(client, k_target_anchor))
        return p;
    return reinterpret_cast<std::uint8_t*>(client) +
           fva::version::known_rva::hook_b_target;
}

} // namespace

bool install_level_init_hook()
{
    if (g_original) return true;
    void* target = resolve_target();
    if (!target) return false;

    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    const auto rva = reinterpret_cast<std::uintptr_t>(target) -
                     reinterpret_cast<std::uintptr_t>(client);
    const auto* b  = reinterpret_cast<const std::uint8_t*>(target);
    std::printf("[fva_recon] Hook B resolved target=%p (RVA 0x%zX) "
                 "prologue=%02X %02X %02X %02X %02X\n",
                 target, rva, b[0], b[1], b[2], b[3], b[4]);

    if (MH_CreateHook(target, reinterpret_cast<void*>(&detour),
                        reinterpret_cast<void**>(&g_original)) != MH_OK)
        return false;
    return MH_EnableHook(target) == MH_OK;
}

void uninstall_level_init_hook()
{
    if (!g_original) return;
    void* target = resolve_target();
    if (!target) return;
    MH_DisableHook(target);
    MH_RemoveHook(target);
    g_original = nullptr;
}

} // namespace fva::hooks
