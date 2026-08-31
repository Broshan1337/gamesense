// ============================================================================
// view_angle_spoofer.h
//
// Мутирует CBaseUserCmdPB.viewangles + добавляет фейковые
// CSGOInputHistoryEntryPB в CSGOUserCmdPB.input_history через тот же
// arena-аллокатор, что и CS2 game code — сервер видит цепочку subtick'ов
// "игрок повернулся с A в B за один tick", вместо реального мгновенного
// снапа мыши перед выстрелом.  Anti-cheat rewind check ползёт по subtick'ам
// backwards и стреляет из неправильного места → противник не получает урон.
//
// Поток мутации (по одному вызову apply на attack tick):
//   1. pb->has_bits |= BITS_VIEWANGLES (0x4)      — форс сериализации поля
//   2. prior = pb->viewangles                     — snapshot текущего значения
//   3. delta = wrap180_each(prior - target)       — угловая разность
//   4. for iter in 0..remaining_free_slots:
//        step   = CS2_CSGOInputHistoryEntryPB::New(subs.arena)   — 120 B
//        qangle = CS2_CMsgQAngle::New(nullptr)                   — 40 B
//        qangle.x/y/z = prior + fraction*delta                   — интерп
//        step[+0x18] = qangle    (viewAngles)
//        step->has_bits |= 0x1E01  (presence flags)
//        AddAllocated(subs, step)  → fast/slow path
//   5. Update reference cache для delta computation в следующем tick
//   6. pb->random_seed = ComputeRandomSeed(tick)  — server-valid seed
//   7. pb->has_bits |= BITS_RANDOM_SEED (0x8)
//
// Attack-gate:  apply() вызывается create_move_hook.cpp только когда
//    pb->has_bits & (ATTACK1_START_HISTORY_INDEX | ATTACK2_START_HISTORY_INDEX)
//    (биты 0x20 / 0x40).  См. steamtracking-protos/csgo/cs_usercmd.proto —
//    CS2 set'ит их когда игрок стреляет.  На non-attack tick apply skip'ается,
//    watcher-логи в create_move_hook продолжают storing per-tick state.
//
// Target angle: set_target_angle() позволяет извне задать viewangles куда
// сервер должен думать что игрок смотрит.  Если target не armed —
// self-echo (delta=0), spoof минимально влияет на wire.
// ============================================================================
#pragma once

namespace valve::pb::raw { struct CUserCmd; }

namespace fva::view_angle_spoofer
{

// One-shot init — no drift-prone resolution required today (all offsets are
// compile-time from the layout header, all writes are direct field stores).
// Kept as an init() to match the module pattern used by the rest of the tree
// and to allow later addition of a CS2-side subtick allocator sig-scan.
bool init();

// Feed the target view-angle the mutation will wrap TOWARDS.  Safe to call
// from any thread; apply() reads with acquire semantics.
void set_target_angle(float pitch, float yaw, float roll) noexcept;

// Clear the target — apply() becomes a no-op until set again.
void clear_target_angle() noexcept;

// Runs the Phase B2 mutation on the freshly-built CUserCmd handed to Hook A.
// No-op if init() hasn't run, no target is set, base is null, or viewangles
// hasn't been lazy-allocated by an earlier phase.
void apply(valve::pb::raw::CUserCmd* raw) noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace fva::view_angle_spoofer
