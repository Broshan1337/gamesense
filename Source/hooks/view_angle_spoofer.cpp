// ============================================================================
// Hook A Phase B2 — implementation.
//
// FVA-side flow (sub_7FFBE80C9D8C + 0x0944..+0x0CC5) is reproduced with the
// following simplifications, none of which change the wire-observable effect:
//
//   * Lazy-alloc of raw_cmd->base is delegated to Phase E's null-base guard
//     upstream of us; if it still comes back null we bail (base isn't
//     populated on menu ticks anyway).
//   * Lazy-alloc of pb->viewangles is delegated to CS2 — if CS2's original
//     CreateMove didn't populate viewangles this tick we bail rather than
//     carry a private CMsgQAngle allocator.  Every gameplay tick populates
//     it, so this cost only the first pre-connect tick.
//   * The internal `find last subtick w/ pressed_amount==0` scan + Serialize
//     into a throwaway `std::string` is elided — it fed a base64 audit that
//     lives in Phase D, not the wire.
//   * Subtick emission reuses pre-allocated Rep slots (standard protobuf
//     `RepeatedPtrField::Add()` semantics) rather than calling into CS2's
//     `CSubtickMoveStep::New(arena)`.  Growth-when-full is a follow-up: once
//     the arena-aware allocator is sig-scanned it slots into `append_step()`
//     with no other changes.
//
// Wrap math matches FVA exactly: `wrap180(dx) = std::remainderf(dx, 360)`
// clamped into (-180, 180].  FVA uses `remainderf`, NOT `fmodf`.
// ============================================================================
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

// From src/core/logf.cpp — cached sig-scan resolvers for CS2's own protobuf
// arena allocator and the CSubtickMoveStep vtable pointer.  Both are
// resolved once against the currently-loaded client.dll.
namespace fva::log
{
    void* find_arena_allocator();
    void* find_csubtick_vtable();
}

namespace fva::view_angle_spoofer
{

namespace {

std::atomic<bool>  g_ready{false};

// 3 x atomic<uint32_t> (bit-cast float bits) + one arm/disarm flag.
// atomic<float> is C++20 but not guaranteed lock-free on every ABI; the bit
// pattern approach is unambiguously lock-free on x64.
std::atomic<std::uint32_t> g_target_bits[3]{};
std::atomic<bool>          g_target_armed{false};

// Grace period removed 2026-07-11 — engine2!IsInGame + IsConnected +
// LevelShutdown gate is authoritative.  See fva::hooks::engine2::is_stable_gameplay().

// Cached pointer to CS2's protobuf arena allocator (sub_7FFBE81029A0
// analog in client.dll).  Lazy-initialised by rep_grow() / alloc_bytes()
// via fva::log::find_arena_allocator().  Declared here so rep_grow()
// (defined below) can see the symbol; the later re-declaration is a
// no-op merge.
using ArenaAllocFn = void*(__fastcall*)(void*, std::size_t, const void*);
std::atomic<ArenaAllocFn> g_arena_alloc{nullptr};

inline std::uint32_t bits_of(float f) noexcept {
    std::uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u;
}
inline float float_of(std::uint32_t u) noexcept {
    float f; std::memcpy(&f, &u, sizeof(f)); return f;
}

// wrap180: fold `dx` into (-180, 180].  FVA uses `remainderf(dx, 360)` — do
// the same to preserve sign parity on values in [180, 360).
float wrap180(float dx) noexcept
{
    float m = std::remainderf(dx, 360.0f);
    if      (m >  180.0f) m -= 360.0f;
    else if (m < -180.0f) m += 360.0f;
    return m;
}

// =============================================================================
// CS2 IMemAlloc bridge — 1:1 port of FVA's sub_7FFBE80DC5A8 / sub_7FFBE80DC73C.
//
// FVA resolves the allocator via VMP-encrypted hash lookup and stores the
// interface pointer in `qword_7FFBE823AA10`.  It then calls:
//   * Alloc(size)  = vtable[+0x08]   (slot 1)   ← sub_7FFBE80DC5A8
//   * Free(ptr)    = vtable[+0x18]   (slot 3)   ← sub_7FFBE80DC73C
//
// tier0.dll (which is the source of IMemAlloc) exports `g_pMemAlloc` — we
// just GetProcAddress and dereference.  Zero-cost after first call.
// =============================================================================
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

// =============================================================================
// 1:1 port of FVA sub_7FFBE8102020 → sub_7FFBE8102330 — RepeatedPtrField Reserve.
//
// Full port — handles BOTH heap and arena paths.  Called by sub_7FFBE80CB348
// (Rep AddAllocated slow-path) when rep is NULL OR current_size == total_size.
//
// FVA disasm branch selector:
//   if (v8)  v10 = sub_7FFBE8103080(*a1, size, &unk_7FFBE8235A78);  // arena
//   else    v10 = sub_7FFBE80DC5A8(size);                            // heap
//
// Heap path: IMemAlloc (tier0!g_pMemAlloc)->Alloc(size) — cs2_alloc.
//
// Arena path: sub_7FFBE8103080(arena, size, rtti) — FVA's TLS-cached arena
// bump allocator.  We can NOT call FVA's variant directly (it uses FVA's own
// TlsIndex which is uninitialised in a legit host); instead we sig-scan
// CS2's equivalent function inside client.dll and call it — CS2's arena
// allocator uses CS2's TLS which is correctly primed by every CS2-owned
// arena.  Semantically identical: both bump the same arena's bump-pointer.
//
// Growth: v9 = max(2*old_total + 1, needed);  new_cap = v9
// Layout: new_rep = alloc(8 + new_cap * 8)
//   [0..3]  int32 allocated_size
//   [8..]   void* elements[new_cap]
//
// Returns address of first slot at position `current_size` in the new rep.
// Returns nullptr on allocation failure.
// =============================================================================
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

    // Growth formula — verbatim from FVA sub_7FFBE8102020 disasm:
    //   if (needed >= 1) {
    //     if (v4 <= 1073741819) { v9 = 2*v4+1; if (v9<v6) v9=v3+a2; }
    //     else v9 = 0x7FFFFFFF;
    //   } else v9 = 1;
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

    // Arena branch — sub_7FFBE8103080 equivalent.  Try CS2's arena allocator
    // (found via sig-scan of client.dll's sub_7FFBE81029A0-analog).
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
                // FVA passes &unk_7FFBE8235A78 (RepeatedPtrField::Rep RTTI
                // descriptor) as 3rd arg.  CS2 accepts null-RTTI in the
                // TLS-cached fast path; arena_alloc_aligned dispatches on it
                // only when the thread-cache-bucket lookup misses.
                new_raw = afn(arena, new_bytes, nullptr);
                if (new_raw) arena_owned = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                new_raw = nullptr;
            }
        }
    }

    // Heap branch — fallback if no arena OR arena alloc failed.
    if (!new_raw) {
        new_raw = cs2_alloc(new_bytes);
    }
    if (!new_raw) return nullptr;

    // Zero-init to be safe (elements[current..new_cap) are unused slots).
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
                // If reading the old rep faults, we still installed new_rep
                // with allocated_size=0.  Continue — the fresh rep is empty
                // but usable.
                new_rep->allocated_size = 0;
            }
        }

        // DO NOT free old_rep.
        //
        // FVA sub_7FFBE8102020 does free it (`sub_7FFBE819EEC0(v5, v13)`
        // in the heap branch, TLS-cached-free-list in the arena branch)
        // — but in our environment freeing triggers a delayed use-after-
        // free crash: CS2 caches pointers to old_rep in the pb's internal
        // metadata (protobuf runtime bookkeeping we can't audit).  When
        // CS2 later destructs the pb, it iterates via the CACHED old_rep
        // pointer that we just freed → AV in networksystem/protobuf-side
        // destruction path.  FVA doesn't hit this because its protobuf
        // runtime is COMPILED WITH the same TLS/metadata layout that its
        // free is aware of;  ours is a hosted plugin that can't safely
        // touch CS2's private bookkeeping.
        //
        // Cost of the leak: 8 + old_total*8 bytes per grow.  Rep grows
        // from 7 → 15 → 31 → 63 → ...  ~5-6 grows per view-angle emitter apply
        // in worst case.  Total per-session leak stays under 1 MB even
        // in long sessions.  Acceptable for stability.
        (void)old_rep;
    } else {
        new_rep->allocated_size = 0;
    }

    subs->rep        = new_rep;
    subs->total_size = new_cap;

    return reinterpret_cast<void**>(&new_rep->elements[current]);
}

// Legacy alias — retained so callers don't need renaming; heap-only was a
// misnomer since 2026-07-11 (arena path now inline).  Redirects to rep_grow().
template<typename T>
void** rep_grow_heap(valve::pb::raw::RepeatedPtrField_t<T>* subs) noexcept
{
    return rep_grow(subs);
}

// =============================================================================
// 1:1 port of FVA sub_7FFBE80CB348 — RepeatedPtrField AddAllocated slow-path.
//
// FVA decomp signature:
//   sub_7FFBE80CB348(subs, step, source_arena, target_arena)
// where target_arena == subs->arena (untagged) and source_arena is the arena
// the step was allocated in.
//
// Since our view-angle emitter emitter enters this function ONLY when arenas already
// match (checked pre-call), the cross-arena copy branch degenerates to a
// no-op and we go straight to the AddAllocated logic:
//
//   1. If rep==NULL OR rep->allocated_size == total_size → grow via
//      rep_grow_heap(subs).  (FVA calls sub_7FFBE8102330(subs, total+1)
//      which internally calls sub_7FFBE8102020 with the needed delta.)
//   2. Else if allocated < total → swap-append semantics.
//   3. Finally: rep->elements[current_size++] = step.
//
// Returns true on success, false on allocation failure.
// =============================================================================
template<typename T>
bool add_allocated_slow_path(valve::pb::raw::RepeatedPtrField_t<T>* subs,
                              T* step) noexcept
{
    using RepT = typename valve::pb::raw::RepeatedPtrField_t<T>::Rep_t;

    if (!subs || !step) return false;

    RepT* rep = subs->rep;
    const int current = subs->current_size;
    const int total   = subs->total_size;

    // --------------------------------------------------------------------
    // 1:1 port of FVA sub_7FFBE80CB348 — CORRECTED 2026-07-11 for exact
    // branch parity.  Previous version conflated Branch C (rep-full slot
    // recycle) with Branch A (grow), triggering 5+ grows per view-angle emitter
    // emit that live-crashed cs2.  FVA disasm has 3 distinct branches:
    //
    //   A. `!v8 || current == total`  → grow via sub_7FFBE8102330
    //   B. `allocated != total`       → swap-append (fills alloc slack)
    //   C. `allocated == total && current != total && arena == 0`
    //                                 → destroy elements[current] & overwrite
    //
    // Branch C KEY behavior: FVA does NOT grow when the rep is completely
    // populated but current has not reached total.  It DESTROYS the cs2-
    // owned element at the current slot (via CS2 destructor +
    // sub_7FFBE819EEC0 = IMemAlloc::Free) and OVERWRITES the slot with
    // our new step.  This lets FVA emit up to (total_size) subticks
    // WITHOUT ever calling sub_7FFBE8102020 grow — the risky path.
    //
    // Our port omits the destroy() call (freeing a cs2-owned protobuf
    // message via arbitrary allocator == UB; FVA is compiled with its
    // own protobuf runtime so it knows the destructor's ABI).  Skipping
    // the destroy leaks the cs2-owned step for one cmd cycle — cs2
    // normally recycles these via its arena, so the leak is minimal.
    // --------------------------------------------------------------------

    __try {
        // Case A: rep NULL OR current == total → GROW.  Only path that
        // actually grows the rep.  For a typical view-angle emitter emit starting
        // from (current=1, total=7, allocated=4), Case A fires exactly
        // ONCE per emit (at iter 6 when current reaches 7).
        if (!rep || current == total) {
            void** slot = rep_grow(subs);
            if (!slot) return false;
            rep = subs->rep;
            if (!rep) return false;
            // FVA LABEL_20 → LABEL_21: allocated++, then insert at current++.
            rep->allocated_size = rep->allocated_size + 1;
            rep->elements[current] = step;
            subs->current_size    = current + 1;
            return true;
        }

        // Load allocated_size (guarded — stale rep might fault).
        const int alloc = rep->allocated_size;

        // Case B: allocated < total → swap-append (no grow).
        //   FVA: `if (current >= alloc) allocated++;`
        //        `else { elements[alloc] = elements[current];  allocated++ }`
        // Then insert at current++.
        if (alloc != total) {
            if (current >= alloc) {
                rep->allocated_size = alloc + 1;
            } else {
                // Move existing occupant (cs2-owned slot in [current..alloc)
                // range) to the end so it survives beyond visible range.
                rep->elements[alloc] = rep->elements[current];
                rep->allocated_size  = alloc + 1;
            }
            rep->elements[current] = step;
            subs->current_size    = current + 1;
            return true;
        }

        // Case C: allocated == total AND current < total AND arena == 0
        //   → destroy+overwrite (rep-full slot recycle, NO GROW).
        // FVA:
        //     v13 = elements[current]
        //     if (v13) { destructor(v13);  IMemAlloc::Free(v13, 120); }
        //     elements[current++] = step
        //
        // We SKIP the destroy — cs2's protobuf message destructor is
        // build-specific and calling it here risks corruption.  The
        // cs2-owned step at elements[current] gets orphaned; cs2's arena
        // (or heap ref counting) reclaims it later.
        rep->elements[current] = step;
        subs->current_size    = current + 1;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// -----------------------------------------------------------------------------
// FULL FVA view-angle emitter port — reproduces sub_7FFBE82E8C32 (view-angle emitter emitter)
// verified via IDA decomp of C:\vmp\FVA.dll.i64 session ea21efe4.
//
// Vtable extraction strategy: LIVE from CS2 messages.
//   * CS2's real CSubtickMoveStep vtable ← copied from
//     subs->rep->elements[0] (any populated subtick entry).
//   * CS2's real CMsgQAngle vtable ← copied from pb->viewangles.
// This is fundamentally safer than sig-scanning `48 8D 05 ? ? ? ? 48 89 01`
// (which matched hundreds of protobuf constructors and produced the wrong
// vtable — the root cause of the ~15s post-append crashes we observed).
// -----------------------------------------------------------------------------

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

// Arena allocator cache — declared near the top of the anon namespace so
// rep_grow() can see it.  Nothing more to do here.

// -----------------------------------------------------------------------------
// CS2 CSubtickMoveStep::New and CMsgQAngle::New — DIRECT sig-scan.
//
// Verified in C:\vmp\...\client.dll via IDA MCP session f73dcd28:
//   CSubtickMoveStep::New @ RVA 0x4B22D0 — `sub_1804B22D0`
//   CMsgQAngle::New       @ RVA 0x6840F0 — `sub_1806840F0`
//
// Both share the same MSVC template `T::New(Arena*)` shape but pin the
// size immediate (B9 imm32 = mov ecx, size) to disambiguate.  We use
// the EXACT pattern with fixed size marker so we can't accidentally
// match the wrong T::New.
//
// Both routes converge on `sub_180B931D0` (CS2's heap alloc via
// tier0's MemAlloc_AllocFunc) when arena is null.  The destructor
// (sub_1804B0070 / equivalents) calls `sub_180B93230` (MemAlloc_FreeFunc)
// — SAME allocator.  So callers of CS2's New are automatically
// delete-compat when CS2's protobuf Rep destructor runs.
// -----------------------------------------------------------------------------
using ProtoNewFn = void*(__fastcall*)(void* /*arena*/);
std::atomic<ProtoNewFn> g_cs2_csubtick_new{nullptr};
std::atomic<ProtoNewFn> g_cs2_input_history_new{nullptr};
std::atomic<ProtoNewFn> g_cs2_cmsgqangle_new{nullptr};

// FUNDAMENTAL RE (2026-07-10 16:47): our "subtick_moves" observations
// were actually CSGOUserCmdPB.input_history — the entries there are
// CSGOInputHistoryEntryPB (120 B, vtable RVA 0x1A1E028 — matches our
// live-extracted vtable EXACTLY).
// Sigs below with size markers:
//   0x38 = CSubtickMoveStep (kept for completeness — NOT what we alloc)
//   0x78 = CSGOInputHistoryEntryPB (what we actually need)
//   0x28 = CMsgQAngle (for view_angles sub-message)
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

    // Manifest RVA + fingerprint gate.  Sig-fallback would pick the FIRST
    // T::New with size 0x38 — several exist and picking the wrong one
    // corrupts the Rep.
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

    // KNOWN RVA from manifest — updated per-depot in version_manifest.h.
    // Sig-scan for `B9 78 00 00 00 E8` (mov ecx, 0x78; call heap_alloc)
    // is NOT unique — matches several 120-byte T::New candidates.  Only
    // the manifest RVA + fingerprint gate is authoritative.
    //
    // CRITICAL: if fingerprint fails, RETURN NULL — using a wrong T::New
    // corrupts cs2's protobuf Rep (WRONG vtable + size mismatch) and
    // pegs the game at 1 FPS.  A live cs2 with wrong allocator becomes
    // unrecoverable.
    constexpr std::uintptr_t k_known_rva =
        fva::version::known_rva::new_maybe_arena_csgoinputhistoryentrypb;
    auto* try_at = reinterpret_cast<std::uint8_t*>(client) + k_known_rva;

    // Fingerprint: first 8 bytes of a valid T::New wrapper.
    const std::uint8_t expected[8] = {0x48, 0x89, 0x5C, 0x24, 0x10,
                                      0x57, 0x48, 0x83};
    if (std::memcmp(try_at, expected, 8) == 0) {
        // Additional discriminant: at +0x14 must be `mov ecx, 0x78` for
        // the 120-byte CSGOInputHistoryEntryPB size.  Blocks accidental
        // matches against other T::New wrappers with same prologue.
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

    // Fingerprint failed — REFUSE.  Do not fall back to a generic sig-scan
    // that may pick a different-size T::New and corrupt CS2's Rep.  This
    // disables view-angle emitter append (safer than a crash-loop).
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

    // KNOWN RVA from manifest — see view_angle_spoofer comment on
    // resolve_cs2_input_history_new for why sig-fallback is unsafe.
    constexpr std::uintptr_t k_known_rva =
        fva::version::known_rva::new_maybe_arena_cmsgqangle;
    auto* try_at = reinterpret_cast<std::uint8_t*>(client) + k_known_rva;
    const std::uint8_t expected[8] = {0x48, 0x89, 0x5C, 0x24, 0x10,
                                      0x57, 0x48, 0x83};
    if (std::memcmp(try_at, expected, 8) == 0) {
        // Size discriminant: +0x14 = `mov ecx, 0x28` (40 bytes).
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

    // REFUSE sig fallback — same corruption risk as input_history variant.
    std::printf("[fva_recon][ViewSpoof] CMsgQAngle::New: RVA 0x%zX fingerprint MISS "
                "— REFUSING sig fallback.  Update manifest "
                "known_rva::new_maybe_arena_cmsgqangle for this depot.\n",
                k_known_rva);
    return nullptr;
}

// Legacy sig-scan path (unreachable dead code kept for future reference on
// how CMsgQAngle::New was originally located when no manifest RVA existed):
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

// -----------------------------------------------------------------------------
// PATH 1: delete-compatible allocator via ucrtbase.dll's malloc.
//
// CS2's client.dll dynamically links to ucrtbase.dll — its `operator delete`
// (invoked when a RepeatedPtrField destructor deletes owned elements) calls
// through to ucrtbase's `free`.  If we allocate via the SAME ucrtbase's
// `malloc`, CS2's `delete/free` on our block is compatible.
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// PATH 2 + 3 combined: chain-walk to find a NON-NULL arena.
//
// Try in priority order:
//   subs->arena       — RepeatedPtrField's own arena
//   raw->arena_word   — CUserCmd's arena (with tag-bit unwrap: bit0 = pointer-to-pointer)
//   pb->viewangles->arena — CMsgQAngle's arena (should match parent CBaseUserCmdPB)
//   existing subs->rep->elements[i]->arena — any CS2-populated subtick step's arena
//   pb->buttons_pb->arena — CInButtonStatePB's arena
//
// FVA's arena_word handling from Section-E disasm:
//     if (raw->arena_word & 1)
//         arena = *(void**)(raw->arena_word & ~3);
//     else
//         arena = (void*)(raw->arena_word & ~3);
// -----------------------------------------------------------------------------
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
        // pb->viewangles is a PBMessage — its metadata field at +8 is the arena
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

// -----------------------------------------------------------------------------
// Master allocator — tries all 3 paths.
// -----------------------------------------------------------------------------
void* alloc_bytes(void* preferred_arena, std::size_t size) noexcept
{
    // Path A: arena_alloc when we have a real arena
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

    // Path 1: ucrtbase malloc — delete-compat
    MallocFn mf = resolve_ucrt_malloc();
    if (mf) {
        mem = mf(size);
        if (mem) {
            std::memset(mem, 0, size);
            return mem;
        }
    }

    // Last resort: HeapAlloc (WILL likely crash on cs2-side delete but we've
    // tried everything else).
    return ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

// Allocate a fresh CSGOInputHistoryEntryPB — the REAL type CS2 puts
// into raw+0x28 Rep (verified: live vtable @ RVA 0x1A1E028 matches
// CSGOInputHistoryEntryPB, not CSubtickMoveStep).  DELEGATES to
// sub_180742730 (CSGOInputHistoryEntryPB::New(Arena*)) — 120 bytes,
// delete-compat via tier0 MemAlloc_FreeFunc.
//
// Named `alloc_and_init_step` for legacy API compatibility with the
// existing view_angle_spoofer header; return type still CSubtickMoveStep* but
// the actual object is CSGOInputHistoryEntryPB.  Callers write only
// at raw offsets, never through the CSubtickMoveStep struct fields
// (which would be wrong for a 120-byte layout).
valve::pb::raw::CSubtickMoveStep* alloc_and_init_step(void* arena) noexcept
{
    ProtoNewFn fn = resolve_cs2_input_history_new();
    if (!fn) return nullptr;
    return reinterpret_cast<valve::pb::raw::CSubtickMoveStep*>(fn(arena));
}

// Allocate a fresh CMsgQAngle — same delegation to CS2's own
// sub_1806840F0 (CMsgQAngle::New(Arena*)), size=40.
valve::pb::raw::CMsgQAngle* alloc_and_init_qangle(void* arena) noexcept
{
    ProtoNewFn fn = resolve_cs2_cmsgqangle_new();
    if (!fn) return nullptr;
    return reinterpret_cast<valve::pb::raw::CMsgQAngle*>(fn(arena));
}

// Append one CSubtickMoveStep to `subs`.  When possible uses a fresh
// ============================================================================
// FALLBACK — reconstruction pre-FVA-full-port implementation.
// ============================================================================
// This `append_step` function was our ORIGINAL view-angle emitter strategy: allocate a
// fresh CSGOInputHistoryEntryPB via CS2's T::New, stamp it, and publish into
// the Rep by incrementing current_size + allocated_size.
//
// **PROBLEM**: growing allocated_size makes CS2's Rep destructor include our
// injected entries in the iteration.  Even though our block is delete-compat
// via tier0's MemAlloc chain, accumulated state across a session caused
// crashes on map exit (destructor iterating our entries after CS2 had already
// cleaned up its arena).
//
// **REPLACED BY**: promote-in-place at `apply()` — matches FVA disasm at
// 0x7FFBE80CA7CD..0x7FFBE80CA858 which iterates existing subticks backward,
// finds first candidate (when != 0.0f), and modifies its `view_angles` fields
// in-place via `sub_7FFBE8103A70(element, template)`.
//
// Rep count stays untouched → CS2 owns every pointer → destructor works
// correctly → no map-exit crash.
//
// This function is KEPT COMPILED (dead code, unreachable from apply) so we
// can regression-test the old behavior if the promote-in-place approach
// misses spoof timing that append caught.  Do not delete without a green
// spoof-audit against a live wire capture.
// ============================================================================
bool append_step_fallback_v1(valve::pb::raw::RepeatedPtrField_t<valve::pb::raw::CSubtickMoveStep>* subs,
                              float pitch_delta, float yaw_delta) noexcept
{
    using namespace valve::pb::raw;

    // GAP-DOC(reconstruction):
    // FVA's own arena allocator lives at RVA 0x60BFC (sub_7FFBE80FABFC in
    // the fva_rebuild.i64 session):
    //
    //     CSubtickMoveStep* alloc(Arena* arena) {
    //         void* mem = arena
    //                     ? sub_7FFBE8103070(arena, 56, RTTI_CSubtickMoveStep)
    //                     : sub_7FFBE80DC5A8(56);            // plain malloc
    //         *(void**)(mem + 0)   = &unk_7FFBE8218F00;      // FVA-side vtable
    //         *(void**)(mem + 8)   = arena;
    //         *(u64*)  (mem + 16)  = 0;                       // has_bits/cached
    //         *(u64*)  (mem + 24)  = 0;                       // button
    //         *(u8*)   (mem + 32)  = 0;                       // pressed
    //         *(u64*)  (mem + 36)  = 0;                       // when + analog_fwd
    //         *(u64*)  (mem + 44)  = 0;                       // analog_left+pitch
    //         *(u32*)  (mem + 52)  = 0;                       // yaw_delta
    //         return mem;
    //     }
    //
    // FVA installs its OWN CSubtickMoveStep vtable at unk_7FFBE8218F00 — we
    // can't reuse that pointer inside cs2.  To port cleanly we need CS2's
    // own CSubtickMoveStep vtable pointer, which lives in client.dll and
    // is best sourced from CS2's protobuf `default_instance()` accessor.
    // The proper wiring is:
    //   * sig-scan client.dll for `CSubtickMoveStep::default_instance()`
    //   * cache its vtable qword
    //   * on growth, arena-allocate 56 bytes via CS2's arena helper,
    //     stamp CS2's vtable in slot 0, zero the rest exactly as above.
    //
    // GAP-B fix (2026-07-10 14:47): sig-scan for CS2's own CSubtickMoveStep
    // vtable landed in dllmain via fva::log::find_csubtick_vtable.  On this
    // build it hit RVA 0x1996670 (VA 0x00007FFEDEB36670 in the 14:47 run).
    // With the vtable cached we could arena-allocate + stamp + zero, but we
    // still need CS2's arena helper (matches FVA's sub_7FFBE8103070) to
    // avoid a plain heap fallback that would break Message::MergeFrom's
    // arena-ownership check.  For now keep the pre-alloc-only path — the
    // vtable discovery is a prerequisite, not the finish line.
    if (!subs->rep) return false;
    if (subs->current_size >= fva::version::layout::subtick_moves_capacity) return false;
    // FVA-1:1 append-always fix (2026-07-10 19:03) — user requires spoof
    // on every fire.  Old guard `allocated_size >= total_size` failed 99%
    // of the time because CS2 keeps the Rep at capacity.  New guard just
    // checks bounds: current_size < total_size means we can safely write
    // to elements[current_size] without OOB.  We drop the old pointer
    // (CS2's own CSubtickMoveStep) — tier0 MemAlloc chain via our stamp
    // makes destructor delete-compat, so no crash on cleanup.
    if (subs->current_size >= subs->total_size) return false;

    // REACTIVATED 2026-07-10 17:11 — user confirmed "ИГРА НЕ КРАШНУЛАСЬ Я
    // ЕЕ ЗАКРЫЛ СЛУЧАЙНО" — the 13-append test was NOT a crash, user
    // closed cs2 accidentally.  Our append actually works.  The correct
    // type (CSGOInputHistoryEntryPB, 120 B) via CS2's own
    // CSGOInputHistoryEntryPB::New (sub_180742730, resolved via
    // known-RVA fingerprint) is delete-compat with CS2's Rep destructor
    // through tier0's MemAlloc_FreeFunc.
    auto* raw_bytes  = reinterpret_cast<std::uint8_t*>(subs)
                     - fva::version::layout::user_cmd_subtick_moves_field;
    auto* raw        = reinterpret_cast<CUserCmd*>(raw_bytes);
    CBaseUserCmdPB* pb = raw->base;
    void* arena = chain_find_arena(raw, pb, subs);

    // Alloc CSGOInputHistoryEntryPB (120 bytes) via CS2's New — was
    // labeled CSubtickMoveStep by legacy code but the real type on
    // this build is CSGOInputHistoryEntryPB.
    void* entry_raw = alloc_and_init_step(arena);
    if (!entry_raw) return false;
    auto* entry = reinterpret_cast<std::uint8_t*>(entry_raw);

    // Alloc a CMsgQAngle to hold pitch/yaw/roll deltas.  Same allocator
    // route (CS2's CMsgQAngle::New) — delete-compat.
    ProtoNewFn qfn = resolve_cs2_cmsgqangle_new();
    if (!qfn) return false;
    auto* qangle = reinterpret_cast<std::uint8_t*>(qfn(arena));
    if (!qangle) return false;

    // Fill CMsgQAngle @ CMsgQAngle layout: x @ +0x18, y @ +0x1C, z @ +0x20
    *reinterpret_cast<float*>(qangle + 0x18)    = pitch_delta;
    *reinterpret_cast<float*>(qangle + 0x1C)    = yaw_delta;
    *reinterpret_cast<float*>(qangle + 0x20)    = 0.0f;
    *reinterpret_cast<std::uint32_t*>(qangle + 0x10) |= 0x7u;  // X|Y|Z bits

    // Attach CMsgQAngle to entry.view_angles @ +0x18
    *reinterpret_cast<void**>(entry + 0x18) = qangle;

    // FVA 1:1 EXACT — verified via workflow wq0rgtslf 2026-07-10 (89
    // agents, Rep-slot deep dive).  FVA writes at outer+0x60/0x64/0x68:
    //   +0x60 (int32): pressed value (a5 arg to sub_7FFBE82E8C32).  On
    //          self-echo tick FVA passes 0.  On armed tick it's the raw
    //          user-cmd pressed state.
    //   +0x64 (float): when_fraction — 1.0f for single-step append (FVA
    //          uses xmm9 from interpolator, but 1.0 works for one step).
    //   +0x68 (u64):   cleared to 0.
    *reinterpret_cast<std::int32_t*>(entry + 0x60) = 0;      // pressed
    *reinterpret_cast<float*>(entry + 0x64) = 1.0f;          // when
    *reinterpret_cast<std::uint64_t*>(entry + 0x68) = 0;     // clear tail

    // has_bits |= 0x1E01 (NOT just 0x1) — FVA sets 5 bits atomically:
    //   0x0001 = VIEW_ANGLES presence
    //   0x0200 = RENDER_TICK_COUNT
    //   0x0400 = RENDER_TICK_FRACTION
    //   0x0800 = PLAYER_TICK_COUNT
    //   0x1000 = PLAYER_TICK_FRACTION
    // Verified in FVA disasm: `or dword ptr [rbx+10h], 1E01h`.
    *reinterpret_cast<std::uint32_t*>(entry + 0x10) |= 0x1E01u;

    // Publish into Rep — DIRECT overwrite path (2026-07-10 19:03).
    // Old swap-safe path required allocated_size < total_size which failed
    // 99% of the time.  Simple overwrite: write our entry at
    // elements[current_size], bump current_size.  If allocated_size < new
    // current_size, sync it up.  Original slot pointer is dropped (leaked
    // via CS2's arena but not crashing per tier0 delete-compat chain).
    subs->rep->elements[subs->current_size] =
        reinterpret_cast<CSubtickMoveStep*>(entry_raw);
    subs->current_size += 1;
    if (subs->rep->allocated_size < subs->current_size) {
        subs->rep->allocated_size = subs->current_size;
    }
    return true;
}

#if 0  // Preserved old block for reference
namespace preserved {
inline void old_wrong_type() {
    CSubtickMoveStep* step = alloc_and_init_step(arena);
    if (!step) return false;

    // Fill CS2 CSubtickMoveStep fields DIRECTLY (no CMsgQAngle attach):
    // - CS2's CSubtickMoveStep::New already zeroed the block and set the
    //   correct vtable via sub_1804B27C0 (verified in IDA session
    //   f73dcd28: mem[+8] = arena, mem[+0] = vftable, all data fields
    //   zeroed).
    // - We only OR the has_bits and write pitch_delta / yaw_delta at
    //   the canonical CS2 protobuf offsets.  These match the CS2
    //   client.dll build we're on (verified same session).  The +0x18
    //   button field stays 0 (CS2's default) — do NOT write anything
    //   there; FVA's CMsgQAngle-attach @+0x18 is for FVA's EXTENDED
    //   proto that differs from CS2's stock CSubtickMoveStep.
    step->pitch_delta = pitch_delta;
    step->yaw_delta   = yaw_delta;
    step->when        = 1.0f;
    step->has_bits   |= CSUBTICKMOVESTEP_BITS_WHEN
                    |  CSUBTICKMOVESTEP_BITS_PITCH_DELTA
                    |  CSUBTICKMOVESTEP_BITS_YAW_DELTA;

    // MINIMAL Rep publish — leave allocated_size alone.  Overwrite
    // elements[current_size] (loses the cached CS2-alloc'd ptr that
    // used to be there; that block gets leaked but Rep destructor
    // won't touch it since allocated_size stays at its old value).
    //
    // The previous swap-based version (matching FVA sub_7FFBE82E8C32)
    // extended allocated_size which appears to trigger CS2 internal
    // Rep-management corruption ~11 s after the first apply.  We keep
    // allocated_size stable so CS2's destructor sees the exact
    // element-count it originally cached.
    return;
}
}  // namespace preserved
#endif

} // namespace

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

// -------------------------------------------------------------------------
// subs sanity gate — refuses to invoke view-angle emitter on rep that looks stale,
// still transitioning, or lives in an arena we don't recognize.
//
// Concrete filters (each MUST hold before we touch rep->elements[]):
//   1. rep != nullptr                                      (map cmd built)
//   2. rep in user-mode heap: 0x0000_0400.. 0x0000_7FFF   (broad)
//   3. total_size in [1, 100]                              (protobuf sane)
//   4. 0 <= current_size <= total_size                     (invariant)
//   5. arena in {NULL, valid user-mode range}              (not garbage)
//   6. rep->allocated_size in [current_size, total_size]  (invariant)
//
// Silent-ish (only prints when it decides NOT-safe to avoid log flood).
// -------------------------------------------------------------------------
template<typename T>
bool subs_looks_safe(valve::pb::raw::RepeatedPtrField_t<T>* subs,
                     std::uint32_t call_no, bool verbose) noexcept
{
    if (!subs) return false;

    // filter 1: rep must exist
    auto* rep = subs->rep;
    if (!rep) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u subs->rep=null → "
                        "skip view-angle emitter\n", call_no);
        return false;
    }

    // filter 2: rep must be in reasonable user-mode heap range
    const auto rep_ui = reinterpret_cast<std::uintptr_t>(rep);
    if (rep_ui < 0x0000010000000000ULL || rep_ui >= 0x00007FFF00000000ULL) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u rep=%p out-of-range "
                        "→ skip view-angle emitter\n", call_no, (void*)rep);
        return false;
    }

    // filter 3+4: sizes in expected ranges
    const int total = subs->total_size;
    const int curr  = subs->current_size;
    if (total < 1 || total > 100 || curr < 0 || curr > total) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u sizes bad "
                        "curr=%d total=%d → skip view-angle emitter\n",
                        call_no, curr, total);
        return false;
    }

    // filter 5: arena is NULL or in user-mode range
    const auto arena_ui = reinterpret_cast<std::uintptr_t>(subs->arena);
    if (arena_ui != 0 &&
        (arena_ui < 0x0000010000000000ULL || arena_ui >= 0x00007FFF00000000ULL)) {
        if (verbose)
            std::printf("[fva_recon][ViewSpoof][gate] apply#%u arena=%p out-of-range "
                        "→ skip view-angle emitter\n", call_no, subs->arena);
        return false;
    }

    // filter 6: rep->allocated_size must be in [current_size, total_size]
    // We wrap in SEH because if rep is stale, reading allocated_size crashes.
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

// ============================================================================
// FVA view-angle emitter emitter — 1:1 port of sub_7FFBE82E8C32.
//
// Decompiled from C:\vmp\FVA.dll.i64 session ea21efe4 and cross-
// referenced with the disasm in docs/FVA_SECTION_J_TRUE_2026-07-10.md.
//
// Pseudocode (identical to FVA):
//   remaining_slots = 16 - subs->current_size
//   for iter in 0..remaining_slots:
//     fraction = (iter + 1) / remaining_slots
//     interp_pitch = engine_view.pitch + fraction * delta.pitch
//     interp_yaw   = engine_view.yaw   + fraction * delta.yaw
//     interp_roll  = engine_view.roll  + fraction * delta.roll
//     when         = fraction              // render_tick_fraction
//
//     step   = CS2_CSGOInputHistoryEntryPB_New(subs.arena)    // 120 B
//     qangle = CS2_CMsgQAngle_New(nullptr)                    // 40 B
//     qangle{ x=interp_pitch, y=interp_yaw, z=interp_roll, has_bits |= 0x7 }
//
//     step[+0x18] = qangle                 // view_angles
//     step[+0x60] = cached_tick_count      // render_tick_count carry-over
//     step[+0x64] = when                   // render_tick_fraction
//     step[+0x68] = 0                      // player_tick_count / fraction
//     step->has_bits |= 0x1E01             // VIEW|R_TC|R_TF|P_TC|P_TF
//
//     // inline swap-safe AddAllocated:
//     if (rep && rep->allocated_size < subs->total_size) {
//        if (subs->current_size < rep->allocated_size)
//            rep->elements[allocated_size] = rep->elements[current_size]
//        rep->elements[current_size++] = step
//        rep->allocated_size++
//     } else break     // slow-path (sub_7FFBE80CB348) — Rep grow — TBD
//
// The cached_tick_count carry-over is FVA's "button state accumulator"
// (misnamed in the disasm — the field at +0x60 is render_tick_count on
// this build, per CSGOInputHistoryEntryPB layout).  Scanned from any
// pre-existing entries in the same Rep before the emit loop.
// ============================================================================
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

    // ---------------------------------------------------------------
    // FVA @ +0xB13..+0xB6C — pre-emit scan: iterate existing entries,
    // cache last-known render_tick_count into var_1D0 (button
    // accumulator equivalent).  All emitted entries will inherit this.
    // ---------------------------------------------------------------
    if (subs->rep) {
        for (int i = 0; i < subs->current_size; ++i) {
            auto* step = reinterpret_cast<std::uint8_t*>(subs->rep->elements[i]);
            if (!step) continue;
            out_cached_tick_count =
                *reinterpret_cast<const std::uint32_t*>(step + 0x60);
        }
    }

    // ---------------------------------------------------------------
    // FVA @ +0xBE0 — remaining_slots = 16 - current_size.
    // Cap the emitter at the layout maximum.
    // ---------------------------------------------------------------
    const int cap = fva::version::layout::subtick_moves_capacity;   // 16
    const int remaining = cap - subs->current_size;
    if (remaining <= 0) return true;   // nothing to emit but not an error

    // Resolve CS2 T::New wrappers.  Both are cached after first hit.
    ProtoNewFn step_new   = resolve_cs2_input_history_new();
    ProtoNewFn qangle_new = resolve_cs2_cmsgqangle_new();
    if (!step_new || !qangle_new) return false;

    // FVA passes subs->arena to the step allocator so that step & subs
    // share the same memory owner (fast-path arena_consistency check).
    void* subs_arena = subs->arena;

    // ==========================================================
    // HEAVY DIAGNOSTIC — log Rep state before we start emitting.
    // ==========================================================
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
        static constexpr float k_when_base  = 0.023590f;  // dword_7FFBE82190B8
        static constexpr float k_when_delta = 0.908453f;  // dword_7FFBE82190BC
        const float fraction     = static_cast<float>(iter + 1) /
                                    static_cast<float>(remaining);
        const float interp_pitch = engine_pitch + fraction * d_pitch;
        const float interp_yaw   = engine_yaw   + fraction * d_yaw;
        const float interp_roll  = engine_roll  + fraction * d_roll;
        const float when         = k_when_base + fraction * k_when_delta;

        // Alloc CSGOInputHistoryEntryPB via CS2's own T::New (SEH-guarded).
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

        // FVA 1:1 — qangle_new called with NULL, NOT subs_arena.
        // From sub_7FFBE82E8C32 decompile:
        //   _RAX = qword_7FFBE823A9A8(0);  // qangle = qangle_new(0)
        // Then cross-arena copy to step's arena via sub_7FFBE8101CC0
        // if step's arena != qangle's arena.
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
            // Read step/qangle arena_words to verify what CS2 stamped
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

        // Fill CMsgQAngle: pitch/yaw/roll + presence bits (SEH-guarded).
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

        // -----------------------------------------------------------
        // FVA 1:1 cross-arena copy — sub_7FFBE8101CC0.
        // Verified via IDA Hex-Rays decompile:
        //   v30 = step_arena_effective (with bit 0 indirection, bit 1 null-tag)
        //   v33 = qangle_arena_effective (same tag handling)
        //   if (v30 != v33) qangle = sub_7FFBE8101CC0(v30, qangle);
        //
        // sub_7FFBE8101CC0(target_arena, source, r8=0):
        //   if (!target_arena || r8): return source (would register)
        //   else: new = source.vtable[+0x10](source, target_arena);
        //         new.vtable[+0x30](new, source);  // MergeFrom
        //         return new;
        //
        // Arena untag lambda: FVA-1:1 with bit 0 indirection + bit 1 null.
        //   if (arena_word & 2) → effective = 0
        //   else: effective = arena_word & ~3
        //         if (arena_word & 1) → effective = *(u64*)effective
        // -----------------------------------------------------------
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
                    // Cross-arena copy: alloc new qangle in step's arena via
                    // vtable[+0x10] = New(Arena*), then MergeFrom via
                    // vtable[+0x30].  Matches FVA sub_7FFBE8101CC0 else-branch.
                    using NewInArenaFn = void*(__fastcall*)(void*, void*);
                    using MergeFromFn  = void(__fastcall*)(void*, const void*);

                    auto** src_vtbl = *reinterpret_cast<void***>(qangle);
                    auto new_fn = reinterpret_cast<NewInArenaFn>(src_vtbl[2]);  // vt+0x10
                    void* new_qangle_raw =
                        new_fn(qangle,
                                reinterpret_cast<void*>(v_step_arena));
                    if (new_qangle_raw) {
                        auto** dst_vtbl = *reinterpret_cast<void***>(new_qangle_raw);
                        auto merge_fn = reinterpret_cast<MergeFromFn>(dst_vtbl[6]);  // vt+0x30
                        merge_fn(new_qangle_raw, qangle);
                        qangle = reinterpret_cast<std::uint8_t*>(new_qangle_raw);
                    }
                    // else: step's arena is NULL, both effectively heap → no copy.
                }
                // else: !target_arena — FVA would register source in NULL arena;
                // our port treats this as no-op (heap-heap match).
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                std::printf("[fva_recon][B2-EM][#%llu iter=%d] SEH in cross-arena copy! "
                            "Aborting emit.\n",
                            (unsigned long long)call_no, iter);
                std::fflush(stdout);
                break;
            }
        }

        // Attach qangle (possibly new one from cross-arena copy) &
        // fill step template (SEH-guarded).
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

        // -----------------------------------------------------------
        // FVA 1:1 arena consistency check (MISSED IN PRIOR VERSION):
        //   if (*subs != step.arena) → slow path (sub_7FFBE80CB348)
        // Our port previously skipped this check → wrong-arena step
        // inserted into Rep → CS2 Rep destructor free()'s step with
        // wrong arena → AV on next tick.  CRITICAL FIX 2026-07-11.
        // -----------------------------------------------------------
        // FVA 1:1 AddAllocated arena check with proper bit 0 indirection.
        //   v34 = *(step+8)
        //   if (v34 & 2) v35 = 0
        //   else { v35 = v34 & ~3; if (v34 & 1) v35 = *v35 }
        //   if (*subs == v35) → fast path
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
        // For consistency with logs below
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
            // -------------------------------------------------------
            // 1:1 port of FVA sub_7FFBE80CB348 (Rep AddAllocated slow-path).
            //
            // Three sub-cases handled by add_allocated_slow_path():
            //   A) rep NULL OR current == total → grow via IMemAlloc, then insert.
            //   B) allocated < total (with cached-swap) → swap-append & insert.
            //   C) allocated == total (rare with arena=0) → grow & insert.
            //
            // Arena-mismatch case (subs.arena != step.arena.untagged) is
            // handled in the CROSS-ARENA COPY block earlier in this loop
            // via vtable[+0x10]/[+0x30], so by the time we hit this else,
            // the arenas are guaranteed to agree.
            // -------------------------------------------------------
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

            // ISOLATION TEST 3 — Rep grow via add_allocated_slow_path RE-ENABLED
            // now that move_crc write is confirmed as crash source and disabled.
            // If cs2 stays stable with grow enabled: Rep grow is safe alone.
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

    // Rate-limited entry telemetry — first 16 calls verbose then every 128.
    static std::atomic<std::uint32_t> s_calls{0};
    const std::uint32_t call_no = s_calls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (!g_ready.load(std::memory_order_acquire))         return;
    // NOTE: FVA has NO equivalent of `g_target_armed` — view-angle emitter fires
    // autonomously on every tick where `byte_7FFBE823A9A0 != 0` (our
    // `fire_flag_snapshot`).  The previous version gated on `g_target_armed`
    // which meant apply() bailed forever unless someone called
    // `set_target_angle` — no such call exists in FVA (verified from
    // FIRE_LOGIC_DECODED_2026-07-10.md).  Remove the gate; if no target
    // was set, fall through to a self-echo (target = current viewangles,
    // delta = 0) so the pipeline runs but the wire is untouched.  Real
    // anti-aim targets can still be driven by set_target_angle at any
    // point — it just isn't a prerequisite to firing.
    if (!raw)                                             return;

    CBaseUserCmdPB* pb = raw->base;
    if (!pb) return;

    // Cache CS2's real vtables from live entries — needed by
    // alloc_and_init_step / alloc_and_init_qangle.  Idempotent.
    auto* subs_early = reinterpret_cast<RepeatedPtrField_t<CSubtickMoveStep>*>(
        reinterpret_cast<std::uint8_t*>(raw) +
        fva::version::layout::user_cmd_subtick_moves_field);
    try_cache_vtables(pb, subs_early);

    // Snapshot the viewangles + subtick count BEFORE our mutation.
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

    // iter1 fix (viewangles gap): FVA @ +0x9F9 ORs the has-bit
    // UNCONDITIONALLY — it does NOT gate on the pointer being non-null
    // (an unpopulated CMsgQAngle sub-message serialises to zero bytes anyway
    // and downstream parsers accept the presence flag as ground truth).
    // The prior port gated on qang != nullptr which produced a first-tick
    // divergence against baseline FVA captures.
    pb->has_bits |= CBASEUSERCMDPB_BITS_VIEWANGLES;
    CMsgQAngle* qang = pb->viewangles;
    if (!qang) {
        // FVA 1:1 (workflow wq0rgtslf audit) — FVA does NOT bail when
        // pb->viewangles is null.  It lazy-allocates a CMsgQAngle via
        // qword_7FFBE823A9A8 (CS2's CMsgQAngle::New) and stores it in
        // pb->viewangles.  Match that behavior using our known-RVA
        // resolver.  Falls through to bail only if allocation itself fails.
        ProtoNewFn qfn = resolve_cs2_cmsgqangle_new();
        if (!qfn) return;
        qang = reinterpret_cast<CMsgQAngle*>(qfn(nullptr));
        if (!qang) return;
        pb->viewangles = qang;
    }

    // FVA @ +0xA20: snapshot prior view-angles.  These are the values we
    // will write back at the end; between now and then only the subtick
    // field changes, not qang itself.
    const float prior_x = qang->x;
    const float prior_y = qang->y;
    const float prior_z = qang->z;

    // FVA 1:1 REFERENCE (verified via IDA newFVA1 session 2026-07-10 17:56):
    //   Handler A @ 0x7FFBE80CA6C1: `vsubss xmm0, xmm13, [rbx]` where
    //   rbx = qword_7FFBE823C250 (FVA's 20-byte struct, init'ed to
    //   [0.0f, 0.0f, 0.0f, byte_flag=1, int_seq=-1] by sub_7FFBE80B3DD8).
    //
    //   Since [rbx+0..8] = [0,0,0], FVA's delta = viewangles - 0 =
    //   viewangles itself.  This is the ACTUAL spoof — server sees "player
    //   rotated from (0,0,0) to (current) this tick" = nonsense delta
    //   that anti-cheat rotation tracking doesn't handle correctly.
    //
    //   Our previous SELF-ECHO (tgt = prior → delta=0) was WRONG.  User
    //   armed target still works when explicitly set.
    static std::atomic<std::uint32_t> s_reference_bits[3]{};  // default 0.0f
    float tgt_x, tgt_y, tgt_z;
    if (g_target_armed.load(std::memory_order_acquire)) {
        tgt_x = float_of(g_target_bits[0].load(std::memory_order_relaxed));
        tgt_y = float_of(g_target_bits[1].load(std::memory_order_relaxed));
        tgt_z = float_of(g_target_bits[2].load(std::memory_order_relaxed));
    } else {
        // FVA-parity: read reference from static-init struct (all zeros)
        tgt_x = float_of(s_reference_bits[0].load(std::memory_order_relaxed));
        tgt_y = float_of(s_reference_bits[1].load(std::memory_order_relaxed));
        tgt_z = float_of(s_reference_bits[2].load(std::memory_order_relaxed));
    }

    // FVA @ +0xA72F..+0xA7BE — 3-AXIS wrap180 delta (verified via disasm):
    //   vsubss xmm0, xmm13, [rbx+0x00]   ; pitch delta pre-wrap
    //   call fmodf(360) → wrap → xmm8
    //   vsubss xmm0, xmm11, [rbx+0x04]   ; yaw delta pre-wrap
    //   call fmodf(360) → wrap → xmm7
    //   vsubss xmm0, xmm12, [rbx+0x08]   ; roll delta pre-wrap
    //   call fmodf(360) → wrap → xmm0
    //   vmovss [rbp+0x50], xmm8    ; store pitch
    //   vmovss [rbp+0x54], xmm7    ; store yaw
    //   vmovss [rbp+0x58], xmm0    ; store roll (NOT discarded — FVA writes all 3)
    const float d_pitch = wrap180(prior_x - tgt_x);
    const float d_yaw   = wrap180(prior_y - tgt_y);
    const float d_roll  = wrap180(prior_z - tgt_z);

    // NOTE (2026-07-10): Earlier attempt at a "button-gate" was based on a
    // misread of FVA disasm.  `test r12b, 0x01` at 0x7FFBE80CA915 is NOT
    // a button check — r12d holds `nSlot` (the 2nd arg to CreateMove,
    // loaded via `mov r12d, edi` at 0x7FFBE80C9DE5).  Those bits are the
    // low bits of the player-slot index, not IN_ATTACK.  Reverted.

    // FVA @ +0xB6C: append one interpolation step covering the delta.
    // FVA's helper can spread the delta across up to `remaining` steps at
    // fractional `when` values; a single step at when=1.0 is functionally
    // equivalent for the server-side unwind and keeps allocation pressure
    // predictable.
    auto* subs = reinterpret_cast<RepeatedPtrField_t<CSubtickMoveStep>*>(
        reinterpret_cast<std::uint8_t*>(raw) +
        fva::version::layout::user_cmd_subtick_moves_field);

    // FVA 1:1 FULL EMITTER (workflow 2026-07-10):
    // Replaces the earlier promote-in-place fallback with a faithful
    // port of sub_7FFBE82E8C32.  Multi-iteration loop, fractional
    // interpolator, render_tick_count carry-over, inline AddAllocated
    // fast path.  Slow path (Rep grow via sub_7FFBE80CB348) still TBD —
    // rare in gameplay because CS2 pre-sizes the Rep.
    //
    // The Rep at raw+0x28 stores CSGOInputHistoryEntryPB (120 B)
    // entries — verified via live vtable extraction at RVA 0x1A1E028.
    // We reinterpret_cast because our public API keeps the historical
    // CSubtickMoveStep name for source-compat.
    auto* input_history_rep =
        reinterpret_cast<RepeatedPtrField_t<CSGOInputHistoryEntryPB>*>(subs);

    // ---------------------------------------------------------------------
    // GATE 1: engine2 state — IsInGame() && IsConnected() && !g_shutting_down.
    // Replaces the earlier tick-based grace period with an authoritative
    // check on CNetworkGameClient state.  When engine2 says "not in game" or
    // "not connected", CS2 is either at main menu, in map transition, or
    // shutting down — writing to pb->subtick_moves.rep during any of these
    // states corrupts arena memory that networksystem later reads (repro:
    // crash at networksystem+0xB7AE5 during "Loading map de_mirage").
    //
    // fva::hooks::engine2::init() must run before any view-angle emitter invocation
    // (called from install_all).  If it hasn't or resolution failed, this
    // returns false (fail-safe → SELF-ECHO only).
    // ---------------------------------------------------------------------
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

    // ---------------------------------------------------------------------
    // GATE 2: subs sanity — rejects stale/transitioning rep pointers.
    // ---------------------------------------------------------------------
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

    // One-shot diagnostic: first 4 emits print state so we can verify
    // the emitter actually wrote what we expect into the Rep.  Read
    // the FIRST just-emitted entry (index = current_size - 1).
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

    // FVA 1:1 (workflow wq0rgtslf audit) — verified via IDA disasm of
    // Handler A @ 0x7FFBE80CA9C4-CD:
    //   `vmovss dword ptr [rbx], xmm13`      ; ref[0] = current pitch
    //   `vmovss dword ptr [rbx+4], xmm11`    ; ref[4] = current yaw
    //   `vmovss dword ptr [rbx+8], xmm12`    ; ref[8] = current roll
    // FVA writes CURRENT viewangles to REFERENCE CACHE (qword_7FFBE823C250),
    // NOT back to qang.  This updates the reference for NEXT tick's delta
    // computation: next tick will read this as prior_x/y/z, compute
    // delta = new_view - this_tick_view (small rotation delta).
    //
    // Our previous code wrote back to qang which cancels our own write —
    // NOT what FVA does.  Correct behavior: update reference cache.
    s_reference_bits[0].store(bits_of(prior_x), std::memory_order_relaxed);
    s_reference_bits[1].store(bits_of(prior_y), std::memory_order_relaxed);
    s_reference_bits[2].store(bits_of(prior_z), std::memory_order_relaxed);

    // -------------------------------------------------------------------
    // Category 4 (checklist audit) — pb->random_seed via CS2 ComputeRandomSeed.
    // FVA's view-angle emitter sets BITS_RANDOM_SEED and populates random_seed.
    // If the resolved helper is available we call it for a server-valid
    // seed; otherwise leave the field untouched (has_bits also stays clear).
    // ComputeRandomSeed(rcx=pb_or_null, rdx=arena_or_null, r8=tick) → uint32.
    // -------------------------------------------------------------------
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
                // Fall through with has_bits unset — CS2 will use its own
                // seed on the server side.
            }
        }
    }
    (void)cached_tick_count;  // silence unused when crs is null

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

} // namespace fva::view_angle_spoofer
