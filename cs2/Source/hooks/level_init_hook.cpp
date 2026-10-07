













#include "level_init_hook.h"
#include "capture_buffer.h"
#include "create_move_hook.h"       
#include "engine2_bindings.h"       
#include "game_state_resolver.h"
#include "view_angle_spoofer.h"               
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



constexpr std::string_view k_target_anchor =
    "48 89 74 24 10 57 48 83 EC 30 48 8B 0D ? ? ? ? 48 8B FA";












void __fastcall detour(void* client_state, void* init_info) noexcept
{
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    fva::game_state::force_reresolve();

    
    fva::hooks::fire_flag_reset();

    
    
    
    
    fva::hooks::engine2::notify_level_init_complete();

    if (g_original) (void)g_original(client_state, init_info);
}

void* resolve_target()
{
    
    
    
    
    
    
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;
    
    if (void* p = fva::scanner::find_in_module(client, k_target_anchor))
        return p;
    return reinterpret_cast<std::uint8_t*>(client) +
           fva::version::known_rva::hook_b_target;
}

} 

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

} 
