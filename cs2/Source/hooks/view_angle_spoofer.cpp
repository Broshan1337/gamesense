
























#include "view_angle_spoofer.h"
#include "engine2_bindings.h"
#include "../core/signature_scanner.h"
#include "../schema/cbaseusercmdpb_layout.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>




namespace fva::log
{
    void* find_arena_allocator();
    void* find_csubtick_vtable();
}

namespace fva::view_angle_spoofer
{

namespace {

std::atomic<bool>  g_ready{false};




std::atomic<std::uint32_t> g_target_bits[3]{};
std::atomic<bool>          g_target_armed{false};









using ArenaAllocFn = void*(__fastcall*)(void*, std::size_t, const void*);
std::atomic<ArenaAllocFn> g_arena_alloc{nullptr};

inline std::uint32_t bits_of(float f) noexcept {
    std::uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u;
}
inline float float_of(std::uint32_t u) noexcept {
    float f; std::memcpy(&f, &u, sizeof(f)); return f;
}



float wrap180(float dx) noexcept
{
    float m = std::remainderf(dx, 360.0f);
    if      (m >  180.0f) m -= 360.0f;
    else if (m < -180.0f) m += 360.0f;
    return m;
}












struct IMemAllocOpaque;

IMemAllocOpaque* resolve_cs2_mem_alloc() noexcept
{
    static std::atomic<IMemAllocOpaque*> s_cached{nullptr};
    auto* c = s_cached.load(std::memory_order_acquire);
    if (c) return c;
    HMODULE tier0 = ::GetModuleHandleW(L"tier0.dll");
    if (!tier0) return nullptr;
    auto pp = reinterpret_cast<IMemAllocOpaque**>(
        ::GetProcAddress(tier0, "g_pMemAlloc"));
    if (!pp || !*pp) return nullptr;
    s_cached.store(*pp, std::memory_order_release);
    std::printf("[fva_recon][ViewSpoof] IMemAlloc resolved via tier0!g_pMemAlloc → %p\n",
                (void*)*pp);
    std::fflush(stdout);
    return *pp;
}

void* cs2_alloc(std::size_t size) noexcept
{
    auto* ma = resolve_cs2_mem_alloc();
    if (!ma) return nullptr;
    auto** vt = *reinterpret_cast<void***>(ma);
    using AllocFn = void*(__fastcall*)(void*, std::size_t);
    void* r = nullptr;
    __try { r = reinterpret_cast<AllocFn>(vt[1])(ma, size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
    return r;
}

void cs2_free(void* ptr) noexcept
{
    if (!ptr) return;
    auto* ma = resolve_cs2_mem_alloc();
    if (!ma) return;
    auto** vt = *reinterpret_cast<void***>(ma);
    using FreeFn = void(__fastcall*)(void*, void*);
    __try { reinterpret_cast<FreeFn>(vt[3])(ma, ptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}




























template<typename T>
void** rep_grow(valve::pb::raw::RepeatedPtrField_t<T>* subs) noexcept
{
    using RepT = typename valve::pb::raw::RepeatedPtrField_t<T>::Rep_t;

    const int current   = subs->current_size;
    const int old_total = subs->total_size;
    const int needed    = current + 1;

    if (old_total >= needed) {
        return subs->rep
            ? reinterpret_cast<void**>(&subs->rep->elements[current])
            : nullptr;
    }

    
    
    
    
    
    int new_cap;
    if (needed >= 1) {
        if (old_total <= 1073741819) {
            new_cap = 2 * old_total + 1;
            if (new_cap < needed) new_cap = needed;
        } else {
            new_cap = 0x7FFFFFFF;
        }
    } else {
        new_cap = 1;
    }

    const std::size_t new_bytes = 8 + std::size_t(new_cap) * sizeof(void*);
    void*  arena       = subs->arena;
    void*  new_raw     = nullptr;
    bool   arena_owned = false;

    
    
    if (arena) {
        ArenaAllocFn afn = g_arena_alloc.load(std::memory_order_acquire);
        if (!afn) {
            void* p = fva::log::find_arena_allocator();
            if (p) {
                afn = reinterpret_cast<ArenaAllocFn>(p);
                g_arena_alloc.store(afn, std::memory_order_release);
            }
        }
        if (afn) {
            __try {
                
                
                
                
                new_raw = afn(arena, new_bytes, nullptr);
                if (new_raw) arena_owned = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                new_raw = nullptr;
            }
        }
    }

    
    if (!new_raw) {
        new_raw = cs2_alloc(new_bytes);
    }
    if (!new_raw) return nullptr;

    
    std::memset(new_raw, 0, new_bytes);

    RepT* new_rep = reinterpret_cast<RepT*>(new_raw);
    RepT* old_rep = subs->rep;

    if (old_rep) {
        int old_allocated = 0;
        __try {
            old_allocated = old_rep->allocated_size;
            if (old_allocated < 0) old_allocated = 0;
            if (old_allocated > old_total) old_allocated = old_total;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            old_allocated = 0;
        }

        new_rep->allocated_size = old_allocated;

        if (old_allocated > 0) {
            __try {
                std::memcpy(&new_rep->elements[0], &old_rep->elements[0],
                            std::size_t(old_allocated) * sizeof(void*));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                
                
                
                new_rep->allocated_size = 0;
            }
        }

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        (void)old_rep;
    } else {
        new_rep->allocated_size = 0;
    }

    subs->rep        = new_rep;
    subs->total_size = new_cap;

    return reinterpret_cast<void**>(&new_rep->elements[current]);
}



template<typename T>
void** rep_grow_heap(valve::pb::raw::RepeatedPtrField_t<T>* subs) noexcept
{
    return rep_grow(subs);
}





















template<typename T>
bool add_allocated_slow_path(valve::pb::raw::RepeatedPtrField_t<T>* subs,
                              T* step) noexcept
{
    using RepT = typename valve::pb::raw::RepeatedPtrField_t<T>::Rep_t;

    if (!subs || !step) return false;

    RepT* rep = subs->rep;
    const int current = subs->current_size;
    const int total   = subs->total_size;

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    __try {
        
        
        
        
        if (!rep || current == total) {
            void** slot = rep_grow(subs);
            if (!slot) return false;
            rep = subs->rep;
            if (!rep) return false;
            
            rep->allocated_size = rep->allocated_size + 1;
            rep->elements[current] = step;
            subs->current_size    = current + 1;
            return true;
        }

        
        const int alloc = rep->allocated_size;

        
        
        
        
        if (alloc != total) {
            if (current >= alloc) {
                rep->allocated_size = alloc + 1;
            } else {
                
                
                rep->elements[alloc] = rep->elements[current];
                rep->allocated_size  = alloc + 1;
            }
            rep->elements[current] = step;
            subs->current_size    = current + 1;
            return true;
        }

        
        
        
        
        
        
        
        
        
        
        
        rep->elements[current] = step;
        subs->current_size    = current + 1;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}














std::atomic<void*> g_csubtick_vtable{nullptr};
std::atomic<void*> g_cmsgqangle_vtable{nullptr};

void try_cache_vtables(valve::pb::raw::CBaseUserCmdPB* pb,
                       valve::pb::raw::RepeatedPtrField_t<valve::pb::raw::CSubtickMoveStep>* subs) noexcept
{
    if (!g_csubtick_vtable.load(std::memory_order_relaxed) && subs->rep) {
        for (int i = 0; i < subs->current_size; ++i) {
            auto* step = subs->rep->elements[i];
            if (step && step->vtable) {
                g_csubtick_vtable.store(step->vtable, std::memory_order_release);
                std::printf("[fva_recon][ViewSpoof] cached CS2 CSubtickMoveStep vtable @ %p from live entry\n",
                            step->vtable);
                break;
            }
        }
    }
    if (!g_cmsgqangle_vtable.load(std::memory_order_relaxed)
        && pb->viewangles && pb->viewangles->vtable)
    {
        g_cmsgqangle_vtable.store(pb->viewangles->vtable, std::memory_order_release);
        std::printf("[fva_recon][ViewSpoof] cached CS2 CMsgQAngle vtable @ %p from pb->viewangles\n",
                    pb->viewangles->vtable);
    }
}






















using ProtoNewFn = void*(__fastcall*)(void* );
std::atomic<ProtoNewFn> g_cs2_csubtick_new{nullptr};
std::atomic<ProtoNewFn> g_cs2_input_history_new{nullptr};
std::atomic<ProtoNewFn> g_cs2_cmsgqangle_new{nullptr};









constexpr std::string_view k_csubtick_new_sig =
    "48 89 5C 24 ? 57 48 83 EC 20 33 DB 48 8B F9 48 85 C9 75 ? B9 38 00 00 00 E8";
constexpr std::string_view k_input_history_new_sig =
    "48 89 5C 24 ? 57 48 83 EC 20 33 DB 48 8B F9 48 85 C9 75 ? B9 78 00 00 00 E8";
constexpr std::string_view k_cmsgqangle_new_sig =
    "48 89 5C 24 ? 57 48 83 EC 20 33 DB 48 8B F9 48 85 C9 75 ? B9 28 00 00 00 E8";

ProtoNewFn resolve_cs2_csubtick_new() noexcept
{
    ProtoNewFn cached = g_cs2_csubtick_new.load(std::memory_order_acquire);
    if (cached) return cached;
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;

    
    
    
    constexpr std::uintptr_t k_known_rva =
        fva::version::known_rva::new_maybe_arena_csubtickmovestep;
    auto* try_at = reinterpret_cast<std::uint8_t*>(client) + k_known_rva;
    const std::uint8_t expected[8] = {0x48, 0x89, 0x5C, 0x24, 0x10,
                                      0x57, 0x48, 0x83};
    const std::uint8_t size_imm[5] = {0xB9, 0x38, 0x00, 0x00, 0x00};
    if (std::memcmp(try_at, expected, 8) == 0 &&
        std::memcmp(try_at + 0x14, size_imm, 5) == 0) {
        auto fn = reinterpret_cast<ProtoNewFn>(try_at);
        g_cs2_csubtick_new.store(fn, std::memory_order_release);
        std::printf("[fva_recon][ViewSpoof] resolved CS2 CSubtickMoveStep::New "
                    "@ %p (RVA 0x%zX) [known-RVA + size 0x38 verified]\n",
                    (void*)try_at, k_known_rva);
        return fn;
    }
    std::printf("[fva_recon][ViewSpoof] CSubtickMoveStep::New: RVA 0x%zX fingerprint "
                "MISS — REFUSING sig fallback.  Update manifest.\n",
                k_known_rva);
    return nullptr;
}

ProtoNewFn resolve_cs2_input_history_new() noexcept
{
    ProtoNewFn cached = g_cs2_input_history_new.load(std::memory_order_acquire);
    if (cached) return cached;
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;

    
    
    
    
    
    
    
    
    
    constexpr std::uintptr_t k_known_rva =
        fva::version::known_rva::new_maybe_arena_csgoinputhistoryentrypb;
    auto* try_at = reinterpret_cast<std::uint8_t*>(client) + k_known_rva;

    
    const std::uint8_t expected[8] = {0x48, 0x89, 0x5C, 0x24, 0x10,
                                      0x57, 0x48, 0x83};
    if (std::memcmp(try_at, expected, 8) == 0) {
        
        
        
        const std::uint8_t size_imm[5] = {0xB9, 0x78, 0x00, 0x00, 0x00};
        if (std::memcmp(try_at + 0x14, size_imm, 5) == 0) {
            auto fn = reinterpret_cast<ProtoNewFn>(try_at);
            g_cs2_input_history_new.store(fn, std::memory_order_release);
            std::printf("[fva_recon][ViewSpoof] resolved CS2 CSGOInputHistoryEntryPB::New "
                        "@ %p (RVA 0x%zX) [known-RVA + size 0x78 verified]\n",
                        (void*)try_at, k_known_rva);
            return fn;
        }
    }

    
    
    
    std::printf("[fva_recon][ViewSpoof] CSGOInputHistoryEntryPB::New: RVA 0x%zX "
                "fingerprint MISS — REFUSING sig fallback (would corrupt "
                "cs2 Rep with wrong-type allocator, causing 1 FPS freeze). "
                "Update manifest known_rva::new_maybe_arena_csgoinputhistoryentrypb "
                "for this depot.\n",
                k_known_rva);
    return nullptr;
}

ProtoNewFn resolve_cs2_cmsgqangle_new() noexcept
{
    ProtoNewFn cached = g_cs2_cmsgqangle_new.load(std::memory_order_acquire);
    if (cached) return cached;
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;

    
    
    constexpr std::uintptr_t k_known_rva =
        fva::version::known_rva::new_maybe_arena_cmsgqangle;
    auto* try_at = reinterpret_cast<std::uint8_t*>(client) + k_known_rva;
    const std::uint8_t expected[8] = {0x48, 0x89, 0x5C, 0x24, 0x10,
                                      0x57, 0x48, 0x83};
    if (std::memcmp(try_at, expected, 8) == 0) {
        
        const std::uint8_t size_imm[5] = {0xB9, 0x28, 0x00, 0x00, 0x00};
        if (std::memcmp(try_at + 0x14, size_imm, 5) == 0) {
            auto fn = reinterpret_cast<ProtoNewFn>(try_at);
            g_cs2_cmsgqangle_new.store(fn, std::memory_order_release);
            std::printf("[fva_recon][ViewSpoof] resolved CS2 CMsgQAngle::New "
                        "@ %p (RVA 0x%zX) [known-RVA + size 0x28 verified]\n",
                        (void*)try_at, k_known_rva);
            return fn;
        }
    }

    
    std::printf("[fva_recon][ViewSpoof] CMsgQAngle::New: RVA 0x%zX fingerprint MISS "
                "— REFUSING sig fallback.  Update manifest "
                "known_rva::new_maybe_arena_cmsgqangle for this depot.\n",
                k_known_rva);
    return nullptr;
}



static inline void legacy_cmsgqangle_sig_scan_reference() noexcept
{
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return;
    void* p = fva::scanner::find_in_module(client, k_cmsgqangle_new_sig, 0);
    if (!p) return;
    auto fn = reinterpret_cast<ProtoNewFn>(p);
    g_cs2_cmsgqangle_new.store(fn, std::memory_order_release);
    const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(p) -
                               reinterpret_cast<std::uintptr_t>(client);
    std::printf("[fva_recon][ViewSpoof] resolved CS2 CMsgQAngle::New @ %p (RVA 0x%zX) [sig fallback]\n",
                p, rva);
    (void)fn;
}









using MallocFn = void*(__cdecl*)(std::size_t);
using FreeFn   = void  (__cdecl*)(void*);
std::atomic<MallocFn> g_ucrt_malloc{nullptr};

MallocFn resolve_ucrt_malloc() noexcept
{
    MallocFn cached = g_ucrt_malloc.load(std::memory_order_acquire);
    if (cached) return cached;
    HMODULE ucrt = ::GetModuleHandleW(L"ucrtbase.dll");
    if (!ucrt) ucrt = ::LoadLibraryW(L"ucrtbase.dll");
    if (!ucrt) return nullptr;
    auto fn = reinterpret_cast<MallocFn>(::GetProcAddress(ucrt, "malloc"));
    if (fn) {
        g_ucrt_malloc.store(fn, std::memory_order_release);
        std::printf("[fva_recon][ViewSpoof] resolved ucrtbase malloc @ %p (Path 1)\n", (void*)fn);
    }
    return fn;
}

















void* untag_arena_word(std::uintptr_t tagged) noexcept
{
    void* base = reinterpret_cast<void*>(tagged & ~std::uintptr_t{3});
    if (tagged & 1) return *reinterpret_cast<void**>(base);
    return base;
}

void* chain_find_arena(valve::pb::raw::CUserCmd* raw,
                       valve::pb::raw::CBaseUserCmdPB* pb,
                       valve::pb::raw::RepeatedPtrField_t<valve::pb::raw::CSubtickMoveStep>* subs) noexcept
{
    if (subs->arena) return subs->arena;
    if (raw->arena_word) {
        void* a = untag_arena_word(raw->arena_word);
        if (a) return a;
    }
    if (pb->viewangles) {
        
        auto* v = reinterpret_cast<std::uint8_t*>(pb->viewangles);
        void* a = *reinterpret_cast<void**>(v + 8);
        if (a) return a;
    }
    if (subs->rep) {
        for (int i = 0; i < subs->current_size; ++i) {
            auto* step = subs->rep->elements[i];
            if (!step) continue;
            auto* s = reinterpret_cast<std::uint8_t*>(step);
            void* a = *reinterpret_cast<void**>(s + 8);
            if (a) return a;
        }
    }
    if (pb->buttons_pb) {
        auto* b = reinterpret_cast<std::uint8_t*>(pb->buttons_pb);
        void* a = *reinterpret_cast<void**>(b + 8);
        if (a) return a;
    }
    return nullptr;
}




void* alloc_bytes(void* preferred_arena, std::size_t size) noexcept
{
    
    ArenaAllocFn fn = g_arena_alloc.load(std::memory_order_acquire);
    if (!fn) {
        void* p = fva::log::find_arena_allocator();
        if (p) {
            fn = reinterpret_cast<ArenaAllocFn>(p);
            g_arena_alloc.store(fn, std::memory_order_release);
        }
    }
    void* mem = nullptr;
    if (preferred_arena && fn) {
        mem = fn(preferred_arena, size, nullptr);
        if (mem) {
            std::memset(mem, 0, size);
            return mem;
        }
    }

    
    MallocFn mf = resolve_ucrt_malloc();
    if (mf) {
        mem = mf(size);
        if (mem) {
            std::memset(mem, 0, size);
            return mem;
        }
    }

    
    
    return ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}












valve::pb::raw::CSubtickMoveStep* alloc_and_init_step(void* arena) noexcept
{
    ProtoNewFn fn = resolve_cs2_input_history_new();
    if (!fn) return nullptr;
    return reinterpret_cast<valve::pb::raw::CSubtickMoveStep*>(fn(arena));
}



valve::pb::raw::CMsgQAngle* alloc_and_init_qangle(void* arena) noexcept
{
    ProtoNewFn fn = resolve_cs2_cmsgqangle_new();
    if (!fn) return nullptr;
    return reinterpret_cast<valve::pb::raw::CMsgQAngle*>(fn(arena));
}




























bool append_step_fallback_v1(valve::pb::raw::RepeatedPtrField_t<valve::pb::raw::CSubtickMoveStep>* subs,
                              float pitch_delta, float yaw_delta) noexcept
{
    using namespace valve::pb::raw;

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (!subs->rep) return false;
    if (subs->current_size >= fva::version::layout::subtick_moves_capacity) return false;
    
    
    
    
    
    
    
    if (subs->current_size >= subs->total_size) return false;

    
    
    
    
    
    
    
    auto* raw_bytes  = reinterpret_cast<std::uint8_t*>(subs)
                     - fva::version::layout::user_cmd_subtick_moves_field;
    auto* raw        = reinterpret_cast<CUserCmd*>(raw_bytes);
    CBaseUserCmdPB* pb = raw->base;
    void* arena = chain_find_arena(raw, pb, subs);

    
    
    
    void* entry_raw = alloc_and_init_step(arena);
    if (!entry_raw) return false;
    auto* entry = reinterpret_cast<std::uint8_t*>(entry_raw);

    
    
    ProtoNewFn qfn = resolve_cs2_cmsgqangle_new();
    if (!qfn) return false;
    auto* qangle = reinterpret_cast<std::uint8_t*>(qfn(arena));
    if (!qangle) return false;

    
    *reinterpret_cast<float*>(qangle + 0x18)    = pitch_delta;
    *reinterpret_cast<float*>(qangle + 0x1C)    = yaw_delta;
    *reinterpret_cast<float*>(qangle + 0x20)    = 0.0f;
    *reinterpret_cast<std::uint32_t*>(qangle + 0x10) |= 0x7u;  

    
    *reinterpret_cast<void**>(entry + 0x18) = qangle;

    
    
    
    
    
    
    
    
    *reinterpret_cast<std::int32_t*>(entry + 0x60) = 0;      
    *reinterpret_cast<float*>(entry + 0x64) = 1.0f;          
    *reinterpret_cast<std::uint64_t*>(entry + 0x68) = 0;     

    
    
    
    
    
    
    
    *reinterpret_cast<std::uint32_t*>(entry + 0x10) |= 0x1E01u;

    
    
    
    
    
    
    subs->rep->elements[subs->current_size] =
        reinterpret_cast<CSubtickMoveStep*>(entry_raw);
    subs->current_size += 1;
    if (subs->rep->allocated_size < subs->current_size) {
        subs->rep->allocated_size = subs->current_size;
    }
    return true;
}

#if 0  
namespace preserved {
inline void old_wrong_type() {
    CSubtickMoveStep* step = alloc_and_init_step(arena);
    if (!step) return false;

    
    
    
    
    
    
    
    
    
    
    
    step->pitch_delta = pitch_delta;
    step->yaw_delta   = yaw_delta;
    step->when        = 1.0f;
    step->has_bits   |= CSUBTICKMOVESTEP_BITS_WHEN
                    |  CSUBTICKMOVESTEP_BITS_PITCH_DELTA
                    |  CSUBTICKMOVESTEP_BITS_YAW_DELTA;

    
    
    
    
    
    
    
    
    
    
    return;
}
}  
#endif

} 

bool init()
{
    g_ready.store(true, std::memory_order_release);
    return true;
}

void set_target_angle(float pitch, float yaw, float roll) noexcept
{
    g_target_bits[0].store(bits_of(pitch), std::memory_order_relaxed);
    g_target_bits[1].store(bits_of(yaw),   std::memory_order_relaxed);
    g_target_bits[2].store(bits_of(roll),  std::memory_order_relaxed);
    g_target_armed.store(true, std::memory_order_release);
}

void clear_target_angle() noexcept
{
    g_target_armed.store(false, std::memory_order_release);
}

bool ready() noexcept
{
    return g_ready.load(std::memory_order_acquire);
}















template<typename T>
bool subs_looks_safe(valve::pb::raw::RepeatedPtrField_t<T>* subs,
                     std::uint32_t call_no, bool verbose) noexcept
{
    if (!subs) return false;

    
    auto* rep = subs->rep;
    if (!rep) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u subs->rep=null → "
                        "skip view-angle emitter\n", call_no);
        return false;
    }

    
    const auto rep_ui = reinterpret_cast<std::uintptr_t>(rep);
    if (rep_ui < 0x0000010000000000ULL || rep_ui >= 0x00007FFF00000000ULL) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u rep=%p out-of-range "
                        "→ skip view-angle emitter\n", call_no, (void*)rep);
        return false;
    }

    
    const int total = subs->total_size;
    const int curr  = subs->current_size;
    if (total < 1 || total > 100 || curr < 0 || curr > total) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u sizes bad "
                        "curr=%d total=%d → skip view-angle emitter\n",
                        call_no, curr, total);
        return false;
    }

    
    const auto arena_ui = reinterpret_cast<std::uintptr_t>(subs->arena);
    if (arena_ui != 0 &&
        (arena_ui < 0x0000010000000000ULL || arena_ui >= 0x00007FFF00000000ULL)) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u arena=%p out-of-range "
                        "→ skip view-angle emitter\n", call_no, subs->arena);
        return false;
    }

    
    
    __try {
        const int alloc = rep->allocated_size;
        if (alloc < curr || alloc > total) {
            if (verbose)
                std::printf("[fva_recon][ViewSpoof][gate] apply#%u rep.allocated=%d "
                            "out of [%d,%d] → skip view-angle emitter\n",
                            call_no, alloc, curr, total);
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u SEH reading "
                        "rep->allocated_size → skip view-angle emitter\n", call_no);
        return false;
    }

    return true;
}







































bool run_fva_section_j_emitter(
    valve::pb::raw::RepeatedPtrField_t<valve::pb::raw::CSGOInputHistoryEntryPB>* subs,
    float engine_pitch, float engine_yaw, float engine_roll,
    float d_pitch,      float d_yaw,      float d_roll,
    int&  out_emitted,
    std::uint32_t& out_cached_tick_count) noexcept
{
    using namespace valve::pb::raw;
    out_emitted = 0;
    out_cached_tick_count = 0;

    if (!subs) return false;

    
    
    
    
    
    if (subs->rep) {
        for (int i = 0; i < subs->current_size; ++i) {
            auto* step = reinterpret_cast<std::uint8_t*>(subs->rep->elements[i]);
            if (!step) continue;
            out_cached_tick_count =
                *reinterpret_cast<const std::uint32_t*>(step + 0x60);
        }
    }

    
    
    
    
    const int cap = fva::version::layout::subtick_moves_capacity;   
    const int remaining = cap - subs->current_size;
    if (remaining <= 0) return true;   

    
    ProtoNewFn step_new   = resolve_cs2_input_history_new();
    ProtoNewFn qangle_new = resolve_cs2_cmsgqangle_new();
    if (!step_new || !qangle_new) return false;

    
    
    void* subs_arena = subs->arena;

    
    
    
    const auto entry_time_us = static_cast<unsigned long long>(::GetTickCount64() * 1000);
    static std::atomic<uint64_t> s_emitter_calls{0};
    const auto call_no = s_emitter_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool verbose_emit = (call_no <= 8);

    if (verbose_emit) {
        std::printf("[fva_recon][B2-EM][#%llu t=%llums] ENTRY subs=%p subs->arena=%p "
                    "subs->current_size=%d subs->total_size=%d subs->rep=%p "
                    "step_new=%p qangle_new=%p remaining=%d "
                    "engine=(%.3f,%.3f,%.3f) delta=(%.3f,%.3f,%.3f) "
                    "cached_tc=%u\n",
                    (unsigned long long)call_no, entry_time_us,
                    (void*)subs, (void*)subs_arena,
                    subs->current_size, subs->total_size, (void*)subs->rep,
                    (void*)step_new, (void*)qangle_new, remaining,
                    engine_pitch, engine_yaw, engine_roll,
                    d_pitch, d_yaw, d_roll,
                    out_cached_tick_count);
        if (subs->rep) {
            std::printf("[fva_recon][B2-EM][#%llu] Rep pre: allocated_size=%d "
                        "elements[0..3]=%p %p %p %p\n",
                        (unsigned long long)call_no, subs->rep->allocated_size,
                        (void*)subs->rep->elements[0], (void*)subs->rep->elements[1],
                        (void*)subs->rep->elements[2], (void*)subs->rep->elements[3]);
        }
        std::fflush(stdout);
    }

    for (int iter = 0; iter < remaining; ++iter)
    {
        static constexpr float k_when_base  = 0.023590f;  
        static constexpr float k_when_delta = 0.908453f;  
        const float fraction     = static_cast<float>(iter + 1) /
                                    static_cast<float>(remaining);
        const float interp_pitch = engine_pitch + fraction * d_pitch;
        const float interp_yaw   = engine_yaw   + fraction * d_yaw;
        const float interp_roll  = engine_roll  + fraction * d_roll;
        const float when         = k_when_base + fraction * k_when_delta;

        
        std::uint8_t* step   = nullptr;
        std::uint8_t* qangle = nullptr;

        __try {
            step = reinterpret_cast<std::uint8_t*>(step_new(subs_arena));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH in step_new(arena=%p)! "
                        "Aborting emit.\n",
                        (unsigned long long)call_no, iter, (void*)subs_arena);
            std::fflush(stdout);
            break;
        }
        if (!step) {
            if (verbose_emit) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] step_new returned NULL, break\n",
                            (unsigned long long)call_no, iter);
                std::fflush(stdout);
            }
            break;
        }

        
        
        
        
        
        __try {
            qangle = reinterpret_cast<std::uint8_t*>(qangle_new(nullptr));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH in qangle_new(NULL)! "
                        "step leaked at %p. Aborting emit.\n",
                        (unsigned long long)call_no, iter, (void*)step);
            std::fflush(stdout);
            break;
        }
        if (!qangle) {
            if (verbose_emit) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] qangle_new returned NULL, break\n",
                            (unsigned long long)call_no, iter);
                std::fflush(stdout);
            }
            break;
        }

        if (verbose_emit) {
            
            const auto step_arena_raw =
                *reinterpret_cast<std::uint64_t*>(step + 8);
            const auto qangle_arena_raw =
                *reinterpret_cast<std::uint64_t*>(qangle + 8);
            std::printf("[fva_recon][B2-EM][#%llu iter=%d] alloc: step=%p (arena_word=0x%llX) "
                        "qangle=%p (arena_word=0x%llX)  passed_arena=%p\n",
                        (unsigned long long)call_no, iter,
                        (void*)step, (unsigned long long)step_arena_raw,
                        (void*)qangle, (unsigned long long)qangle_arena_raw,
                        (void*)subs_arena);
            std::fflush(stdout);
        }

        
        __try {
            *reinterpret_cast<float*>(qangle + 0x18) = interp_pitch;
            *reinterpret_cast<float*>(qangle + 0x1C) = interp_yaw;
            *reinterpret_cast<float*>(qangle + 0x20) = interp_roll;
            *reinterpret_cast<std::uint32_t*>(qangle + 0x10) |= 0x7u;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH filling qangle @ %p! "
                        "Aborting emit.\n",
                        (unsigned long long)call_no, iter, (void*)qangle);
            std::fflush(stdout);
            break;
        }

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        auto untag_msg_arena = [](std::uint8_t* msg) -> std::uint64_t {
            const auto raw = *reinterpret_cast<std::uint64_t*>(msg + 8);
            if (raw & 2) return 0;
            auto untagged = raw & ~std::uint64_t{3};
            if (raw & 1) {
                __try { untagged = *reinterpret_cast<std::uint64_t*>(untagged); }
                __except (EXCEPTION_EXECUTE_HANDLER) { untagged = 0; }
            }
            return untagged;
        };

        const auto v_step_arena   = untag_msg_arena(step);
        const auto v_qangle_arena = untag_msg_arena(qangle);

        if (v_step_arena != v_qangle_arena) {
            if (verbose_emit) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] cross-arena copy: "
                            "step.arena=0x%llX qangle.arena=0x%llX → copying qangle\n",
                            (unsigned long long)call_no, iter,
                            (unsigned long long)v_step_arena,
                            (unsigned long long)v_qangle_arena);
                std::fflush(stdout);
            }
            __try {
                if (v_step_arena) {
                    
                    
                    
                    using NewInArenaFn = void*(__fastcall*)(void*, void*);
                    using MergeFromFn  = void(__fastcall*)(void*, const void*);

                    auto** src_vtbl = *reinterpret_cast<void***>(qangle);
                    auto new_fn = reinterpret_cast<NewInArenaFn>(src_vtbl[2]);  
                    void* new_qangle_raw =
                        new_fn(qangle,
                                reinterpret_cast<void*>(v_step_arena));
                    if (new_qangle_raw) {
                        auto** dst_vtbl = *reinterpret_cast<void***>(new_qangle_raw);
                        auto merge_fn = reinterpret_cast<MergeFromFn>(dst_vtbl[6]);  
                        merge_fn(new_qangle_raw, qangle);
                        qangle = reinterpret_cast<std::uint8_t*>(new_qangle_raw);
                    }
                    
                }
                
                
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH in cross-arena copy! "
                            "Aborting emit.\n",
                            (unsigned long long)call_no, iter);
                std::fflush(stdout);
                break;
            }
        }

        
        
        __try {
            *reinterpret_cast<void**>(step + 0x18)          = qangle;
            *reinterpret_cast<std::uint32_t*>(step + 0x60)  = out_cached_tick_count;
            *reinterpret_cast<float*>(step + 0x64)          = when;
            *reinterpret_cast<std::uint64_t*>(step + 0x68)  = 0;
            *reinterpret_cast<std::uint32_t*>(step + 0x10) |= 0x1E01u;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH filling step @ %p! "
                        "Aborting emit.\n",
                        (unsigned long long)call_no, iter, (void*)step);
            std::fflush(stdout);
            break;
        }

        
        
        
        
        
        
        
        
        
        
        
        
        auto* rep = subs->rep;
        const auto step_arena_raw =
            *reinterpret_cast<std::uint64_t*>(step + 8);
        std::uint64_t step_arena_effective = 0;
        if ((step_arena_raw & 2) == 0) {
            step_arena_effective = step_arena_raw & ~std::uint64_t{3};
            if (step_arena_raw & 1) {
                __try {
                    step_arena_effective =
                        *reinterpret_cast<std::uint64_t*>(step_arena_effective);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    step_arena_effective = 0;
                }
            }
        }
        const auto subs_arena_val =
            reinterpret_cast<std::uint64_t>(subs->arena);
        const bool arena_match = (subs_arena_val == step_arena_effective);
        const bool fast_ok = rep && arena_match &&
                             (rep->allocated_size < subs->total_size);
        
        const auto step_arena_untagged = step_arena_effective;

        if (verbose_emit) {
            std::printf("[fva_recon][B2-EM][#%llu iter=%d] AddAllocated gate: "
                        "rep=%p subs.arena=0x%llX step.arena.untagged=0x%llX arena_match=%d "
                        "rep.allocated=%d subs.total=%d fast_ok=%d\n",
                        (unsigned long long)call_no, iter, (void*)rep,
                        (unsigned long long)subs_arena_val,
                        (unsigned long long)step_arena_untagged,
                        (int)arena_match,
                        rep ? rep->allocated_size : -1,
                        subs->total_size, (int)fast_ok);
            std::fflush(stdout);
        }

        if (fast_ok) {
            __try {
                if (subs->current_size < rep->allocated_size) {
                    rep->elements[rep->allocated_size] =
                        rep->elements[subs->current_size];
                }
                rep->elements[subs->current_size] =
                    reinterpret_cast<CSGOInputHistoryEntryPB*>(step);
                subs->current_size    += 1;
                rep->allocated_size   += 1;
                out_emitted           += 1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH in Rep insert! "
                            "Aborting emit.\n",
                            (unsigned long long)call_no, iter);
                std::fflush(stdout);
                break;
            }
            if (verbose_emit) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] INSERT ok: "
                            "step=%p at rep.elements[%d]  new current_size=%d "
                            "new allocated_size=%d\n",
                            (unsigned long long)call_no, iter,
                            (void*)step, subs->current_size - 1,
                            subs->current_size, rep->allocated_size);
                std::fflush(stdout);
            }
        } else {
            
            
            
            
            
            
            
            
            
            
            
            
            
            static std::atomic<int> s_slowpath_hits{0};
            const int n = s_slowpath_hits.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 8 || (n & 0xFFF) == 0) {
                const char* reason =
                    (!rep) ? "REP-NULL" :
                    (!arena_match) ? "ARENA-MISMATCH" :
                    (rep->allocated_size >= subs->total_size) ? "REP-FULL" :
                    "UNKNOWN";
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] SLOW-PATH #%d "
                            "reason=%s: rep=%p current=%d total=%d "
                            "subs.arena=0x%llX step.arena=0x%llX allocated=%d "
                            "→ invoking add_allocated_slow_path\n",
                            (unsigned long long)call_no, iter, n, reason,
                            (void*)rep, subs->current_size, subs->total_size,
                            (unsigned long long)subs_arena_val,
                            (unsigned long long)step_arena_untagged,
                            rep ? rep->allocated_size : -1);
                std::fflush(stdout);
            }

            
            
            
            if (!arena_match) { break; }
            bool inserted = false;
            __try {
                inserted = add_allocated_slow_path(
                    subs, reinterpret_cast<CSGOInputHistoryEntryPB*>(step));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                inserted = false;
            }
            if (!inserted) { break; }
            out_emitted += 1;
        }
    }

    if (verbose_emit) {
        const auto exit_time_us = static_cast<unsigned long long>(::GetTickCount64() * 1000);
        std::printf("[fva_recon][B2-EM][#%llu t=%llums] EXIT emitted=%d "
                    "final current_size=%d cached_tc=%u duration_us=%llu\n",
                    (unsigned long long)call_no, exit_time_us,
                    out_emitted, subs->current_size, out_cached_tick_count,
                    exit_time_us - entry_time_us);
        std::fflush(stdout);
    }

    return true;
}

void apply(valve::pb::raw::CUserCmd* raw) noexcept
{
    using namespace valve::pb::raw;

    
    static std::atomic<std::uint32_t> s_calls{0};
    const std::uint32_t call_no = s_calls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (!g_ready.load(std::memory_order_acquire))         return;
    
    
    
    
    
    
    
    
    
    
    if (!raw)                                             return;

    CBaseUserCmdPB* pb = raw->base;
    if (!pb) return;

    
    
    auto* subs_early = reinterpret_cast<RepeatedPtrField_t<CSubtickMoveStep>*>(
        reinterpret_cast<std::uint8_t*>(raw) +
        fva::version::layout::user_cmd_subtick_moves_field);
    try_cache_vtables(pb, subs_early);

    
    const bool verbose = call_no <= 16 || (call_no & 0x7Fu) == 0;
    float pre_x = 0.0f, pre_y = 0.0f, pre_z = 0.0f;
    std::uint32_t pre_subs_size = 0;
    if (pb->viewangles) {
        pre_x = pb->viewangles->x;
        pre_y = pb->viewangles->y;
        pre_z = pb->viewangles->z;
    }
    {
        auto* subs = reinterpret_cast<RepeatedPtrField_t<CSubtickMoveStep>*>(
            reinterpret_cast<std::uint8_t*>(raw) +
            fva::version::layout::user_cmd_subtick_moves_field);
        pre_subs_size = static_cast<std::uint32_t>(subs->current_size);
    }
    const bool armed_at_entry = g_target_armed.load(std::memory_order_acquire);
    const float tgt_pitch = float_of(g_target_bits[0].load(std::memory_order_relaxed));
    const float tgt_yaw   = float_of(g_target_bits[1].load(std::memory_order_relaxed));
    if (verbose) {
        std::printf("[fva_recon][ViewSpoof] apply#%u ENTRY  pre_va=(%.3f,%.3f,%.3f)  "
                    "target=(%.3f,%.3f) %s  pre_subs=%u\n",
                    call_no, pre_x, pre_y, pre_z, tgt_pitch, tgt_yaw,
                    armed_at_entry ? "ARMED" : "SELF-ECHO",
                    pre_subs_size);
    }

    
    
    
    
    
    
    pb->has_bits |= CBASEUSERCMDPB_BITS_VIEWANGLES;
    CMsgQAngle* qang = pb->viewangles;
    if (!qang) {
        
        
        
        
        
        ProtoNewFn qfn = resolve_cs2_cmsgqangle_new();
        if (!qfn) return;
        qang = reinterpret_cast<CMsgQAngle*>(qfn(nullptr));
        if (!qang) return;
        pb->viewangles = qang;
    }

    
    
    
    const float prior_x = qang->x;
    const float prior_y = qang->y;
    const float prior_z = qang->z;

    
    
    
    
    
    
    
    
    
    
    
    
    static std::atomic<std::uint32_t> s_reference_bits[3]{};  
    float tgt_x, tgt_y, tgt_z;
    if (g_target_armed.load(std::memory_order_acquire)) {
        tgt_x = float_of(g_target_bits[0].load(std::memory_order_relaxed));
        tgt_y = float_of(g_target_bits[1].load(std::memory_order_relaxed));
        tgt_z = float_of(g_target_bits[2].load(std::memory_order_relaxed));
    } else {
        
        tgt_x = float_of(s_reference_bits[0].load(std::memory_order_relaxed));
        tgt_y = float_of(s_reference_bits[1].load(std::memory_order_relaxed));
        tgt_z = float_of(s_reference_bits[2].load(std::memory_order_relaxed));
    }

    
    
    
    
    
    
    
    
    
    
    const float d_pitch = wrap180(prior_x - tgt_x);
    const float d_yaw   = wrap180(prior_y - tgt_y);
    const float d_roll  = wrap180(prior_z - tgt_z);

    
    
    
    
    

    
    
    
    
    
    auto* subs = reinterpret_cast<RepeatedPtrField_t<CSubtickMoveStep>*>(
        reinterpret_cast<std::uint8_t*>(raw) +
        fva::version::layout::user_cmd_subtick_moves_field);

    
    
    
    
    
    
    
    
    
    
    
    auto* input_history_rep =
        reinterpret_cast<RepeatedPtrField_t<CSGOInputHistoryEntryPB>*>(subs);

    
    
    
    
    
    
    
    
    
    
    
    
    
    const bool engine_stable = fva::hooks::engine2::is_stable_gameplay();

    if (!engine_stable && verbose) {
        std::printf("[fva_recon][ViewSpoof][gate] apply#%u engine2!stable=0 "
                    "(IsInGame=%d IsConnected=%d shutting=%d) → SELF-ECHO\n",
                    call_no,
                    (int)fva::hooks::engine2::is_in_game(),
                    (int)fva::hooks::engine2::is_connected(),
                    !fva::hooks::engine2::is_stable_gameplay()
                        && fva::hooks::engine2::is_in_game()
                        && fva::hooks::engine2::is_connected() ? 1 : 0);
    }

    
    
    
    const bool subs_safe = subs_looks_safe(input_history_rep, call_no, verbose);

    int           emitted            = 0;
    std::uint32_t cached_tick_count  = 0;
    bool          emitter_ok         = false;
    if (engine_stable && subs_safe) {
        __try {
            emitter_ok = run_fva_section_j_emitter(
                input_history_rep,
                prior_x, prior_y, prior_z,
                d_pitch, d_yaw, d_roll,
                emitted, cached_tick_count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::printf("[fva_recon][ViewSpoof] apply#%u SEH in view-angle emitter emitter "
                        "root — subs=%p rep=%p total=%d current=%d\n",
                        call_no, (void*)input_history_rep,
                        (void*)input_history_rep->rep,
                        input_history_rep->total_size,
                        input_history_rep->current_size);
            std::fflush(stdout);
            emitter_ok = false;
        }
    }

    
    
    
    if (emitter_ok && emitted > 0) {
        static std::atomic<int> s_dumps{0};
        if (s_dumps.fetch_add(1, std::memory_order_relaxed) < 4 &&
            input_history_rep->rep &&
            input_history_rep->current_size > 0)
        {
            const int last_idx = input_history_rep->current_size - 1;
            auto* last = reinterpret_cast<std::uint8_t*>(
                             input_history_rep->rep->elements[last_idx]);
            if (last) {
                auto* qang_ptr =
                    *reinterpret_cast<std::uint8_t**>(last + 0x18);
                std::printf("[fva_recon][ViewSpoof] EMIT last_idx=%d step@%p "
                            "has_bits=0x%X qangle@%p qangle=(%.3f,%.3f,%.3f) "
                            "qangle_has_bits=0x%X r_tc=%u r_tf=%.3f "
                            "cached_tc=%u emitted=%d\n",
                            last_idx, (void*)last,
                            *reinterpret_cast<std::uint32_t*>(last + 0x10),
                            (void*)qang_ptr,
                            qang_ptr ? *reinterpret_cast<float*>(qang_ptr + 0x18) : 0.f,
                            qang_ptr ? *reinterpret_cast<float*>(qang_ptr + 0x1C) : 0.f,
                            qang_ptr ? *reinterpret_cast<float*>(qang_ptr + 0x20) : 0.f,
                            qang_ptr ? *reinterpret_cast<std::uint32_t*>(qang_ptr + 0x10) : 0u,
                            *reinterpret_cast<std::uint32_t*>(last + 0x60),
                            *reinterpret_cast<float*>(last + 0x64),
                            cached_tick_count, emitted);
            }
        }
    }
    (void)armed_at_entry;
    const bool appended = emitted > 0;

    
    
    
    
    
    
    
    
    
    
    
    
    s_reference_bits[0].store(bits_of(prior_x), std::memory_order_relaxed);
    s_reference_bits[1].store(bits_of(prior_y), std::memory_order_relaxed);
    s_reference_bits[2].store(bits_of(prior_z), std::memory_order_relaxed);

    
    
    
    
    
    
    
    if (engine_stable && appended) {
        auto crs = fva::hooks::engine2::compute_random_seed();
        if (crs) {
            __try {
                const std::uint32_t seed = crs(nullptr, nullptr,
                                                cached_tick_count);
                pb->random_seed = static_cast<int32_t>(seed);
                pb->has_bits   |= CBASEUSERCMDPB_BITS_RANDOM_SEED;
                if (verbose) {
                    std::printf("[fva_recon][ViewSpoof] apply#%u random_seed=0x%08X "
                                "via ComputeRandomSeed(tick=%u)\n",
                                call_no, seed, cached_tick_count);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                
                
            }
        }
    }
    (void)cached_tick_count;  

    if (verbose) {
        std::uint32_t post_subs_size = 0;
        auto* subs_after = reinterpret_cast<RepeatedPtrField_t<CSubtickMoveStep>*>(
            reinterpret_cast<std::uint8_t*>(raw) +
            fva::version::layout::user_cmd_subtick_moves_field);
        post_subs_size = static_cast<std::uint32_t>(subs_after->current_size);
        std::printf("[fva_recon][ViewSpoof] apply#%u EXIT   post_va=(%.3f,%.3f,%.3f)  "
                    "delta=(%.3f,%.3f)  post_subs=%u (+%d) appended=%d\n",
                    call_no, qang->x, qang->y, qang->z,
                    d_pitch, d_yaw,
                    post_subs_size, (int)post_subs_size - (int)pre_subs_size,
                    (int)appended);
    }
}

} 
