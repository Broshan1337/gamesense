// ============================================================================
// Hook D body — animationsystem.dll ShouldUpdateSequences.
// See header for FVA cross-reference.
// ============================================================================
#include "animation_hook.h"
#include "../core/signature_scanner.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstdio>

namespace fva::hooks
{

namespace {

using ShouldUpdateSequencesFn =
    bool(__fastcall*)(void*, void*, void*);
ShouldUpdateSequencesFn g_original = nullptr;
void* s_installed_target = nullptr;

// Anchor for ShouldUpdateSequences (from UnknownCheats research + verified
// via mydisasm on legacy CS2 animationsystem.dll RVA 0x14EFF0):
//   48 89 5C 24 08         mov [rsp+8], rbx
//   48 89 74 24 18         mov [rsp+18h], rsi
//   57                     push rdi
//   48 83 EC 20            sub rsp, 20h
//   49 8B 40 48            mov rax, [r8+48h]
constexpr std::string_view k_target_anchor =
    "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20 49 8B 40 48";

bool __fastcall detour(void* self, void* networked_vars,
                        void* anim_graph_ctx) noexcept
{
    // FVA H4 mutation: clear networkedVars+0x30 to force anim graph to
    // rebuild sequence state.  See UnknownCheats forum research on knife
    // animation sequences.
    //
    // Guard against null networked_vars so we don't crash on early ticks.
    if (networked_vars) {
        *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(networked_vars) + 0x30) = nullptr;
    }

    if (!g_original) return true;   // fail-open: default to "yes update"
    return g_original(self, networked_vars, anim_graph_ctx);
}

void* resolve_target()
{
    HMODULE anim = ::GetModuleHandleW(
        fva::version::module_names::animationsystem_dll);
    if (!anim) return nullptr;
    if (void* p = fva::scanner::find_in_module(anim, k_target_anchor))
        return p;
    return reinterpret_cast<std::uint8_t*>(anim) +
           fva::version::known_rva::hook_d_target;
}

} // namespace

bool install_animation_hook()
{
    if (g_original) return true;
    void* target = resolve_target();
    if (!target) return false;

    HMODULE anim = ::GetModuleHandleW(
        fva::version::module_names::animationsystem_dll);
    const auto rva = reinterpret_cast<std::uintptr_t>(target) -
                     reinterpret_cast<std::uintptr_t>(anim);
    const auto* b  = reinterpret_cast<const std::uint8_t*>(target);
    std::printf("[fva_recon] Hook D resolved target=%p (RVA 0x%zX) "
                 "prologue=%02X %02X %02X %02X %02X\n",
                 target, rva, b[0], b[1], b[2], b[3], b[4]);

    if (MH_CreateHook(target, reinterpret_cast<void*>(&detour),
                       reinterpret_cast<void**>(&g_original)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) return false;
    s_installed_target = target;
    return true;
}

void uninstall_animation_hook()
{
    if (!g_original || !s_installed_target) return;
    MH_DisableHook(s_installed_target);
    MH_RemoveHook(s_installed_target);
    s_installed_target = nullptr;
    g_original = nullptr;
}

} // namespace fva::hooks
