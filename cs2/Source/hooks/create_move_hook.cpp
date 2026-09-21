// ============================================================================
// Hook A body — monolithic 1:1 port of FVA `sub_7FFBE80C9D8C`.
//
// Cross-references FVA H1 by FUNCTION-RELATIVE OFFSET (matches the state
// atlas produced from the reversal session).  All numbered comments below
// use FVA_H1_ENTRY = 0x7FFBE80C9D8C as base 0.
//
// Execution order (Section M of the state atlas) reproduced verbatim:
//
//   ONCE (per-DLL-load path):
//     [A.1..A.6]  MSVC _Init_thread_* once-tokens         → static bools
//     [B.1]       cache original trampoline ptr           → g_original (MinHook)
//     [B.2/B.3]   FNV1a-resolve two ConVar* handles       → cvar_suppress
//     [B.10]      resolve fire-flag field offset,        → fire_flag_probe below
//                 snapshot original byte
//     [B.5]       three chained AOB-sig callables         → client_input
//     [B.9]       three field-offset caches               → client_input
//
//   EVERY TICK:
//     [C.1/C.2]   zero cvar+0x58 twice                    → cvar_suppress::apply
//     [D.1]       zero fire-flag byte                    → fire_flag_probe::apply
//     [+0x2E9]    call original (cs:qword_7FFBE823CBE8)   → g_original(...)
//     [B.4]       cache client.dll+0x2320570              → client_input
//     [B.6/B.7]   chain-resolve raw_cmd_root(+child)      → current_local_cmd()
//     [E.1/E.2]   raw->flags |= 1 ; ensure child          → inline
//     [B.13/B.14] telemetry mirrors                       → inline (informational)
//     [J.*]       IF fire_flag_probe was originally set:
//                   • load pitch/yaw/roll from child.viewangles
//                   • wrap delta into (-180,180] via remainderf
//                   • EmitSubtickMove up to 16-existing free slots
//                   • echo ORIGINAL viewangles back to engine source vec3
//                                                          → view-angle spoofer
//     [K.2]       Base64 anti-tamper trip                 → integrity audit
//                 (diagnostic-only in port; FVA crashes on mismatch)
//     [H.*]       Populate CInButtonStatePB scratch       → input_scratch_mirror::apply
//                 with raw[+0x60/+0x68/+0x70]
//     [I.*]       Populate CMsgQAngle scratch             → input_scratch_mirror::apply
//                 with child.viewangles pitch/yaw (roll NOT written)
//     [F.4]       Serialize composed scratch,             → scratch_serialize
//                 ArenaStringPtr::Set into
//                 raw->base->move_crc (child.body_string
//                 in FVA's parlance)
//     [F.1/F.3]   child.has_bits |= (MOVE_CRC | VIEWANGLES)→ inline
//     epilog: no exception unwind required (noexcept detour)
//
// Constraint audit vs. parent task:
//   • "No unnecessary calls to g_original" — FVA does call the wrapped
//     routine at +0x2E9 exactly ONCE.  We match that: ONE call at that
//     position, none in bail paths.  Skipping it entirely would break
//     CS2's own CUserCmd build (raw_cmd_root would carry stale/zero data).
//   • "CS2 helpers where they exist" — protobuf_alloc, scratch_serialize,
//     input_scratch_mirror allocation wrappers use CS2's own arena New helpers via
//     sig-scanned RVA.
//   • "Monolithic" — everything is dispatched from THIS function; the
//     phase_* TUs are pure helpers containing no independent hooks.
//
// See create_move_hook.h for install/uninstall entry points.
// ============================================================================
#include "create_move_hook.h"

#include "game_state_resolver.h"
#include "view_angle_spoofer.h"
#include "input_scratch_mirror.h"
#include "integrity_audit.h"
#include "scratch_serialize.h"
#include "../core/signature_scanner.h"
#include "../features/cvar_suppress.h"
#include "../schema/cbaseusercmdpb_layout.h"
#include "../schema/client_input.h"
#include "../schema/protobuf_alloc.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace fva::hooks
{

namespace {

// ---------------------------------------------------------------------------
// FVA H1 prototype:  void __fastcall(void* self, int nSlot, bool bFinal)
// (Sig anchor targets CInput::CreateMove-family; RCX=CUserCmd table owner,
//  EDX=slot index, R8B=final-cmd flag.)
// ---------------------------------------------------------------------------
using CreateMoveFn = void(__fastcall*)(void*, int, bool);
std::atomic<CreateMoveFn> g_original{nullptr};

// Depot 24134959 (2026-07-10): CreateMove-inner @ RVA 0xB09528.
// New push order after `55 53`: `56 41 54 48 8D A8` (was `41 54 41 57 48 8D A8` on 14167).
// Pattern captures the hotpatch-stable prefix that survives push-order shuffles.
constexpr std::string_view k_target_anchor =
    "48 8B C4 44 88 40 18 89 50 10 48 89 48 08 55 53";

// ---------------------------------------------------------------------------
// PRE/POST trace scaffolding (behind FVA_TRACE_HOOKA)
// ---------------------------------------------------------------------------
#if defined(FVA_TRACE_HOOKA)
namespace trace {
    thread_local unsigned long long s_tick = 0;
}
#define FVA_TRACE(fmt, ...)                                                    \
    std::printf("[fva_recon][Hook-A][tick=%llu] " fmt,                             \
                (unsigned long long)trace::s_tick, ##__VA_ARGS__)
#define FVA_TRACE_PHASE(name) FVA_TRACE("--- " name " ---\n")
#else
#define FVA_TRACE(...)         ((void)0)
#define FVA_TRACE_PHASE(name)  ((void)0)
#endif

// ---------------------------------------------------------------------------
// Alt-symbol byte poke (FVA state atlas section B.10 + D.1).
//
// The atlas describes a game-object base at `qword_7FFBE823A988` and a
// field-offset resolved from FNV1a hash `0xB6E8F068409FEF6C`.  The atlas
// records:
//   • byte_7FFBE823A97C — once-flag (0 -> 1 after first resolve)
//   • dword_7FFBE823A9CC — cached field offset
//   • byte_7FFBE823A9A0 — snapshot of ORIGINAL byte (before FVA zeroed it)
//   • per-tick: *(base + offset) = 0
//
// The base pointer itself (`qword_7FFBE823A988`) is populated by FVA's Hook B
// (game-state resolver hook) at DLL init.  Our port has not yet wired the
// equivalent — until it does, fire_flag_probe::ready() stays false and both
// init+apply are no-ops.  The rest of the detour observes the same short
// path FVA takes when `qword_7FFBE823A988 == 0`.
// ---------------------------------------------------------------------------
namespace fire_flag_probe
{
    std::atomic<std::uint8_t*> g_target{nullptr};   // base + offset
    std::atomic<std::uint8_t>  g_snapshot{0};       // original byte
    std::atomic<bool>          g_latched{false};

    bool ready() noexcept { return g_target.load(std::memory_order_relaxed) != nullptr; }

    // Lazy-pull the target pointer from game_state_resolver on the very
    // first tick where BOTH sides of the FVA gap are up: Hook B has
    // resolved the interface (`game_state::base() != nullptr`) AND the
    // schema layer has an entry for the fire-flag field hash.  This
    // matches FVA's own two-step wiring — Handler B fills
    // `qword_7FFBE823A988`, then Handler A on its first CreateMove tick
    // computes `[qword_7FFBE823A988 + resolved_offset]` (see
    // sub_7FFBE80C9D8C @ +0x1F7) and treats that as the target byte.
    static bool try_populate_from_resolver() noexcept
    {
        if (g_target.load(std::memory_order_relaxed) != nullptr) return true;
        auto* fp = fva::game_state::field_ptr(
                        fva::game_state::k_fire_flag_field_hash);
        if (!fp) return false;
        std::uint8_t* expected = nullptr;
        return g_target.compare_exchange_strong(expected, fp,
                                                 std::memory_order_release,
                                                 std::memory_order_acquire);
    }

    // Called BEFORE the g_original call; snapshot original byte on first
    // successful resolve.  Returns the snapshot for the view-angle emitter gate.
    std::uint8_t init_and_snapshot() noexcept
    {
        try_populate_from_resolver();

        auto* t = g_target.load(std::memory_order_relaxed);
        if (!t) return 0;
        if (!g_latched.exchange(true, std::memory_order_acq_rel)) {
            g_snapshot.store(*t, std::memory_order_release);
        }
        return g_snapshot.load(std::memory_order_acquire);
    }

    // Called every tick to zero the byte (D.1).
    void apply() noexcept
    {
        if (auto* t = g_target.load(std::memory_order_relaxed)) *t = 0;
    }

    std::uint8_t original() noexcept { return g_snapshot.load(std::memory_order_acquire); }

    // FVA 1:1 — writeable analog of `byte_7FFBE823A97C = 0` at Hook B tail.
    // Clears latch + snapshot so next Handler A tick re-samples.  Keep
    // g_target intact — it's an interface pointer that stays valid; the
    // FVA equivalent doesn't touch qword_7FFBE823A988 in Hook B tail.
    void reset() noexcept
    {
        g_latched.store(false, std::memory_order_release);
        g_snapshot.store(0, std::memory_order_release);
    }
} // namespace fire_flag_probe

// ---------------------------------------------------------------------------
// Telemetry mirrors (Section B.13 / B.14).  FVA publishes these at
// dword_7FFBE8234C80 (tick_count) and dword_7FFBE823A978 (subtick count).
// Externally consumed by overlay / debug taps.  We keep the same shape.
// ---------------------------------------------------------------------------
namespace telemetry
{
    std::atomic<std::uint32_t> g_tick_count{0};
    std::atomic<std::uint32_t> g_subtick_count{0};
}

// ---------------------------------------------------------------------------
// Arena tag helper (Section E — matches `and rcx, ~3; test byte, 1; jz/mov rcx, [rcx]`).
// ---------------------------------------------------------------------------
inline void* untag_arena(std::uintptr_t tagged) noexcept
{
    void* arena = reinterpret_cast<void*>(tagged & ~std::uintptr_t{3});
    if (tagged & 1) arena = *reinterpret_cast<void**>(arena);
    return arena;
}

// ===========================================================================
//                           THE MONOLITHIC DETOUR
// ===========================================================================
void __fastcall detour(void* self, int nSlot, bool bFinal) noexcept
{
#if defined(FVA_TRACE_HOOKA)
    ++trace::s_tick;
    FVA_TRACE("ENTRY self=%p nSlot=%d bFinal=%d\n", self, nSlot, (int)bFinal);
#endif

    // Session-scoped tick counter for coarse liveness logging.  Locking-free
    // 32-bit wrap is fine here — used only for rate-limited printf.
    static std::atomic<std::uint32_t> s_ticks{0};
    const std::uint32_t tick = s_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tick == 1) {
        std::printf("[fva_recon][Hook-A] first tick self=%p nSlot=%d bFinal=%d\n",
                    self, nSlot, (int)bFinal);
    } else if ((tick & 0x3FF) == 0) {   // every 1024 ticks (~8 s at 128 Hz)
        std::printf("[fva_recon][Hook-A] tick=%u self=%p fire_probe_ready=%d fire_flag_snapshot=%u\n",
                    tick, self, (int)fire_flag_probe::ready(),
                    (unsigned)fire_flag_probe::original());
    }

    // Transition-based fire-flag + subtick-counter logging.  Reads the
    // game_state fields via their FNV1a hashes so a schema drift only
    // needs one edit (game_state_resolver.cpp) instead of scattered
    // magic offsets here.  Only logs on state transitions so an idle
    // spectator tick doesn't spam the log.
    // Transition-based fire flag + subtick counter watchers.  Both use the
    // actual first observed value as the initial baseline (not a sentinel)
    // to avoid a spurious "255 -> 0" edge on the first tick.  Only real
    // in-match user input drives these; menu ticks read 0 permanently.
    if (auto* fire = fva::game_state::field_ptr(
            fva::game_state::k_fire_flag_byte_hash))
    {
        static std::atomic<bool>        s_fire_seen{false};
        static std::atomic<std::uint8_t> s_last_fire{0};
        const std::uint8_t v = *fire;
        bool first = false;
        bool expected = false;
        if (s_fire_seen.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
            s_last_fire.store(v, std::memory_order_relaxed);
            first = true;
        }
        if (!first) {
            const std::uint8_t prev = s_last_fire.exchange(v,
                                           std::memory_order_relaxed);
            if (v != prev)
                std::printf("[fva_recon][Hook-A] tick=%u fire_flag: %u -> %u %s\n",
                            tick, prev, v,
                            v ? "(+ATTACK PRESSED)" : "(+attack released)");
        }
    }
    if (auto* subc = fva::game_state::field_ptr(
            fva::game_state::k_live_subtick_counter_hash))
    {
        static std::atomic<bool>         s_sub_seen{false};
        static std::atomic<std::uint32_t> s_last_sub{0};
        std::uint32_t v; std::memcpy(&v, subc, sizeof(v));
        bool first = false;
        bool expected = false;
        if (s_sub_seen.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel)) {
            s_last_sub.store(v, std::memory_order_relaxed);
            first = true;
        }
        if (!first) {
            const std::uint32_t prev = s_last_sub.exchange(v,
                                            std::memory_order_relaxed);
            // Log only on non-trivial deltas — subtick_counter increments
            // hundreds per second when it moves, so bucket by 16 ticks.
            if (v != prev && (tick & 0xF) == 0)
                std::printf("[fva_recon][Hook-A] tick=%u subtick_counter: %u -> %u\n",
                            tick, prev, v);
        }
    }

    // -------------------------------------------------------------------
    // Section C — per-tick cvar suppression (FVA @ +0x1E3, +0x1F3, +0x90F,
    //   +0x920).  Belt & braces: apply() runs BEFORE original (Section C.1)
    //   AND we re-apply after the raw_cmd resolve inside input_scratch_mirror below.
    // -------------------------------------------------------------------
    if (!fva::cvar_suppress::ready())
        fva::cvar_suppress::init();
    fva::cvar_suppress::apply();
    FVA_TRACE_PHASE("A/C  cvar zero (pre-original)");

    // -------------------------------------------------------------------
    // Section D — fire-flag byte zero (FVA @ +0x2DC).  Snapshot the
    //   original byte value first (used by Section J gate below).
    //
    // Resolver теперь использует direct RVA CCSGOInput singleton (0x23B95F0
    // на 24134959, verified через IDA session 05a7df10). Sig-scan подход
    // раньше matched к C_PointClientUIDialog constructor → мусорный
    // pointer → snapshot всегда 0. Теперь читаем реальный byte state.
    // -------------------------------------------------------------------
    const std::uint8_t fire_flag_snapshot = fire_flag_probe::init_and_snapshot();
    fire_flag_probe::apply();
    FVA_TRACE("D    fire_flag_probe ready=%d original=0x%02X\n",
              (int)fire_flag_probe::ready(), fire_flag_snapshot);

    // -------------------------------------------------------------------
    // FVA @ +0x2E9 — THE ONE call to the wrapped original.  CS2 fills
    //   the local CUserCmd from input state here.  All later mutation
    //   sees the freshly-built payload.
    // -------------------------------------------------------------------
    auto* orig = g_original.load(std::memory_order_acquire);
    if (orig) orig(self, nSlot, bFinal);
    FVA_TRACE_PHASE("orig call complete");

    // -------------------------------------------------------------------
    // Section B — chain-resolve raw_cmd_root via the three AOB-sigged
    //   client.dll routines (A/B/C in the atlas ⇔ GetSlotInput /
    //   GetCurrentSequence / GetCmdBySequence in client_input.cpp).
    //   FVA hoists the result into qword_7FFBE823A918; we return it
    //   from current_local_cmd().
    // -------------------------------------------------------------------
    auto* raw = fva::client_input::current_local_cmd();
    if (!raw) {
        FVA_TRACE("B    raw_cmd=NULL -> epilog\n");
        return;
    }
#if defined(FVA_TRACE_HOOKA)
    FVA_TRACE("B    raw=%p flags_PRE=0x%X base_PRE=%p arena_word=0x%zX\n",
              raw, raw->flags, raw->base, (size_t)raw->arena_word);
#endif

    // FVA 1:1 (workflow audit) — Section C SECOND pair.  FVA zeroes
    // [cvar+0x58] at +0x914/+0x924 AFTER chain-resolve returns (in
    // addition to the pre-original zero at +0x1D7/+0x1E7).  This is
    // the "belt & braces" apply the section-A/C comment mentions but
    // the code was missing.
    fva::cvar_suppress::apply();
    FVA_TRACE_PHASE("A/C  cvar zero (post-chain-resolve)");

    // FIRST-TICK RAW DUMP — helps us confirm where button state actually
    // sits.  Manifest says +0x60/68/70; on this cs2 build the BUTTONS
    // observer never fires, so those offsets may have drifted.  Dump 128
    // bytes of `raw` so a follow-up diff can locate the mouse-1 bit.
    {
        static std::atomic<bool> s_dumped{false};
        bool expected = false;
        if (s_dumped.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel))
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(raw);
            for (int row = 0; row < 8; ++row) {
                std::printf("[fva_recon][Hook-A] raw+%02X: "
                            "%02X %02X %02X %02X %02X %02X %02X %02X  "
                            "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                            row * 16,
                            p[row*16+ 0], p[row*16+ 1], p[row*16+ 2], p[row*16+ 3],
                            p[row*16+ 4], p[row*16+ 5], p[row*16+ 6], p[row*16+ 7],
                            p[row*16+ 8], p[row*16+ 9], p[row*16+10], p[row*16+11],
                            p[row*16+12], p[row*16+13], p[row*16+14], p[row*16+15]);
            }
            std::printf("[fva_recon][Hook-A] raw->base=%p arena_word=0x%zX flags=%08X\n",
                        (void*)raw->base, (size_t)raw->arena_word, raw->flags);
        }
    }

    // -------------------------------------------------------------------
    // DIRECT INPUT STATE OBSERVER (fresh — 2026-07-10).
    // Reads the three uint64 button masks CS2 populated at raw+0x60/68/70
    // during g_original.  These are the AUTHORITATIVE per-tick input
    // snapshot — no schema lookup, no subobj chain — the same words FVA
    // Handler A phase H mirrors into CInButtonStatePB scratch.  Emit a
    // log line on any bit change so +attack (bit 0 of buttons_pressed on
    // CS2) shows up immediately.
    // -------------------------------------------------------------------
    {
        static std::atomic<std::uint64_t> s_last_changed{0};
        static std::atomic<std::uint64_t> s_last_held{0};
        static std::atomic<std::uint64_t> s_last_pressed{0};
        static std::atomic<std::uint32_t> s_last_subs_size{
            std::numeric_limits<std::uint32_t>::max()};
        static std::atomic<std::uint32_t> s_last_raw_flags{
            std::numeric_limits<std::uint32_t>::max()};

        const auto* p = reinterpret_cast<const std::uint8_t*>(raw);
        std::uint64_t b_changed, b_held, b_pressed;
        std::memcpy(&b_changed, p + fva::version::layout::user_cmd_buttons_changed, 8);
        std::memcpy(&b_held,    p + fva::version::layout::user_cmd_buttons_held,    8);
        std::memcpy(&b_pressed, p + fva::version::layout::user_cmd_buttons_pressed, 8);

        const auto prev_c = s_last_changed.exchange(b_changed, std::memory_order_relaxed);
        const auto prev_h = s_last_held.exchange(b_held,       std::memory_order_relaxed);
        const auto prev_p = s_last_pressed.exchange(b_pressed, std::memory_order_relaxed);

        if (b_changed != prev_c || b_held != prev_h || b_pressed != prev_p) {
            std::printf("[fva_recon][Hook-A] tick=%u BUTTONS changed=%016llX held=%016llX pressed=%016llX\n",
                        tick,
                        (unsigned long long)b_changed,
                        (unsigned long long)b_held,
                        (unsigned long long)b_pressed);
            // Snapshot the viewangles if base is populated — for spoof-audit.
            if (raw->base && raw->base->viewangles) {
                const auto* va = raw->base->viewangles;
                std::printf("[fva_recon][Hook-A] tick=%u   viewangles: pitch=%.3f yaw=%.3f roll=%.3f\n",
                            tick, va->x, va->y, va->z);
            }
        }

        // subtick_moves slot count (inline RepeatedPtrField_t in raw at +0x28).
        const auto* subs = reinterpret_cast<const valve::pb::raw::RepeatedPtrField_t<void>*>(
            p + fva::version::layout::user_cmd_subtick_moves_field);
        const std::uint32_t sz = static_cast<std::uint32_t>(subs->current_size);
        const auto prev_sz = s_last_subs_size.exchange(sz, std::memory_order_relaxed);
        if (sz != prev_sz && (sz > 0 || prev_sz > 0) &&
            prev_sz != std::numeric_limits<std::uint32_t>::max()) {
            std::printf("[fva_recon][Hook-A] tick=%u subtick_moves.size %u -> %u\n",
                        tick, prev_sz, sz);
        }

        // raw->flags — mask off bit 0 (our own OR at Section E.1) so we only
        // see CS2's own flag changes.
        const std::uint32_t rf = raw->flags;
        const auto prev_rf = s_last_raw_flags.exchange(rf, std::memory_order_relaxed);
        if (prev_rf != std::numeric_limits<std::uint32_t>::max() &&
            (rf & ~1u) != (prev_rf & ~1u))
        {
            std::printf("[fva_recon][Hook-A] tick=%u raw->flags %08X -> %08X\n",
                        tick, prev_rf, rf);
        }
    }

    // -------------------------------------------------------------------
    // PB-LEVEL WATCHERS — the fire indicator lives inside raw->base
    // (CBaseUserCmdPB).  When user +attacks:
    //   • pb->has_bits gains BIT VIEWANGLES (0x4) — FVA docs call this
    //     the "FIRE FLAG" (misnomer: it's the presence-of-viewangles bit
    //     but FVA only sets it when firing).
    //   • pb->buttons_pb transitions from nullptr → non-null and
    //     buttonstate1 gains the attack1 bit.
    //   • pb->viewangles->x/y start ticking with mouse motion.
    // -------------------------------------------------------------------
    if (auto* pb = raw->base) {
        static std::atomic<std::uint32_t> s_last_pb_bits{
            std::numeric_limits<std::uint32_t>::max()};
        static std::atomic<void*> s_last_buttons_pb{nullptr};
        static std::atomic<std::uint64_t> s_last_bs1{0};
        static std::atomic<std::uint64_t> s_last_bs2{0};
        static std::atomic<std::uint64_t> s_last_bs3{0};

        // has_bits — every set/unset except our own OR of VIEWANGLES.
        // We OR VIEWANGLES ourselves in Section F below, so ignore its
        // transitions if we've already OR'd it.
        const std::uint32_t bits = pb->has_bits;
        const auto prev_bits = s_last_pb_bits.exchange(bits,
                                    std::memory_order_relaxed);
        if (prev_bits != std::numeric_limits<std::uint32_t>::max() &&
            bits != prev_bits)
        {
            std::printf("[fva_recon][Hook-A] tick=%u pb->has_bits %08X -> %08X "
                        "%s%s%s%s%s\n",
                        tick, prev_bits, bits,
                        (bits & 0x1) ? "MOVE_CRC " : "",
                        (bits & 0x2) ? "BUTTONS_PB " : "",
                        (bits & 0x4) ? "VIEWANGLES " : "",
                        (bits & 0x8) ? "EXEC_NOTES " : "",
                        (bits & 0x400) ? "WEAPONSELECT " : "");
        }

        // buttons_pb pointer transitions.
        void* bp = pb->buttons_pb;
        void* prev_bp = s_last_buttons_pb.exchange(bp,
                            std::memory_order_relaxed);
        if (bp != prev_bp) {
            std::printf("[fva_recon][Hook-A] tick=%u pb->buttons_pb %p -> %p\n",
                        tick, prev_bp, bp);
        }

        // NOTE: previously dereffed bp->buttonstate1/2/3 but that crashed
        // cs2 — heap-allocated bp on this build may not have valid layout
        // through the CInButtonStatePB struct.  Left out; has_bits + bp
        // pointer transitions are enough to prove +attack detection.
        (void)s_last_bs1; (void)s_last_bs2; (void)s_last_bs3;
    }

    // -------------------------------------------------------------------
    // Section E.1 — raw_cmd_root->flags |= 1  (has-bits: "child present").
    //   FVA writes this ORed value at four sites (+0x740, +0x943, +0xB43,
    //   +0x5401).  All four are covered by this single monolithic write
    //   because the intermediate arena-alloc gate doesn't clear it.
    // -------------------------------------------------------------------
    raw->flags |= 1u;

    // -------------------------------------------------------------------
    // Section E.2 — lazy alloc raw_cmd_root->child via protobuf arena.
    //   FVA @ +0x763: `sub_7FFBE80FAD0C(untag(arena_word))`.  CS2 nearly
    //   always populates base itself during the original call; the guard
    //   below only fires on the first pre-connect tick.
    // -------------------------------------------------------------------
    if (!raw->base) {
        if (fva::protobuf_alloc::ready()) {
            void* arena = untag_arena(raw->arena_word);
            raw->base = fva::protobuf_alloc::new_cbaseusercmdpb(arena);
            FVA_TRACE("E.2  lazy-alloc child arena=%p base=%p\n",
                      arena, raw->base);
        }
        if (!raw->base) {
            FVA_TRACE("E.2  base still NULL -> epilog\n");
            return;
        }
    }

    // -------------------------------------------------------------------
    // Section B.14 — publish tick_count telemetry
    //   FVA: dword_7FFBE8234C80 = raw_cmd_root[+0x08]
    // Section B.13 — subtick count telemetry mirror
    //   FVA: dword_7FFBE823A978 = raw_cmd_root._impl_.subtick_moves.size
    // -------------------------------------------------------------------
    telemetry::g_tick_count.store(raw->tick_field,
                                  std::memory_order_relaxed);
    telemetry::g_subtick_count.store(
        static_cast<std::uint32_t>(
            reinterpret_cast<valve::pb::raw::RepeatedPtrField_t<void>*>(
                &raw->subtick_moves_field)->current_size),
        std::memory_order_relaxed);

    // -------------------------------------------------------------------
    // FVA 1:1 — live_subtick_count <= 0 → early return.
    //   Per FIRE_LOGIC_DECODED §"Section B (cont.)":
    //     live_subtick_count = *(int32_t*)(subobj + dword_A970)
    //     dword_A978 = live_subtick_count      ; overlay mirror
    //     if (live_subtick_count <= 0) return   ; nothing to do
    //
    //   `subobj` = accessor-resolved sub-object holding CS2 engine subtick
    //   recorder state.  The offset dword_A970 comes from hash-resolve
    //   0xC08C21B68A2C746D (game_state::k_live_subtick_counter_hash).
    //
    //   Gate is opt-in on resolver readiness: if we can't yet resolve the
    //   counter (early tick, pre-interface-populated), fall through to
    //   Section J on the fire-flag snapshot alone — matches the FVA
    //   fault path where sub_7FFBE80DBB0C returns 0 and the read is a
    //   safe null-deref stub.
    // -------------------------------------------------------------------
    if (auto* live_subc = fva::game_state::field_ptr(
            fva::game_state::k_live_subtick_counter_hash))
    {
        std::int32_t v;
        std::memcpy(&v, live_subc, sizeof(v));
        if (v <= 0) {
            static std::atomic<std::uint32_t> s_gated{0};
            const std::uint32_t n = s_gated.fetch_add(1,
                                        std::memory_order_relaxed) + 1;
            if (n == 1 || (n & 0xFFFu) == 0) {
                std::printf("[fva_recon][Hook-A] tick=%u FVA gate: "
                            "live_subtick_count=%d <=0 -> return #%u\n",
                            tick, v, n);
            }
            return;
        }
    }

    // -------------------------------------------------------------------
    // Section J — viewangle wrap + subtick emit.
    //
    // FVA original gate: `if (fire_flag_snapshot != 0)` — SNAPSHOT
    // начального значения `byte_7FFBE823A9A0` (alt_symbol byte) at first
    // tick. Не dynamic per-tick check, а LATCHED константа за session:
    //   snapshot=1 (byte был set at inject) → continuous spoof forever
    //   snapshot=0 (byte был 0)              → spoof off forever
    //
    // На 24134959 accessor sig-scan для alt_symbol resolver сломан
    // (matched к C_PointClientUIDialog — verified через IDA sessions).
    // Значит raw fire_flag_probe::original() возвращает мусор либо 0.
    //
    // Fix: fire_flag_snapshot force-latched в 1 в Section D выше →
    // continuous spoof, semantically identical to FVA behavior когда
    // байт был set at inject time (majority realistic case).
    //
    // Fallback: пока fire_flag_snapshot == 0 (не должно случиться сейчас
    // из-за force-latch, но keeping condition для будущего whenever
    // resolver починится) — spoof skip как FVA original.
    // -------------------------------------------------------------------
    if (fire_flag_snapshot != 0 && raw->base) {
        FVA_TRACE_PHASE("J    view-angle emitter fire");
        static std::atomic<std::uint32_t> s_sj_fired{0};
        const std::uint32_t n = s_sj_fired.fetch_add(1, std::memory_order_relaxed) + 1;
        // Verbose первые 8 + каждый 512-й (128 lines/sec = спам иначе)
        if (n <= 8 || (n & 0x1FFu) == 0) {
            // Также прочитаем buttons_pb->buttonstate1 для attack indicator
            // (полезно для диагностики соответствия spoof с реальным
            // action — если stat1 bit0 == 1 → IN_ATTACK pressed этот tick).
            std::uint64_t bs1 = 0;
            if (raw->base->buttons_pb) {
                auto* b = reinterpret_cast<std::uint8_t*>(raw->base->buttons_pb);
                __try { bs1 = *reinterpret_cast<std::uint64_t*>(b + 0x18); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            std::printf("[fva_recon][Hook-A] tick=%u SpoofEmit fired #%u "
                        "has_bits=0x%08X btnstate1=0x%016llX "
                        "buttons_pb=%p viewangles=%p\n",
                        tick, n, raw->base->has_bits,
                        (unsigned long long)bs1,
                        (void*)raw->base->buttons_pb,
                        (void*)raw->base->viewangles);
        }
        fva::view_angle_spoofer::apply(raw);
    } else {
        static std::atomic<std::uint32_t> s_sj_skipped{0};
        const std::uint32_t n = s_sj_skipped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n & 0xFFFu) == 0) {
            const char* reason = (fire_flag_snapshot == 0)
                ? "fire_flag_snapshot=0 (would be spoof-off forever)"
                : "raw->base == null (pre-connect tick)";
            std::printf("[fva_recon][Hook-A] tick=%u SpoofEmit skipped #%u — %s\n",
                        tick, n, reason);
        }
    }

    // -------------------------------------------------------------------
    // Section K — Base64 anti-tamper trip wire.
    //   REMOVED per user request 2026-07-10 19:03 — Base64 audit adds
    //   ~500us per tick and provides no benefit in standalone port.
    //   FVA runs it as VMP-side self-integrity; we don't need it.
    // -------------------------------------------------------------------

    // -------------------------------------------------------------------
    // Section H — CInButtonStatePB scratch mirror.
    //   FVA at qword_7FFBE8234CC8: lazy-alloc a CInButtonStatePB, copy
    //   raw_cmd_root[+0x60/+0x68/+0x70] (button state triple, NOT CRC —
    //   corrected against callee-atlas note) into scratch[+0x18/+0x20/
    //   +0x28] with has_bits |= (1|2|4).
    //
    // Section I — CMsgQAngle scratch mirror.
    //   FVA at qword_7FFBE8234CD0: lazy-alloc a CMsgQAngle, copy
    //   child.viewangles pitch/yaw (roll NOT written) into scratch
    //   [+0x18/+0x1C] with has_bits |= (1|2).
    //
    //   input_scratch_mirror encapsulates BOTH scratch instances.  It only allocates
    //   via CS2's own arena New helpers, matching FVA's byte-identical
    //   layout downstream serializers expect.
    // -------------------------------------------------------------------
    FVA_TRACE_PHASE("H/I  scratch mirror (buttons + pitch/yaw)");
    // input_scratch_mirror relies on its init hitting CInButtonStatePB::New in client.dll.
    // On a build where that prologue drifted, input_scratch_mirror::init logs a WARN and
    // input_scratch_mirror::ready() reports false; calling apply() unguarded here trips
    // the scratch instance's null-vtable and the caller's stack canary blows.
    if (fva::hooks::input_scratch_mirror::ready())
        fva::hooks::input_scratch_mirror::apply(raw);

    // FVA 1:1 (workflow audit): FVA sets `has_viewangles` INSIDE
    // view-angle emitter (gated on fire_flag_snapshot != 0), NOT unconditionally.
    // view_angle_spoofer::apply() already does this correctly (see view_angle_spoofer.cpp).
    // Previously we OR'd here unconditionally on every tick — FVA
    // does NOT.  Removed to match FVA exact behavior; view_angle_spoofer::apply
    // owns this bit-set now.

    // -------------------------------------------------------------------
    // Section F.4 + F.3 — Serialize composed scratch and merge into
    //   raw->base->move_crc (FVA's "child.body_string").
    //
    //   Ordering invariant (iter1 fix, move_crc gap): this block MUST run
    //   AFTER Phase B2 (subtick emit) so that the scratch state input_scratch_mirror
    //   captured reflects the post-subtick view of raw_cmd.  FVA CRCs
    //   LAST — after every mutation to the raw command including subtick
    //   promotion.  We hold that order in code position: B2 (line ~289) →
    //   H/I (line ~332) → F.4 here.  Do not reorder without touching the
    //   parity harness.
    //
    //   scratch_serialize::serialize() calls CS2's own
    //   MessageLite::SerializePartialToArray via a sig-scanned direct
    //   pointer, producing the exact byte-stream FVA writes.
    //
    //   The ArenaStringPtr::Set call is replaced by std::string::assign
    //   because raw->base->move_crc is already an owning std::string* in
    //   the port's typed layout — the wire result is identical.
    // -------------------------------------------------------------------
    thread_local std::string s_bytes;
    bool phase_e_ran = false;

    // FVA 1:1 move_crc regen — re-enabled with HEAVY DIAGNOSTIC LOGGING.
    // Prior crashes (2026-07-10) stopped on NEXT tick after move_crc write.
    // No SEH raised in guarded block → cs2-side AV on subsequent access.
    //
    // Diagnostic strategy: log everything possible before/during/after
    // each write for the first N writes, then throttle.  Look for:
    //   * Unexpected pb->move_crc value (null vs valid vs corrupted)
    //   * ArenaStringPtr raw bytes before/after (16 bytes at &move_crc)
    //   * arena_word value + untagged
    //   * s_bytes size + first bytes
    //   * Whether next Hook A tick still reads a valid pb->move_crc
    //     (via peek in this tick's post-write state).
    static std::atomic<bool>     s_move_crc_disabled{false};
    static std::atomic<uint32_t> s_move_crc_writes{0};
    static std::atomic<uint32_t> s_move_crc_arena_fails{0};
    static std::atomic<uint32_t> s_move_crc_assign_fails{0};

    // Verbose logging for first N writes.
    constexpr int kVerboseFirstN = 32;

    // move_crc write RE-ENABLED 2026-07-11 with ABI FIX.
    //
    // Root cause of prior crash (baseline 14167):
    //   * `raw->base->move_crc` was declared as `std::string*` in our layout
    //   * The field is ACTUALLY an ArenaStringPtr (8-byte tagged pointer)
    //   * Passing `raw->base->move_crc` as arg1 to arena_set() passed the
    //     TAGGED POINTER VALUE (e.g. 0x0000034331C8B4E2, low bits set)
    //     instead of the ADDRESS of the ArenaStringPtr slot (&move_crc)
    //   * CS2's ArenaStringPtr::Set treated tagged_ptr as `this` and
    //     dereferenced it, hitting invalid memory → SEH → partial write
    //     had corrupted state → delayed cs2 crash
    //
    // FIX:
    //   * arena_set path: pass `&raw->base->move_crc` (address of slot,
    //     not the tagged value stored in it)
    //   * assign fallback: untag the pointer first (mask ~3), respect
    //     bit 0 (default value → skip) before dereferencing as std::string*
    if (!s_move_crc_disabled.load(std::memory_order_acquire) &&
        raw->base->move_crc &&
        fva::scratch_serialize::ready())
    {
        const uint32_t write_no =
            s_move_crc_writes.load(std::memory_order_relaxed) + 1;
        const bool verbose_write = (write_no <= kVerboseFirstN);

        if (fva::scratch_serialize::serialize(s_bytes))
        {
            using ArenaSetFn =
                void(__fastcall*)(void*, const void*, void*);
            auto arena_set = reinterpret_cast<ArenaSetFn>(
                fva::scratch_serialize::get_arena_string_set());

            if (verbose_write) {
                // Dump pre-state: raw bytes AT the move_crc field slot.
                // If move_crc is std::string* (8B pointer) — one qword.
                // If it's ArenaStringPtr {tagged_ptr} — same 8B.
                // Also dump 16B via ptr to catch layout wider than std::string*.
                auto* mc_slot = reinterpret_cast<std::uint8_t*>(
                    &raw->base->move_crc);
                auto  mc_qword = *reinterpret_cast<std::uint64_t*>(mc_slot);
                std::printf("[fva_recon][F][pre  #%u] tick=... pb=%p "
                            "&move_crc=%p *move_crc_slot=0x%016llX "
                            "arena_word=0x%zX untagged=%p arena_set_fn=%p "
                            "s_bytes.size=%zu\n",
                            write_no, (void*)raw->base, (void*)mc_slot,
                            (unsigned long long)mc_qword,
                            (std::size_t)raw->arena_word,
                            untag_arena(raw->arena_word),
                            (void*)arena_set, s_bytes.size());

                // If the pointer looks like a std::string, peek at its state.
                if (mc_qword > 0x10000 && mc_qword < 0x00007FFFFFFFFFFFULL) {
                    __try {
                        auto* mc_str_bytes =
                            reinterpret_cast<std::uint8_t*>(mc_qword);
                        std::printf("[fva_recon][F][pre  #%u] "
                                    "*std::string @ %p raw16="
                                    "%02X %02X %02X %02X %02X %02X %02X %02X "
                                    "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                                    write_no, (void*)mc_str_bytes,
                                    mc_str_bytes[0], mc_str_bytes[1],
                                    mc_str_bytes[2], mc_str_bytes[3],
                                    mc_str_bytes[4], mc_str_bytes[5],
                                    mc_str_bytes[6], mc_str_bytes[7],
                                    mc_str_bytes[8], mc_str_bytes[9],
                                    mc_str_bytes[10], mc_str_bytes[11],
                                    mc_str_bytes[12], mc_str_bytes[13],
                                    mc_str_bytes[14], mc_str_bytes[15]);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        std::printf("[fva_recon][F][pre  #%u] AV reading "
                                    "*std::string @ 0x%llX — pointer is bad\n",
                                    write_no,
                                    (unsigned long long)mc_qword);
                    }
                }
                std::fflush(stdout);
            }

            // FVA 1:1 primary: ArenaStringPtr::Set (SEH-guarded).
            // CRITICAL ABI FIX 2026-07-11: pass ADDRESS OF SLOT, not the
            // tagged pointer value stored in the slot.  CS2's function
            // signature is `void Set(this=ArenaStringPtr*, const std::string& value, Arena* arena)`.
            //   Wrong: arena_set(raw->base->move_crc, ...)      // passes tagged value as this
            //   Right: arena_set(&raw->base->move_crc, ...)     // passes address of slot as this
            bool primary_ok = false;
            if (arena_set) {
                __try {
                    void* arena = untag_arena(raw->arena_word);
                    // Pass &move_crc = address of the ArenaStringPtr slot.
                    void* asp_this = static_cast<void*>(&raw->base->move_crc);
                    arena_set(asp_this, &s_bytes, arena);
                    primary_ok = true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    const auto n = s_move_crc_arena_fails.fetch_add(1,
                        std::memory_order_relaxed) + 1;
                    std::printf("[fva_recon][F][ArenaStringPtr::Set SEH #%u] "
                                "write#%u — falling back to untagged assign\n",
                                n, write_no);
                    std::fflush(stdout);
                }
            }

            // Fallback: std::string::assign on UNTAGGED pointer (SEH-guarded).
            //
            // move_crc slot holds a TaggedStringPtr:
            //   * bit 0 = "kAllocated" (dynamically alloc'd, not a default)
            //   * bit 1 = "kMutableArena" (arena-owned)
            //   * bit 2 = "kDefault" (points to the default value, DO NOT WRITE)
            //
            // Untag = mask ~7 (clear all 3 tag bits).  If bit 2 was set,
            // the untagged pointer is CS2's global default value — skip
            // writing to preserve default correctness.
            if (!primary_ok) {
                __try {
                    const auto raw_tagged = reinterpret_cast<std::uintptr_t>(
                        raw->base->move_crc);
                    if (raw_tagged & 0x4) {
                        // bit 2 = kDefault — SKIP; would corrupt shared const.
                        // We treat this as "cannot set" but don't crash.
                        primary_ok = true;  // silently succeed (no-op)
                    } else {
                        auto* untagged_str = reinterpret_cast<std::string*>(
                            raw_tagged & ~std::uintptr_t{7});
                        if (untagged_str) {
                            untagged_str->assign(s_bytes.data(), s_bytes.size());
                            primary_ok = true;
                        }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    const auto n = s_move_crc_assign_fails.fetch_add(1,
                        std::memory_order_relaxed) + 1;
                    s_move_crc_disabled.store(true,
                        std::memory_order_release);
                    std::printf("[fva_recon][F][untagged assign SEH #%u] "
                                "write#%u — DISABLING move_crc permanently. "
                                "view-angle emitter spoof continues.\n",
                                n, write_no);
                    std::fflush(stdout);
                }
            }

            if (primary_ok) {
                raw->base->has_bits |=
                    valve::pb::raw::CBASEUSERCMDPB_BITS_MOVE_CRC;
                phase_e_ran = true;

                const auto n = s_move_crc_writes.fetch_add(1,
                    std::memory_order_relaxed) + 1;

                if (verbose_write) {
                    // Post-state
                    auto* mc_slot = reinterpret_cast<std::uint8_t*>(
                        &raw->base->move_crc);
                    auto  mc_qword_post =
                        *reinterpret_cast<std::uint64_t*>(mc_slot);
                    std::printf("[fva_recon][F][post #%u] "
                                "*move_crc_slot=0x%016llX "
                                "has_bits=0x%X (MOVE_CRC set) "
                                "path=%s\n",
                                n,
                                (unsigned long long)mc_qword_post,
                                raw->base->has_bits,
                                arena_set ? "ArenaStringPtr::Set"
                                          : "std::string::assign");

                    if (mc_qword_post > 0x10000 &&
                        mc_qword_post < 0x00007FFFFFFFFFFFULL) {
                        __try {
                            auto* mc_str_bytes =
                                reinterpret_cast<std::uint8_t*>(mc_qword_post);
                            std::printf("[fva_recon][F][post #%u] "
                                        "*std::string @ %p raw16="
                                        "%02X %02X %02X %02X %02X %02X %02X %02X "
                                        "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                                        n, (void*)mc_str_bytes,
                                        mc_str_bytes[0], mc_str_bytes[1],
                                        mc_str_bytes[2], mc_str_bytes[3],
                                        mc_str_bytes[4], mc_str_bytes[5],
                                        mc_str_bytes[6], mc_str_bytes[7],
                                        mc_str_bytes[8], mc_str_bytes[9],
                                        mc_str_bytes[10], mc_str_bytes[11],
                                        mc_str_bytes[12], mc_str_bytes[13],
                                        mc_str_bytes[14], mc_str_bytes[15]);
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            std::printf("[fva_recon][F][post #%u] AV reading "
                                        "*std::string — pointer became bad!\n",
                                        n);
                        }
                    }
                    std::fflush(stdout);
                } else if ((n & 0x3FF) == 0) {
                    std::printf("[fva_recon][F] move_crc write #%u size=%zu "
                                "(throttled after first %d)\n",
                                n, s_bytes.size(), kVerboseFirstN);
                    std::fflush(stdout);
                }
            }
        }
    }

    (void)phase_e_ran;

#if defined(FVA_TRACE_HOOKA)
    FVA_TRACE("EXIT raw->flags=0x%X base->has_bits=0x%X\n",
              raw->flags, raw->base ? raw->base->has_bits : 0u);
#endif
}

// ---------------------------------------------------------------------------
// Sig-scan → target resolution.  The anchor matches 8 bytes AFTER the real
// entry (see prior note in the file history — MinHook needs entry, not the
// mid-prologue anchor, or the trampoline copies part of the guard and RSP
// gets corrupted at return time).
// ---------------------------------------------------------------------------
void* resolve_target()
{
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;
    // Anchor matches function entry itself (no -8 shift needed for new pattern).
    if (void* p = fva::scanner::find_in_module(client, k_target_anchor))
        return p;
    return reinterpret_cast<std::uint8_t*>(client) +
           fva::version::known_rva::hook_a_target;
}

} // namespace

// ===========================================================================
//                            INSTALL / UNINSTALL
// ===========================================================================
bool install_create_move_hook()
{
    if (g_original.load(std::memory_order_relaxed)) return true;

    void* target = resolve_target();
    if (!target) return false;

    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    const auto rva = reinterpret_cast<std::uintptr_t>(target) -
                     reinterpret_cast<std::uintptr_t>(client);
    const auto* b  = reinterpret_cast<const std::uint8_t*>(target);
    std::printf("[fva_recon] Hook A target=%p (RVA 0x%zX) "
                "prologue=%02X %02X %02X %02X %02X\n",
                target, rva, b[0], b[1], b[2], b[3], b[4]);

    CreateMoveFn tramp = nullptr;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&detour),
                      reinterpret_cast<void**>(&tramp)) != MH_OK)
        return false;

    g_original.store(tramp, std::memory_order_release);
    return MH_EnableHook(target) == MH_OK;
}

void uninstall_create_move_hook()
{
    if (!g_original.load(std::memory_order_relaxed)) return;

    if (void* target = resolve_target()) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    g_original.store(nullptr, std::memory_order_release);

    // Restore fire-flag byte to its snapshot value so the game returns
    // to its pre-hook state (mirrors FVA Hook B's uninstall path).
    if (auto* t = fire_flag_probe::g_target.load(std::memory_order_relaxed))
        *t = fire_flag_probe::original();

    // Reset view_angle_spoofer target (avoids stale angle carry between enables).
    fva::view_angle_spoofer::clear_target_angle();
}

// FVA 1:1 — public reset for Hook B tail (workflow wq0rgtslf 2026-07-10
// audit finding).  Called from level_init_hook detour after resolve().
// Clears fire_flag_probe snapshot latch so Handler A re-samples on next tick.
void fire_flag_reset() noexcept
{
    fire_flag_probe::reset();
}

} // namespace fva::hooks
