


#include "input_scratch_mirror.h"

#include "../core/signature_scanner.h"
#include "../schema/cbaseusercmdpb_layout.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fva::hooks::input_scratch_mirror
{

namespace {



using AllocFn = void*(__fastcall*)(void*);

std::atomic<AllocFn> g_new_button_state{nullptr};
std::atomic<AllocFn> g_new_qangle{nullptr};
std::atomic<bool>    g_used_heap_fallback{false};



std::atomic<valve::pb::raw::CInButtonStatePB*> g_buttons{nullptr};
std::atomic<valve::pb::raw::CMsgQAngle*>       g_angles{nullptr};





constexpr valve::pb::raw::CMsgQAngle k_zero_qangle{ {nullptr, nullptr}, 0, 0, 0.0f, 0.0f, 0.0f };







bool resolve_new_wrapper(const char*                         tag,
                          std::uint8_t*                       client_base,
                          std::uintptr_t                      rva,
                          const std::uint8_t*                 prologue,
                          std::size_t                         prologue_len,
                          const std::uint8_t*                 size_imm,
                          std::size_t                         size_imm_len,
                          std::atomic<AllocFn>&               out)
{
    auto* p = client_base + rva;
    std::printf("[fva_recon] input_scratch_mirror %s @ RVA 0x%zX  first=%02X %02X %02X %02X %02X  "
                 "at+0x14=%02X %02X %02X %02X %02X\n",
                 tag, rva, p[0], p[1], p[2], p[3], p[4],
                 p[0x14], p[0x15], p[0x16], p[0x17], p[0x18]);
    if (std::memcmp(p, prologue, prologue_len) != 0) {
        std::printf("[fva_recon] input_scratch_mirror %s: prologue mismatch (got %02X %02X %02X %02X %02X)\n",
                     tag, p[0], p[1], p[2], p[3], p[4]);
        return false;
    }
    if (std::memcmp(p + 0x14, size_imm, size_imm_len) != 0) {
        std::printf("[fva_recon] input_scratch_mirror %s: size-imm mismatch at +0x14 (got %02X %02X %02X %02X %02X)\n",
                     tag, p[0x14], p[0x15], p[0x16], p[0x17], p[0x18]);
        return false;
    }
    out.store(reinterpret_cast<AllocFn>(p), std::memory_order_release);
    return true;
}

valve::pb::raw::CInButtonStatePB* ensure_buttons()
{
    if (auto* p = g_buttons.load(std::memory_order_acquire)) return p;
    auto fn = g_new_button_state.load(std::memory_order_acquire);
    if (!fn) return nullptr;

    
    
    auto* fresh = static_cast<valve::pb::raw::CInButtonStatePB*>(fn(nullptr));
    if (!fresh) return nullptr;

    valve::pb::raw::CInButtonStatePB* expected = nullptr;
    if (g_buttons.compare_exchange_strong(expected, fresh,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return fresh;
    
    
    return expected;
}

valve::pb::raw::CMsgQAngle* ensure_angles()
{
    if (auto* p = g_angles.load(std::memory_order_acquire)) return p;
    auto fn = g_new_qangle.load(std::memory_order_acquire);
    if (!fn) return nullptr;

    auto* fresh = static_cast<valve::pb::raw::CMsgQAngle*>(fn(nullptr));
    if (!fresh) return nullptr;

    valve::pb::raw::CMsgQAngle* expected = nullptr;
    if (g_angles.compare_exchange_strong(expected, fresh,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return fresh;
    return expected;
}

} 

namespace {






























constexpr std::string_view k_protobuf_new_prologue =
    "48 89 5C 24 ? 57 48 83 EC ? 33 DB 48 8B F9 48 85 C9 75 ? B9 ? ? ? ? E8";



AllocFn scan_for_new(std::uint8_t* client_base, std::uint8_t size_marker,
                     const char* tag)
{
    std::size_t off = 0;
    for (int attempt = 0; attempt < 32; ++attempt) {
        
        
        auto* hit = fva::scanner::find_in_module(client_base + off,
                                                    k_protobuf_new_prologue);
        if (!hit) {
            std::printf("[fva_recon] input_scratch_mirror %s: fallback sig-scan miss on attempt %d "
                         "(off=0x%zX, prologue not present in module)\n",
                         tag, attempt, off);
            return nullptr;
        }

        
        
        
        
        
        for (int i = 0; i < 48; ++i) {
            std::uint8_t* p = hit + i;
            const bool edx_match =
                p[0] == 0xBA && p[1] == size_marker && p[2] == 0 &&
                p[3] == 0    && p[4] == 0;
            const bool r8d_match =
                p[0] == 0x41 && p[1] == 0xB8 &&
                p[2] == size_marker && p[3] == 0 && p[4] == 0 && p[5] == 0;
            const bool ecx_match =
                p[0] == 0xB9 && p[1] == size_marker && p[2] == 0 &&
                p[3] == 0    && p[4] == 0;
            if (edx_match || r8d_match || ecx_match) {
                std::printf("[fva_recon] input_scratch_mirror %s: fallback sig-scan hit @ %p "
                             "(size_imm at +0x%X, form=%s)\n",
                             tag, (void*)hit, i,
                             edx_match ? "mov edx" : r8d_match ? "mov r8d"
                                                                : "mov ecx");
                return reinterpret_cast<AllocFn>(hit);
            }
        }

        
        
        off = static_cast<std::size_t>(hit - client_base) + 1;
    }
    std::printf("[fva_recon] input_scratch_mirror %s: fallback sig-scan exhausted 32 "
                 "candidates without a size_marker hit\n", tag);
    return nullptr;
}














void* heap_new_button_state(void* )
{
    return ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, 0x30);
}
void* heap_new_qangle(void* )
{
    return ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, 0x28);
}

bool resolve_via_fallback(const char* tag, std::uint8_t* client_base,
                           std::uint8_t size_marker,
                           std::atomic<AllocFn>& out)
{
    AllocFn fn = scan_for_new(client_base, size_marker, tag);
    if (!fn) return false;
    out.store(fn, std::memory_order_release);
    return true;
}

} 


bool init()
{
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return false;
    auto* base = reinterpret_cast<std::uint8_t*>(client);

    namespace bf = fva::version::build_fingerprint;
    namespace kr = fva::version::known_rva;

    
    
    
    
    if (!resolve_new_wrapper("CInButtonStatePB::New", base, kr::new_maybe_arena_cinbuttonstatepb,
                              bf::kNewCInButtonStatePrologue.data(),
                              bf::kNewCInButtonStatePrologue.size(),
                              bf::kNewCInButtonStateSizeImm.data(),
                              bf::kNewCInButtonStateSizeImm.size(),
                              g_new_button_state))
    {
        std::printf("[fva_recon] input_scratch_mirror: manifest RVA drift for "
                     "CInButtonStatePB::New — trying sig-scan fallback\n");
        if (!resolve_via_fallback("CInButtonStatePB::New", base,
                                    0x30, g_new_button_state))
        {
            std::printf("[fva_recon] input_scratch_mirror: sig-scan also missed for "
                         "CInButtonStatePB::New — installing heap fallback\n");
            g_new_button_state.store(heap_new_button_state,
                                       std::memory_order_release);
            g_used_heap_fallback.store(true, std::memory_order_release);
        }
    }

    if (!resolve_new_wrapper("CMsgQAngle::New", base, kr::new_maybe_arena_cmsgqangle,
                              bf::kNewCMsgQAnglePrologue.data(),
                              bf::kNewCMsgQAnglePrologue.size(),
                              bf::kNewCMsgQAngleSizeImm.data(),
                              bf::kNewCMsgQAngleSizeImm.size(),
                              g_new_qangle))
    {
        std::printf("[fva_recon] input_scratch_mirror: manifest RVA drift for "
                     "CMsgQAngle::New — trying sig-scan fallback\n");
        if (!resolve_via_fallback("CMsgQAngle::New", base,
                                    0x28, g_new_qangle))
        {
            std::printf("[fva_recon] input_scratch_mirror: sig-scan also missed for "
                         "CMsgQAngle::New — installing heap fallback\n");
            g_new_qangle.store(heap_new_qangle, std::memory_order_release);
            g_used_heap_fallback.store(true, std::memory_order_release);
        }
    }

    return true;
}

bool ready() noexcept
{
    return g_new_button_state.load(std::memory_order_acquire) != nullptr &&
           g_new_qangle.load(std::memory_order_acquire)       != nullptr;
}

bool used_heap_fallback() noexcept
{
    return g_used_heap_fallback.load(std::memory_order_acquire);
}


void apply(const valve::pb::raw::CUserCmd* raw_cmd) noexcept
{
    if (!raw_cmd || !ready()) return;

    
    
    
    if (auto* btn = ensure_buttons())
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(raw_cmd);
        std::uint64_t v1, v2, v3;
        std::memcpy(&v1, p + fva::version::layout::user_cmd_buttons_changed, sizeof(v1));
        std::memcpy(&v2, p + fva::version::layout::user_cmd_buttons_held,    sizeof(v2));
        std::memcpy(&v3, p + fva::version::layout::user_cmd_buttons_pressed, sizeof(v3));
        btn->buttonstate1 = v1;
        btn->buttonstate2 = v2;
        btn->buttonstate3 = v3;
        btn->has_bits |= valve::pb::raw::CINBUTTONSTATEPB_BITS_BUTTONSTATE1 |
                         valve::pb::raw::CINBUTTONSTATEPB_BITS_BUTTONSTATE2 |
                         valve::pb::raw::CINBUTTONSTATEPB_BITS_BUTTONSTATE3;
    }

    
    
    
    
    
    if (auto* ang = ensure_angles())
    {
        const auto* src = &k_zero_qangle;
        if (raw_cmd->base && raw_cmd->base->viewangles)
            src = raw_cmd->base->viewangles;
        ang->x = src->x;
        ang->y = src->y;
        ang->has_bits |= valve::pb::raw::CMSGQANGLE_BITS_X |
                         valve::pb::raw::CMSGQANGLE_BITS_Y;
    }
}

const valve::pb::raw::CInButtonStatePB* buttons_cache() noexcept
{
    return g_buttons.load(std::memory_order_acquire);
}

const valve::pb::raw::CMsgQAngle* angles_cache() noexcept
{
    return g_angles.load(std::memory_order_acquire);
}

} 
