// ============================================================================
// Hook A Phase C — implementation.  See input_scratch_mirror.h for FVA cross-reference.
// ============================================================================
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

// (Arena*) -> new-instance-pointer.  Both CInButtonStatePB::New and
// CMsgQAngle::New share the signature — differ only in body/type.
using AllocFn = void*(__fastcall*)(void*);

std::atomic<AllocFn> g_new_button_state{nullptr};
std::atomic<AllocFn> g_new_qangle{nullptr};
std::atomic<bool>    g_used_heap_fallback{false};

// OUR cache-pointer globals — live in this TU's BSS, NOT aliased to FVA's
// qword_7FFBE8234CC8 / CD0.
std::atomic<valve::pb::raw::CInButtonStatePB*> g_buttons{nullptr};
std::atomic<valve::pb::raw::CMsgQAngle*>       g_angles{nullptr};

// FVA-side "default fallback" QAngle singleton — when raw_cmd->base or
// base->viewangles is null, FVA reads from a zero-inited fallback at
// unk_7FFBE82356F8.  We reproduce that behaviour with a local zeroed struct
// so has_bits are still set unconditionally (F3 in workflow verify).
constexpr valve::pb::raw::CMsgQAngle k_zero_qangle{ {nullptr, nullptr}, 0, 0, 0.0f, 0.0f, 0.0f };

// RVA + fingerprint gate: reject if the function's first bytes don't match
// the expected MSVC prologue AND the size-immediate `mov edx, <size>` at
// +0x14 doesn't match the type-discriminant.  Mirrors protobuf_alloc.cpp's
// gate style; more robust than a raw sig-scan because it disambiguates
// same-size New helpers (CMsgVector is also 40 B, so size alone would be
// scan-order-dependent).
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

    // arena == nullptr → CS2's heap-fallback path.  We own the lifetime
    // (process-lifetime, never freed) so an arena would only get in the way.
    auto* fresh = static_cast<valve::pb::raw::CInButtonStatePB*>(fn(nullptr));
    if (!fresh) return nullptr;

    valve::pb::raw::CInButtonStatePB* expected = nullptr;
    if (g_buttons.compare_exchange_strong(expected, fresh,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return fresh;
    // Lost the race — the peer's alloc is already published; ours leaks one
    // 48-byte block, acceptable for a one-shot lazy init.
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

} // namespace

namespace {

// -------------------------------------------------------------------
// Sig-scan fallback for protobuf T::New(Arena*).  Used when the
// version_manifest RVA has drifted (the current CS2 build shipped a
// different code layout for CInButtonStatePB::New / CMsgQAngle::New).
//
// Strategy: search client.dll for the CANONICAL Google-Protobuf
// `T::New` shape and disambiguate by the size-immediate marker (each
// message type carries a unique size in its allocation call — 0x30
// for CInButtonStatePB, 0x28 for CMsgQAngle).
//
// The prologue below matches MSVC-generated protobuf message
// allocators in current CS2 builds.  We accept the FIRST match whose
// body carries the expected size immediate within a small window.
//
// Signature "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 33 D2":
//     mov [rsp+8], rbx      ; save rbx
//     push rdi
//     sub rsp, 0x20
//     mov rdi, rcx          ; arena in rcx
//     xor edx, edx          ; the arena==nullptr test
// This is boilerplate MSVC-generated protobuf New(Arena*), byte
// stable across most recent CS2 builds we've seen.
// -------------------------------------------------------------------
// FVA-verified sig for CS2's protobuf T::New(Arena*) (extracted from
// C:\vmp\FVA.dll.i64 session ea21efe4 — see
// docs/FVA_SECTION_J_TRUE_2026-07-10.md).  The previous port used
// `33 D2` (xor edx, edx) which does NOT match this cs2 build — the
// correct sequence has `33 DB` (xor ebx, ebx) BEFORE mov rdi, rcx AND
// `48 85 C9` (test rcx, rcx) AFTER it.
constexpr std::string_view k_protobuf_new_prologue =
    "48 89 5C 24 ? 57 48 83 EC ? 33 DB 48 8B F9 48 85 C9 75 ? B9 ? ? ? ? E8";

// Search a small window after the prologue for `mov edx/r8d, size_imm`
// where size_imm is 0x30 (CInButtonStatePB) or 0x28 (CMsgQAngle).
AllocFn scan_for_new(std::uint8_t* client_base, std::uint8_t size_marker,
                     const char* tag)
{
    std::size_t off = 0;
    for (int attempt = 0; attempt < 32; ++attempt) {
        // find_in_module returns the FIRST match; step past it each time
        // so we can walk multiple candidates.
        auto* hit = fva::scanner::find_in_module(client_base + off,
                                                    k_protobuf_new_prologue);
        if (!hit) {
            std::printf("[fva_recon] input_scratch_mirror %s: fallback sig-scan miss on attempt %d "
                         "(off=0x%zX, prologue not present in module)\n",
                         tag, attempt, off);
            return nullptr;
        }

        // Look for the size immediate in the first 48 bytes of the body.
        // Match "BA sz 00 00 00"      (mov edx, sz — arena path),
        //   or  "41 B8 sz 00 00 00"    (mov r8d, sz),
        //   or  "B9 sz 00 00 00"       (mov ecx, sz — HEAP path, matches FVA
        //                                sig ending "B9 ? ? ? ? E8").
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

        // No matching size marker — advance past this hit and keep
        // walking.  Recompute offset from client_base.
        off = static_cast<std::size_t>(hit - client_base) + 1;
    }
    std::printf("[fva_recon] input_scratch_mirror %s: fallback sig-scan exhausted 32 "
                 "candidates without a size_marker hit\n", tag);
    return nullptr;
}

// -------------------------------------------------------------------
// Heap-fallback allocators.  Called when we can't find CS2's own
// T::New(Arena*) in the module.  The buttons/angles caches are only
// WRITTEN by input_scratch_mirror::apply and buttons_cache()/angles_cache() are
// never consumed inside reconstruction (verified 2026-07-10 grep) —
// so a plain HeapAlloc block with the right size satisfies apply's
// invariants without needing CS2's protobuf vtable.
//
// This keeps input_scratch_mirror::ready() true on builds where the protobuf
// prologue drifted, which in turn lets create_move_hook's
// input_scratch_mirror::apply(raw) side-effects run.  Bit-for-bit-with-FVA at the
// wire level; deviating only in the private-side cache backing store.
// -------------------------------------------------------------------
void* heap_new_button_state(void* /*arena*/)
{
    return ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, 0x30);
}
void* heap_new_qangle(void* /*arena*/)
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

} // namespace

// -----------------------------------------------------------------------------
bool init()
{
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return false;
    auto* base = reinterpret_cast<std::uint8_t*>(client);

    namespace bf = fva::version::build_fingerprint;
    namespace kr = fva::version::known_rva;

    // Try the manifest-cached RVA first (fastest, no scan).  If it
    // drifted, fall to sig-scan.  If sig-scan misses too, install
    // heap-fallback so input_scratch_mirror::apply keeps running (see comment on
    // heap_new_button_state for correctness argument).
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

// -----------------------------------------------------------------------------
void apply(const valve::pb::raw::CUserCmd* raw_cmd) noexcept
{
    if (!raw_cmd || !ready()) return;

    // --- buttons cache: mirror rbx+0x60/0x68/0x70 (uint64 button masks) --
    // Access via raw memory read at manifest-supplied offsets to keep the
    // dependency on the CUserCmd struct layout explicit.
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

    // --- angle cache: mirror raw->base->viewangles->{x,y} (pitch + yaw) --
    // FVA reads from a zero-inited fallback when either level is null; we
    // reproduce that via k_zero_qangle so has_bits are set unconditionally
    // and downstream consumers observe has_x()/has_y() = true even on early
    // ticks with no populated base.  Roll (z) is DELIBERATELY not written.
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

} // namespace fva::hooks::input_scratch_mirror
