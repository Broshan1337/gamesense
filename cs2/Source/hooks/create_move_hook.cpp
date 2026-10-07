

























































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






using CreateMoveFn = void(__fastcall*)(void*, int, bool);
std::atomic<CreateMoveFn> g_original{nullptr};




constexpr std::string_view k_target_anchor =
    "48 8B C4 44 88 40 18 89 50 10 48 89 48 08 55 53";




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


















namespace fire_flag_probe
{
    std::atomic<std::uint8_t*> g_target{nullptr};   
    std::atomic<std::uint8_t>  g_snapshot{0};       
    std::atomic<bool>          g_latched{false};

    bool ready() noexcept { return g_target.load(std::memory_order_relaxed) != nullptr; }

    
    
    
    
    
    
    
    
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

    
    void apply() noexcept
    {
        if (auto* t = g_target.load(std::memory_order_relaxed)) *t = 0;
    }

    std::uint8_t original() noexcept { return g_snapshot.load(std::memory_order_acquire); }

    
    
    
    
    void reset() noexcept
    {
        g_latched.store(false, std::memory_order_release);
        g_snapshot.store(0, std::memory_order_release);
    }
} 






namespace telemetry
{
    std::atomic<std::uint32_t> g_tick_count{0};
    std::atomic<std::uint32_t> g_subtick_count{0};
}




inline void* untag_arena(std::uintptr_t tagged) noexcept
{
    void* arena = reinterpret_cast<void*>(tagged & ~std::uintptr_t{3});
    if (tagged & 1) arena = *reinterpret_cast<void**>(arena);
    return arena;
}




void __fastcall detour(void* self, int nSlot, bool bFinal) noexcept
{
#if defined(FVA_TRACE_HOOKA)
    ++trace::s_tick;
    FVA_TRACE("ENTRY self=%p nSlot=%d bFinal=%d\n", self, nSlot, (int)bFinal);
#endif

    
    
    static std::atomic<std::uint32_t> s_ticks{0};
    const std::uint32_t tick = s_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tick == 1) {
        std::printf("[fva_recon][Hook-A] first tick self=%p nSlot=%d bFinal=%d\n",
                    self, nSlot, (int)bFinal);
    } else if ((tick & 0x3FF) == 0) {   
        std::printf("[fva_recon][Hook-A] tick=%u self=%p fire_probe_ready=%d fire_flag_snapshot=%u\n",
                    tick, self, (int)fire_flag_probe::ready(),
                    (unsigned)fire_flag_probe::original());
    }

    
    
    
    
    
    
    
    
    
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
            
            
            if (v != prev && (tick & 0xF) == 0)
                std::printf("[fva_recon][Hook-A] tick=%u subtick_counter: %u -> %u\n",
                            tick, prev, v);
        }
    }

    
    
    
    
    
    if (!fva::cvar_suppress::ready())
        fva::cvar_suppress::init();
    fva::cvar_suppress::apply();
    FVA_TRACE_PHASE("A/C  cvar zero (pre-original)");

    
    
    
    
    
    
    
    
    
    const std::uint8_t fire_flag_snapshot = fire_flag_probe::init_and_snapshot();
    fire_flag_probe::apply();
    FVA_TRACE("D    fire_flag_probe ready=%d original=0x%02X\n",
              (int)fire_flag_probe::ready(), fire_flag_snapshot);

    
    
    
    
    
    auto* orig = g_original.load(std::memory_order_acquire);
    if (orig) orig(self, nSlot, bFinal);
    FVA_TRACE_PHASE("orig call complete");

    
    
    
    
    
    
    
    auto* raw = fva::client_input::current_local_cmd();
    if (!raw) {
        FVA_TRACE("B    raw_cmd=NULL -> epilog\n");
        return;
    }
#if defined(FVA_TRACE_HOOKA)
    FVA_TRACE("B    raw=%p flags_PRE=0x%X base_PRE=%p arena_word=0x%zX\n",
              raw, raw->flags, raw->base, (size_t)raw->arena_word);
#endif

    
    
    
    
    
    fva::cvar_suppress::apply();
    FVA_TRACE_PHASE("A/C  cvar zero (post-chain-resolve)");

    
    
    
    
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
            
            if (raw->base && raw->base->viewangles) {
                const auto* va = raw->base->viewangles;
                std::printf("[fva_recon][Hook-A] tick=%u   viewangles: pitch=%.3f yaw=%.3f roll=%.3f\n",
                            tick, va->x, va->y, va->z);
            }
        }

        
        const auto* subs = reinterpret_cast<const valve::pb::raw::RepeatedPtrField_t<void>*>(
            p + fva::version::layout::user_cmd_subtick_moves_field);
        const std::uint32_t sz = static_cast<std::uint32_t>(subs->current_size);
        const auto prev_sz = s_last_subs_size.exchange(sz, std::memory_order_relaxed);
        if (sz != prev_sz && (sz > 0 || prev_sz > 0) &&
            prev_sz != std::numeric_limits<std::uint32_t>::max()) {
            std::printf("[fva_recon][Hook-A] tick=%u subtick_moves.size %u -> %u\n",
                        tick, prev_sz, sz);
        }

        
        
        const std::uint32_t rf = raw->flags;
        const auto prev_rf = s_last_raw_flags.exchange(rf, std::memory_order_relaxed);
        if (prev_rf != std::numeric_limits<std::uint32_t>::max() &&
            (rf & ~1u) != (prev_rf & ~1u))
        {
            std::printf("[fva_recon][Hook-A] tick=%u raw->flags %08X -> %08X\n",
                        tick, prev_rf, rf);
        }
    }

    
    
    
    
    
    
    
    
    
    
    if (auto* pb = raw->base) {
        static std::atomic<std::uint32_t> s_last_pb_bits{
            std::numeric_limits<std::uint32_t>::max()};
        static std::atomic<void*> s_last_buttons_pb{nullptr};
        static std::atomic<std::uint64_t> s_last_bs1{0};
        static std::atomic<std::uint64_t> s_last_bs2{0};
        static std::atomic<std::uint64_t> s_last_bs3{0};

        
        
        
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

        
        void* bp = pb->buttons_pb;
        void* prev_bp = s_last_buttons_pb.exchange(bp,
                            std::memory_order_relaxed);
        if (bp != prev_bp) {
            std::printf("[fva_recon][Hook-A] tick=%u pb->buttons_pb %p -> %p\n",
                        tick, prev_bp, bp);
        }

        
        
        
        
        (void)s_last_bs1; (void)s_last_bs2; (void)s_last_bs3;
    }

    
    
    
    
    
    
    raw->flags |= 1u;

    
    
    
    
    
    
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

    
    
    
    
    
    
    telemetry::g_tick_count.store(raw->tick_field,
                                  std::memory_order_relaxed);
    telemetry::g_subtick_count.store(
        static_cast<std::uint32_t>(
            reinterpret_cast<valve::pb::raw::RepeatedPtrField_t<void>*>(
                &raw->subtick_moves_field)->current_size),
        std::memory_order_relaxed);

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
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

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (fire_flag_snapshot != 0 && raw->base) {
        FVA_TRACE_PHASE("J    view-angle emitter fire");
        static std::atomic<std::uint32_t> s_sj_fired{0};
        const std::uint32_t n = s_sj_fired.fetch_add(1, std::memory_order_relaxed) + 1;
        
        if (n <= 8 || (n & 0x1FFu) == 0) {
            
            
            
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

    
    
    
    
    
    

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    FVA_TRACE_PHASE("H/I  scratch mirror (buttons + pitch/yaw)");
    
    
    
    
    if (fva::hooks::input_scratch_mirror::ready())
        fva::hooks::input_scratch_mirror::apply(raw);

    
    
    
    
    
    

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    thread_local std::string s_bytes;
    bool phase_e_ran = false;

    
    
    
    
    
    
    
    
    
    
    
    
    static std::atomic<bool>     s_move_crc_disabled{false};
    static std::atomic<uint32_t> s_move_crc_writes{0};
    static std::atomic<uint32_t> s_move_crc_arena_fails{0};
    static std::atomic<uint32_t> s_move_crc_assign_fails{0};

    
    constexpr int kVerboseFirstN = 32;

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
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

            
            
            
            
            
            
            bool primary_ok = false;
            if (arena_set) {
                __try {
                    void* arena = untag_arena(raw->arena_word);
                    
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

            
            
            
            
            
            
            
            
            
            
            if (!primary_ok) {
                __try {
                    const auto raw_tagged = reinterpret_cast<std::uintptr_t>(
                        raw->base->move_crc);
                    if (raw_tagged & 0x4) {
                        
                        
                        primary_ok = true;  
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







void* resolve_target()
{
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return nullptr;
    
    if (void* p = fva::scanner::find_in_module(client, k_target_anchor))
        return p;
    return reinterpret_cast<std::uint8_t*>(client) +
           fva::version::known_rva::hook_a_target;
}

} 




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

    
    
    if (auto* t = fire_flag_probe::g_target.load(std::memory_order_relaxed))
        *t = fire_flag_probe::original();

    
    fva::view_angle_spoofer::clear_target_angle();
}




void fire_flag_reset() noexcept
{
    fire_flag_probe::reset();
}

} 
