// ============================================================================
// engine2.dll bindings — implementation.
//
// One-shot sig-scan resolves 8 engine2 functions + 4 client.dll functions
// listed by user 2026-07-11.  IsInGame / IsConnected are read-only calls
// used as gameplay gate; LevelShutdown gets a MinHook trampoline so we
// know when the world tears down.
// ============================================================================
#include "engine2_bindings.h"
#include "../core/signature_scanner.h"
#include "../schema/cs2_signature_atlas.h"

#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>

namespace fva::hooks::engine2
{

namespace {

// ---- Sig-scan patterns (from user's authoritative list 2026-07-11) --------
// GameOverlayRenderer64.dll (present) omitted — not FVA-relevant.

// engine2.dll
constexpr const char* kGetAspectRatioSig =
    "48 89 5C 24 ? 57 48 83 EC ? 8B FA 48 8D 0D";
constexpr const char* kIsInGameSig =
    "48 8B 05 ? ? ? ? 48 85 C0 74 ? 80 B8 ? ? ? ? 00 75 ? 83 B8 ? ? ? ? ? 7C";
constexpr const char* kIsConnectedSig =
    "48 8B 05 ? ? ? ? 48 85 C0 74 ? 83 B8 ? ? ? ? ? 0F 9D C0";
constexpr const char* kRunCommandSig =
    "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 56 41 57 48 81 EC ? ? "
    "? ? 0F 29 70 ? 41 0F B6 E9";
constexpr const char* kGetLevelNameSig =
    "48 83 EC ? E8 ? ? ? ? 84 C0 74 ? 48 8D 05 ? ? ? ? 48 83 C4 ? C3 48 "
    "8B 0D ? ? ? ? 48 85 C9 74 ? 83 B9 ? ? ? ? ? 7C ? 48 8B 89 ? ? ? ? 48 "
    "8D 05 ? ? ? ? 48 85 C9 48 0F 45 C1 48 83 C4 ? C3 48 8D 05 ? ? ? ? 48 "
    "83 C4 ? C3 ? ? ? ? ? ? ? ? ? ? ? ? 48 83 EC";
constexpr const char* kGetLevelNameShortSig =
    "48 83 EC ? E8 ? ? ? ? 84 C0 74 ? 48 8D 05 ? ? ? ? 48 83 C4 ? C3 48 "
    "8B 0D ? ? ? ? 48 85 C9 74 ? 83 B9 ? ? ? ? ? 7C ? 48 8B 89 ? ? ? ? 48 "
    "8D 05 ? ? ? ? 48 85 C9 48 0F 45 C1 48 83 C4 ? C3 48 8D 05 ? ? ? ? 48 "
    "83 C4 ? C3 ? ? ? ? ? ? ? ? ? ? ? ? B8";
constexpr const char* kConnectSig =
    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 44 89 81";

// client.dll — LevelShutdown pattern (user list)
constexpr const char* kLevelShutdownSig =
    "48 83 EC ? 48 8B 0D ? ? ? ? 48 8D 15 ? ? ? ? 45 33 C9 45 33 C0 ? ? "
    "? FF 50 ? 48 85 C0 74 ? 48 8B 0D ? ? ? ? 48 8B D0 ? ? ? 41 FF 50 ? "
    "48 83 C4";

// client.dll — Category 4-6 functions (call-only, not hooks)
constexpr const char* kComputeRandomSeedSig =
    "48 89 5C 24 ? 57 48 81 EC ? ? ? ? ? ? ? ? 48 8D 8C 24";
constexpr const char* kForceButtonsDownSig =
    "40 53 57 41 56 48 81 EC ? ? ? ? 48 83 79 ? 00";
constexpr const char* kGetViewAnglesSig =
    "4C 8B C1 85 D2 74 ? 48 8D 05";
constexpr const char* kSetViewAnglesSig =
    "85 D2 75 ? 48 63 81";

// ---- Resolution state ------------------------------------------------------
std::atomic<bool>  g_init_ran{false};
ResolvedRVAs       g_rvas{};

// Resolved function pointers.
using IsInGameFn      = bool(__fastcall*)();
using IsConnectedFn   = char(__fastcall*)();
using LevelShutdownFn = void*(__fastcall*)();

IsInGameFn          g_is_in_game_fn         = nullptr;
IsConnectedFn       g_is_connected_fn       = nullptr;
ComputeRandomSeedFn g_compute_random_seed   = nullptr;
ForceButtonsDownFn  g_force_buttons_down    = nullptr;
GetViewAnglesFn     g_get_view_angles       = nullptr;
SetViewAnglesFn     g_set_view_angles       = nullptr;

// LevelShutdown hook state.
LevelShutdownFn     g_level_shutdown_orig   = nullptr;
void*               g_level_shutdown_target = nullptr;

// Shutdown / init pairing flag.  True after LevelShutdown fires; cleared
// on notify_level_init_complete().
std::atomic<bool>   g_shutting_down{false};

// Resolve one address by pattern in given module.  Returns nullptr on miss.
void* find(HMODULE m, const char* sig) noexcept
{
    if (!m || !sig) return nullptr;
    return fva::scanner::find_in_module(m, sig, 0);
}

// LevelShutdown detour — sets shutting_down flag, forwards to original.
void* __fastcall level_shutdown_detour() noexcept
{
    g_shutting_down.store(true, std::memory_order_release);
    std::printf("[fva_recon][engine2] LevelShutdown → g_shutting_down=1 "
                "(view-angle emitter gate closed until next LevelInit)\n");
    std::fflush(stdout);
    if (g_level_shutdown_orig) return g_level_shutdown_orig();
    return nullptr;
}

} // anonymous

bool init() noexcept
{
    if (g_init_ran.exchange(true, std::memory_order_acq_rel)) return true;

    HMODULE engine2 = ::GetModuleHandleW(L"engine2.dll");
    HMODULE client  = ::GetModuleHandleW(L"client.dll");
    if (!engine2 || !client) {
        std::printf("[fva_recon][engine2] init: missing module(s) engine2=%p "
                    "client=%p — deferring\n",
                    (void*)engine2, (void*)client);
        g_init_ran.store(false, std::memory_order_release);  // retry later
        return false;
    }

    // ---- engine2.dll ------------------------------------------------------
    void* p_get_ar   = find(engine2, kGetAspectRatioSig);
    void* p_isin     = find(engine2, kIsInGameSig);
    void* p_isconn   = find(engine2, kIsConnectedSig);
    void* p_runcmd   = find(engine2, kRunCommandSig);
    void* p_getlvl   = find(engine2, kGetLevelNameSig);
    void* p_getlvls  = find(engine2, kGetLevelNameShortSig);
    void* p_connect  = find(engine2, kConnectSig);

    // ---- client.dll -------------------------------------------------------
    void* p_shutdown = find(client, kLevelShutdownSig);
    void* p_crs      = find(client, kComputeRandomSeedSig);
    void* p_fbd      = find(client, kForceButtonsDownSig);
    void* p_gva      = find(client, kGetViewAnglesSig);
    void* p_sva      = find(client, kSetViewAnglesSig);

    const auto rva_of = [](HMODULE m, void* p) -> std::uintptr_t {
        return p ? (reinterpret_cast<std::uintptr_t>(p) -
                    reinterpret_cast<std::uintptr_t>(m))
                 : 0;
    };
    g_rvas.get_aspect_ratio     = rva_of(engine2, p_get_ar);
    g_rvas.is_in_game           = rva_of(engine2, p_isin);
    g_rvas.is_connected         = rva_of(engine2, p_isconn);
    g_rvas.run_command          = rva_of(engine2, p_runcmd);
    g_rvas.get_level_name       = rva_of(engine2, p_getlvl);
    g_rvas.get_level_name_short = rva_of(engine2, p_getlvls);
    g_rvas.connect              = rva_of(engine2, p_connect);
    g_rvas.level_shutdown       = rva_of(client,  p_shutdown);

    g_is_in_game_fn       = reinterpret_cast<IsInGameFn>(p_isin);
    g_is_connected_fn     = reinterpret_cast<IsConnectedFn>(p_isconn);
    g_compute_random_seed = reinterpret_cast<ComputeRandomSeedFn>(p_crs);
    g_force_buttons_down  = reinterpret_cast<ForceButtonsDownFn>(p_fbd);
    g_get_view_angles     = reinterpret_cast<GetViewAnglesFn>(p_gva);
    g_set_view_angles     = reinterpret_cast<SetViewAnglesFn>(p_sva);
    g_level_shutdown_target = p_shutdown;

    std::printf("[fva_recon][engine2] init: sig-scan resolved:\n"
                "  engine2!GetAspectRatio        RVA 0x%zX\n"
                "  engine2!IsInGame              RVA 0x%zX  ptr=%p\n"
                "  engine2!IsConnected           RVA 0x%zX  ptr=%p\n"
                "  engine2!RunCommand            RVA 0x%zX\n"
                "  engine2!GetLevelName          RVA 0x%zX\n"
                "  engine2!GetLevelNameShort     RVA 0x%zX\n"
                "  engine2!Connect               RVA 0x%zX\n"
                "  client!LevelShutdown          RVA 0x%zX  ptr=%p\n"
                "  client!ComputeRandomSeed      RVA 0x%zX  ptr=%p\n"
                "  client!ForceButtonsDown       RVA 0x%zX  ptr=%p\n"
                "  client!GetViewAngles          RVA 0x%zX  ptr=%p\n"
                "  client!SetViewAngles          RVA 0x%zX  ptr=%p\n",
                g_rvas.get_aspect_ratio,
                g_rvas.is_in_game, p_isin,
                g_rvas.is_connected, p_isconn,
                g_rvas.run_command,
                g_rvas.get_level_name,
                g_rvas.get_level_name_short,
                g_rvas.connect,
                g_rvas.level_shutdown, p_shutdown,
                rva_of(client, p_crs), p_crs,
                rva_of(client, p_fbd), p_fbd,
                rva_of(client, p_gva), p_gva,
                rva_of(client, p_sva), p_sva);
    std::fflush(stdout);

    return true;
}

bool is_in_game() noexcept
{
    if (!g_is_in_game_fn) return false;
    bool r = false;
    __try { r = g_is_in_game_fn(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = false; }
    return r;
}

bool is_connected() noexcept
{
    if (!g_is_connected_fn) return false;
    bool r = false;
    __try {
        // IsConnected returns char (bool-like) — cast explicitly.
        r = (g_is_connected_fn() != 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = false; }
    return r;
}

bool is_stable_gameplay() noexcept
{
    if (g_shutting_down.load(std::memory_order_acquire)) return false;
    if (!is_connected()) return false;
    if (!is_in_game())   return false;
    return true;
}

void notify_level_init_complete() noexcept
{
    if (g_shutting_down.exchange(false, std::memory_order_acq_rel)) {
        std::printf("[fva_recon][engine2] LevelInit tail → g_shutting_down=0 "
                    "(view-angle emitter gate re-armed)\n");
        std::fflush(stdout);
    }
}

bool install_level_shutdown_hook() noexcept
{
    if (g_level_shutdown_orig) return true;
    if (!g_level_shutdown_target) {
        // Try one more resolution in case client.dll wasn't loaded during init.
        HMODULE client = ::GetModuleHandleW(L"client.dll");
        if (client) g_level_shutdown_target = find(client, kLevelShutdownSig);
    }
    if (!g_level_shutdown_target) {
        std::printf("[fva_recon][engine2] LevelShutdown hook: target unresolved\n");
        std::fflush(stdout);
        return false;
    }
    const auto rc = MH_CreateHook(g_level_shutdown_target,
                                    reinterpret_cast<void*>(&level_shutdown_detour),
                                    reinterpret_cast<void**>(&g_level_shutdown_orig));
    if (rc != MH_OK) {
        std::printf("[fva_recon][engine2] LevelShutdown MH_CreateHook rc=%d\n", rc);
        std::fflush(stdout);
        return false;
    }
    const auto rc2 = MH_EnableHook(g_level_shutdown_target);
    if (rc2 != MH_OK) {
        std::printf("[fva_recon][engine2] LevelShutdown MH_EnableHook rc=%d\n", rc2);
        std::fflush(stdout);
        MH_RemoveHook(g_level_shutdown_target);
        g_level_shutdown_orig = nullptr;
        return false;
    }
    std::printf("[fva_recon][engine2] LevelShutdown hook installed @ %p\n",
                g_level_shutdown_target);
    std::fflush(stdout);
    return true;
}

void uninstall_level_shutdown_hook() noexcept
{
    if (!g_level_shutdown_target || !g_level_shutdown_orig) return;
    MH_DisableHook(g_level_shutdown_target);
    MH_RemoveHook(g_level_shutdown_target);
    g_level_shutdown_orig = nullptr;
}

const ResolvedRVAs& resolved() noexcept { return g_rvas; }

ComputeRandomSeedFn compute_random_seed() noexcept { return g_compute_random_seed; }
ForceButtonsDownFn  force_buttons_down()  noexcept { return g_force_buttons_down;  }
GetViewAnglesFn     get_view_angles()     noexcept { return g_get_view_angles;     }
SetViewAnglesFn     set_view_angles()     noexcept { return g_set_view_angles;     }

} // namespace fva::hooks::engine2
