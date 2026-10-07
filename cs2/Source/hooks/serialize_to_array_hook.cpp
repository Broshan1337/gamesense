












#include "serialize_to_array_hook.h"
#include "capture_buffer.h"
#include "../core/signature_scanner.h"
#include "../core/obfuscation.h"
#include "../schema/cbaseusercmdpb_layout.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>

namespace fva::hooks
{

namespace {

using SerializeFn = char(__fastcall*)(void*, void*, int);
SerializeFn g_original = nullptr;




void* s_installed_target = nullptr;








void* s_target_vtable = nullptr;

using MergeFromFn   = void(__fastcall*)(void*, const void*);
using ClearFn       = void(__fastcall*)(void*);

inline bool is_target_message(void* msg) noexcept
{
    if (!msg || !s_target_vtable) return false;
    return *reinterpret_cast<void**>(msg) == s_target_vtable;
}

inline void clear_and_merge_from(void* dst, const void* src)
{
    auto* vtbl = *reinterpret_cast<void***>(const_cast<void*>(src));
    auto  clear = reinterpret_cast<ClearFn>(
        vtbl[fva::version::protobuf::vtbl_clear / sizeof(void*)]);
    auto  merge = reinterpret_cast<MergeFromFn>(
        vtbl[fva::version::protobuf::vtbl_merge_from / sizeof(void*)]);
    clear(dst);
    merge(dst, src);
}



inline bool is_self_scratch(const void* self)
{
    return self == static_cast<const void*>(&capture_buffer::g_scratch);
}

} 








inline void log_unique_vtable(void* self) noexcept
{
    if (!self) return;
    auto* vt = *reinterpret_cast<void**>(self);
    static std::atomic<void*> s_seen[8]{};
    for (int i = 0; i < 8; ++i) {
        auto* expected = s_seen[i].load(std::memory_order_relaxed);
        if (expected == vt) return;   
        if (expected == nullptr &&
            s_seen[i].compare_exchange_strong(expected, vt,
                std::memory_order_acq_rel))
        {
            HMODULE client = ::GetModuleHandleW(
                fva::version::module_names::client_dll);
            const auto rva = client
                ? reinterpret_cast<std::uintptr_t>(vt) -
                  reinterpret_cast<std::uintptr_t>(client)
                : 0;
            const char* tag = (vt == s_target_vtable)
                ? " [CBaseUserCmdPB — target]"
                : "";
            std::printf("[fva_recon][Hook-C] vtable seen: %p (RVA 0x%zX)%s\n",
                        vt, rva, tag);
            return;
        }
    }
}




char __fastcall hook_body(void* self, void* stream, int max_size)
{
    log_unique_vtable(self);

    
    if (!self || is_self_scratch(self) || !is_target_message(self))
    {
        return g_original ? g_original(self, stream, max_size) : 0;
    }

    
    
    capture_buffer::cache_vtable_from(self);

    
    clear_and_merge_from(&capture_buffer::g_scratch, self);
    capture_buffer::publish();

    
    
    
    const char rc = g_original ? g_original(self, stream, max_size) : 0;
    {
        static std::atomic<int> s_dumps{0};
        if (s_dumps.fetch_add(1, std::memory_order_relaxed) < 3) {
            const auto* bytes = static_cast<const std::uint8_t*>(stream);
            const int n = max_size < 128 ? max_size : 128;
            std::printf("[fva_recon][Hook-C] wire snapshot len=%d bytes: ", n);
            for (int i = 0; i < n; ++i) std::printf("%02X ", bytes[i]);
            std::printf("\n");
        }
    }
    return rc;
}





bool install_serialize_to_array_hook()
{
    if (g_original) return true;
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return false;

    
    
    
    s_target_vtable = reinterpret_cast<std::uint8_t*>(client) +
                      fva::version::known_rva::vtable_cbaseusercmdpb;
    std::printf("[fva_recon] Hook C target vtable=%p (RVA 0x%zX)\n",
                 s_target_vtable,
                 static_cast<std::size_t>(
                     fva::version::known_rva::vtable_cbaseusercmdpb));

    
    
    
    static constexpr std::string_view kSigLive =
        "48 89 5C 24 18 55 56 57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4";
    void* target = fva::scanner::find_in_module(client, kSigLive);
    if (!target)
        target = reinterpret_cast<std::uint8_t*>(client) +
                 fva::version::known_rva::hook_c_target;

    const auto rva = reinterpret_cast<std::uintptr_t>(target) -
                     reinterpret_cast<std::uintptr_t>(client);
    const auto* b  = reinterpret_cast<const std::uint8_t*>(target);
    std::printf("[fva_recon] Hook C resolved target=%p (RVA 0x%zX) "
                 "prologue=%02X %02X %02X %02X %02X\n",
                 target, rva, b[0], b[1], b[2], b[3], b[4]);

    
    
    
    
    
    
    namespace bf = fva::version::build_fingerprint;
    if (std::memcmp(target, bf::kHookCPrePatch.data(),
                     bf::kHookCPrePatch.size()) != 0) {
        std::printf("[fva_recon] Hook C fingerprint MISMATCH — refusing "
                    "to install (target bytes don't look like "
                    "SerializePartialToArray prologue)\n");
        return false;
    }

    if (MH_CreateHook(target, reinterpret_cast<void*>(&hook_body),
                       reinterpret_cast<void**>(&g_original)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) return false;
    s_installed_target = target;
    return true;
}

void uninstall_serialize_to_array_hook()
{
    if (!g_original || !s_installed_target) return;
    MH_DisableHook(s_installed_target);
    MH_RemoveHook(s_installed_target);
    s_installed_target = nullptr;
    g_original = nullptr;
}

} 
