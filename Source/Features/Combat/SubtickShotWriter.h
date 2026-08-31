#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/UserCmd.h>
#include <Utils/Optional.h>
#include <Utils/VerifyConsole.h>

// Writes SPREAD- AND PUNCH-COMPENSATED view angles into ONE input_history entry of the outgoing
// command - the shared firing-path helper used by BOTH consumers of AttackCommand:
//   * the rage aimbot (silent redirect: the entry carries the corrected angle, rendered view
//     untouched),
//   * the triggerbot (fires at the crosshair; the correction cancels the cone so the bullet
//     actually lands there - without it the shot is cone luck, gun- and movement-dependent).
//
// THE ONE-ENTRY RULE (the fake-bullets fix). The server interpolates the shooter's view ACROSS the
// history entries around the attack timestamp. Rewriting EVERY entry - each corrected for its own
// tick, all minus TODAY's aim punch - produced neighbouring samples that disagree with each other
// exactly while moving or spraying, and interpolation across them mixes the corrections into
// garbage: flash and sound, no hit. So this now edits ONLY the entry the attack references (the
// game's own attack1_start_history_index when present - a real click this tick - otherwise the
// newest entry, which is exactly the one AttackCommand will set), and makes that one entry
// self-consistent the way velocity-cs2's fire_gun does:
//   * view angles  = spread correction for ONE tick, minus the CURRENT aim punch,
//   * player_tick  = that SAME tick, STAMPED into the entry rather than assumed, so the seed we
//                    corrected for is the seed the server derives,
//   * render_tick  = the shot's rewind tick (+1) while backtracking, else the current server tick,
//   * sv_interp0 / sv_interp1 / cl_interp zeroed WHEN PRESENT, so no stale interpolation window
//     survives alongside the rewritten angles.
// Entries we do not touch stay exactly as the game filled them - i.e. mutually consistent.
//
// The previous velocity-extrapolation machinery (per-entry cone prediction from a recorded local
// velocity history) is gone BY CONSTRUCTION rather than lost: with a single entry there is only
// dt = 0 between sampling the weapon state and stamping the tick it applies to, so the live-read
// cone IS the cone at the stamped tick.
template <typename HookContext>
class SubtickShotWriter {
public:
    explicit SubtickShotWriter(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Every pointer is null-guarded - this sits on the input path and must never crash.
    //
    // Returns whether the shot this command would fire lands on the intended angle: true when no
    // spread compensation was requested (the raw aim IS the intent), or when the solver produced a
    // correction; false when compensation was requested but could not be computed (unresolvable
    // weapon state, no self-consistent correction, or a bail-out that would leave the shot on the
    // raw angle). Callers that hold fire on an unpredictable shot use this to suppress the attack
    // - the triggerbot ignores the value on purpose (its hitchance gates already decided to fire).
    //
    // `redirectedEntry` (optional out): when the shot went through an input_history ENTRY (the
    // silent path), receives that entry's index so the caller can point
    // attack1_start_history_index at it - without that marker the server never consults the entry
    // at all and fires its no-history fallback along the BASE view angles (the crosshair), which
    // is exactly the "silent aim shots go where I am pointing" symptom. -1 when no entry was
    // redirected (base-angle fallback).
    [[nodiscard]] bool run(cs2::CUserCmd* cmd, auto&& localPawn,
              float aimPitch, float aimYaw,
              float punchPitch, float punchYaw,
              float backtrackSimTime,
              bool compensateSpread,
              int* redirectedEntry = nullptr) const noexcept
    {
        if (redirectedEntry)
            *redirectedEntry = -1;

        const bool backtracking = backtrackSimTime > 0.0f;

        auto* const cmdBytes = reinterpret_cast<std::byte*>(cmd);

        int size{};
        std::memcpy(&size, cmdBytes + cs2::CUserCmd::kInputHistorySizeOffset, sizeof(size));
        if (size <= 0 || size > cs2::CUserCmd::kMaxInputHistoryEntries) {
            bool claimedLands = false;
            int claimedEntry = -1;
            if (size <= 0
                && claimRecycledHistoryEntry(cmdBytes, localPawn, aimPitch, aimYaw, punchPitch, punchYaw,
                                             backtrackSimTime, compensateSpread, backtracking, claimedLands, claimedEntry)) {
                if (redirectedEntry)
                    *redirectedEntry = claimedEntry;
                return claimedLands;
            }

            // MEASURED (build 14177, in-game): CreateMove fills input_history, but slot 6 rebuilds
            // it afterwards and this server config leaves it EMPTY - so by the time the command is
            // sent there are no entries to rewrite. The server then resolves the shot from the
            // command's own view angles, which is where the correction goes instead.
            //
            // [hist] PROBE (fast path unavailable): fires only while a shot is staged, rate-limited,
            // so an empty line sample still says which shot mechanism ran.
            std::byte* probeRep = nullptr;
            std::memcpy(&probeRep, cmdBytes + kInputHistoryRepOffset, sizeof(probeRep));
            int totalSize{};
            std::memcpy(&totalSize, cmdBytes + kInputHistoryTotalSizeOffset, sizeof(totalSize));
            int allocatedSize = -1;
            if (probeRep)
                std::memcpy(&allocatedSize, probeRep, sizeof(allocatedSize));
            VerifyConsole::write(1.0f, "hist", "empty path (base-angle fallback): current=%d total=%d rep=%p allocated=%d", size, totalSize, static_cast<void*>(probeRep), allocatedSize);

            return writeIntoBaseViewangles(localPawn, cmd, aimPitch, aimYaw, punchPitch, punchYaw, compensateSpread);
        }

        std::byte* rep = nullptr;
        std::memcpy(&rep, cmdBytes + kInputHistoryRepOffset, sizeof(rep));
        if (!rep)
            return writeIntoBaseViewangles(localPawn, cmd, aimPitch, aimYaw, punchPitch, punchYaw, compensateSpread);

        // Which entry does the attack reference? A non-negative index the game wrote itself wins;
        // otherwise the attack does not exist yet (or is ours about to be spliced) and will point
        // at the newest entry - correct that one.
        int attackIndex{};
        std::memcpy(&attackIndex, cmdBytes + cs2::CUserCmd::kAttack1StartHistoryIndexOffset, sizeof(attackIndex));
        const int index = (attackIndex >= 0 && attackIndex < size) ? attackIndex : size - 1;

        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + kRepElementsOffset + static_cast<std::ptrdiff_t>(index) * static_cast<std::ptrdiff_t>(sizeof(entry)), sizeof(entry));
        if (!entry)
            return !compensateSpread;

        std::byte* viewAngles = nullptr;
        std::memcpy(&viewAngles, entry + kHistoryViewAnglesOffset, sizeof(viewAngles));
        if (!viewAngles)
            return !compensateSpread;

        // SILENT-PATH: this is the branch where the shot is claimed through input_history
        // instead of the base viewangles - the silent mechanism. Fires whenever a shot is staged
        // on a server whose config keeps the entries alive past slot 6. Pair it with the [hist]
        // empty-path line to always know which shot mechanism ran.
        VerifyConsole::write(1.0f, "hist", "entry path: redirecting entry %d of %d (silent shot)", index, size);

        if (redirectedEntry)
            *redirectedEntry = index;

        return writeShotIntoEntry(entry, viewAngles, localPawn, aimPitch, aimYaw, punchPitch, punchYaw,
                                  backtrackSimTime, compensateSpread, backtracking, /*recycledEntry=*/false);
    }

private:
    // TRUE-SILENT FAST PATH (velocity's push_input_history fast branch, transplanted). On this
    // build slot 6 rebuilds input_history and leaves the command with current_size == 0 - but the
    // repeated field's backing storage is arena-allocated, so Clear() only reset the COUNT: the
    // rep block { allocated_size@0, elements@8 } and the entry submessages survive. So instead of
    // the visible base-angle redirect we RESURRECT entry 0, claim it with current_size = 1, and
    // write the shot into it - the camera never moves, and AttackCommand (which runs after the
    // aimbot's writer in the same hook) then points attack1_start_history_index at it (size-1 =
    // 0). The server resolves the shot along the entry's angles: fully silent.
    //
    // Clear() also wiped every has-bit, which would serialize the resurrected entry as empty
    // (unset floats parse as zeros server-side - a shot straight down +X), so the entry's
    // view_angles bit and the QAngle submessage's own pitch/yaw/roll bits are re-set here, and
    // roll is always written (writeShotIntoEntry forces it for recycled entries).
    //
    // Every step is null-guarded and falls back to the base-angle path - never crash on the
    // input path, never send a half-claimed entry. Returns whether the entry was claimed (false =
    // caller falls back to the base-angle path); `lands` out is whether the claimed shot carries a
    // real spread correction - false means the entry went out on the raw angle. `entryIndex` out
    // is the claimed entry's index (0) for the caller's attack1 marker write.
    [[nodiscard]] bool claimRecycledHistoryEntry(std::byte* cmdBytes, auto&& localPawn,
                                                 float aimPitch, float aimYaw,
                                                 float punchPitch, float punchYaw,
                                                 float backtrackSimTime, bool compensateSpread,
                                                 bool backtracking, bool& lands, int& entryIndex) const noexcept
    {
        std::byte* rep = nullptr;
        std::memcpy(&rep, cmdBytes + kInputHistoryRepOffset, sizeof(rep));
        if (!rep)
            return false;

        int allocatedSize{};
        std::memcpy(&allocatedSize, rep, sizeof(allocatedSize));
        if (allocatedSize <= 0)
            return false;

        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + kRepElementsOffset, sizeof(entry));
        if (!entry)
            return false;

        std::byte* viewAngles = nullptr;
        std::memcpy(&viewAngles, entry + kHistoryViewAnglesOffset, sizeof(viewAngles));
        if (!viewAngles)
            return false;

        // Claim the slot: current_size 0 -> 1 (velocity bumps only the count; total_size is
        // overhead accounting the serializer never reads).
        const int newSize = 1;
        std::memcpy(cmdBytes + cs2::CUserCmd::kInputHistorySizeOffset, &newSize, sizeof(newSize));

        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + kHasBitsOffset, sizeof(hasBits));
        hasBits |= kViewAnglesEntryHasBit;
        std::memcpy(entry + kHasBitsOffset, &hasBits, sizeof(hasBits));

        std::uint32_t vaHasBits{};
        std::memcpy(&vaHasBits, viewAngles + kSubMessageHasBitsOffset, sizeof(vaHasBits));
        vaHasBits |= kViewAnglesSubMessageHasBits;
        std::memcpy(viewAngles + kSubMessageHasBitsOffset, &vaHasBits, sizeof(vaHasBits));

        VerifyConsole::write(1.0f, "hist", "fast path: recycled entry 0 (fully silent shot)");

        entryIndex = 0;
        lands = writeShotIntoEntry(entry, viewAngles, localPawn, aimPitch, aimYaw, punchPitch, punchYaw,
                                   backtrackSimTime, compensateSpread, backtracking, /*recycledEntry=*/true);
        return true;
    }

    // The shot itself, shared by the live-entry path and the recycled fast path: corrected angles
    // minus the CURRENT aim punch, tick fields stamped so the server derives exactly the seed the
    // correction cancelled, interpolation windows zeroed when present. Returns whether the shot
    // lands on the intended angle (no compensation requested, or the solver produced a correction).
    [[nodiscard]] bool writeShotIntoEntry(std::byte* entry, std::byte* viewAngles, auto&& localPawn,
                            float aimPitch, float aimYaw, float punchPitch, float punchYaw,
                            float backtrackSimTime, bool compensateSpread, bool backtracking,
                            bool recycledEntry) const noexcept
    {
        // The tick this entry is made self-consistent with: the rewind tick when backtracking,
        // else the predicted server tick. velocity stamps their eye-derived sample tick here; ours
        // substitutes m_nTickBase - the same predicted server tick (documented deviation).
        int tickBase{};
        if (const auto baseTick = hookContext.localPlayerController().tickBase(); baseTick.hasValue())
            tickBase = baseTick.value();
        const int stampTick = backtracking
            ? static_cast<int>(backtrackSimTime / kTickInterval) + 1
            : tickBase;

        Optional<typename SpreadSolver<HookContext>::Angles> corrected;
        if (compensateSpread && stampTick > 0) {
            auto solver = hookContext.template make<SpreadSolver>();
            if (const auto params = solver.weaponParams(localPawn.getActiveWeapon()); params.hasValue())
                corrected = solver.findSpreadCorrection(typename SpreadSolver<HookContext>::Angles{aimPitch, aimYaw, 0.0f}, stampTick, params.value());
        }
        const bool lands = !compensateSpread || corrected.hasValue();

        // The engine adds the CURRENT aim punch on top of these angles when resolving the shot, so
        // subtracting it here makes the entry's direction the true aim (corrected when we have the
        // correction, the crosshair/redirect angle otherwise). Matches velocity's convention:
        // angles->set_x(aim_angle.x - aim_punch.x).
        const float pitch = (corrected.hasValue() ? corrected.value().pitch : aimPitch) - punchPitch;
        const float yaw = (corrected.hasValue() ? corrected.value().yaw : aimYaw) - punchYaw;

        std::memcpy(viewAngles + cs2::CUserCmd::BaseMessage::ViewAngles::kPitchOffset, &pitch, sizeof(pitch));
        std::memcpy(viewAngles + cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, &yaw, sizeof(yaw));

        // Roll exists to rotate the spread offset onto the aim (see SpreadSolver); leave whatever
        // roll the game wrote alone unless we are actually correcting (velocity writes z only for
        // no_spread, for the same reason). A recycled entry is the exception: its QAngle was
        // cleared, and we re-claimed the roll has-bit above, so a value must go out - the cleared
        // 0.0f unless the solver produced one.
        if (corrected.hasValue() || recycledEntry) {
            const float roll = corrected.hasValue() ? corrected.value().roll : 0.0f;
            std::memcpy(viewAngles + cs2::CUserCmd::BaseMessage::ViewAngles::kRollOffset, &roll, sizeof(roll));
        }

        // Stamp the tick fields so the entry carries exactly the timestamp our correction assumed.
        // Unstamped (game-filled) values would let the server derive a different seed than the one
        // the correction cancelled - a systematic miss whenever they drift apart.
        if (backtracking) {
            stampBacktrackTick(entry, backtrackSimTime);
        } else if (stampTick > 0) {
            stampLiveTick(entry, stampTick);
        }

        zeroInterpolationInfo(entry);
        return lands;
    }

    // NO-HISTORY FALLBACK (the path BOTH consumers actually take on this build: the triggerbot at
    // slot 7 and now the rage aimbot). Writes the aim angle into the command's BASE view angles -
    // the field the server falls back to when the command carries no input_history, and one of the
    // two things move_crc covers, so the WriteMoveCrc recomputation after us keeps the command
    // valid.
    //
    // The ANGLE WRITE is unconditional (it IS the redirect - without it a rage shot goes wherever
    // the camera points); compensateSpread only gates the cone-cancellation solver on top.
    //
    // Same math as the entry path: seed for (rounded angles, predicted server tick), correction
    // that cancels the cone onto the aim, minus the CURRENT aim punch (the engine adds punch on
    // top of these angles when resolving the shot). Roll rotates the spread offset onto the aim -
    // the fire basis honours roll even though the forward vector ignores it, which is the whole
    // trick (see SpreadSolver).
    //
    // Unlike a history-entry rewrite this is NOT fully silent: base view angles are also what the
    // local view is built from, so the camera shows the corrected angle for this command - a
    // sub-degree nudge standing, up to a few degrees mid-spray on a running Deagle. During a held
    // spray the punch subtraction doubles as recoil control: the view pulls against the kick to
    // keep bullets on the original aim point.
    [[nodiscard]] bool writeIntoBaseViewangles(auto&& localPawn, cs2::CUserCmd* cmd, float aimPitch, float aimYaw,
                                               float punchPitch, float punchYaw, bool compensateSpread) const noexcept
    {
        int tickBase{};
        if (const auto baseTick = hookContext.localPlayerController().tickBase(); baseTick.hasValue())
            tickBase = baseTick.value();

        Optional<typename SpreadSolver<HookContext>::Angles> corrected;
        if (compensateSpread && tickBase > 0) {
            auto solver = hookContext.template make<SpreadSolver>();
            if (const auto params = solver.weaponParams(localPawn.getActiveWeapon()); params.hasValue())
                corrected = solver.findSpreadCorrection(typename SpreadSolver<HookContext>::Angles{aimPitch, aimYaw, 0.0f}, tickBase, params.value());
        }
        const bool lands = !compensateSpread || corrected.hasValue();
        const float pitch = (corrected.hasValue() ? corrected.value().pitch : aimPitch) - punchPitch;
        const float yaw = (corrected.hasValue() ? corrected.value().yaw : aimYaw) - punchYaw;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return lands;
        userCmd.setViewAngles(pitch, yaw);

        // NO roll write in the base-angle path, deliberately. The solver's roll exists to rotate
        // the spread-offset basis inside a history ENTRY (invisible to the camera); written into
        // BASE view angles it rolls the actual camera - measured in-game as the whole screen
        // tilting and oscillating left/right while firing. Pitch/yaw alone still carry the
        // correction; the residual roll-axis error is a few-hundredths-of-a-degree cone sliver.
        return lands;
    }

    // Live shot: the bullet spawns this predicted server tick. render_tick gets tick+1 with a
    // zero fraction - the same shape the game's own fill produces for the current sample and the
    // same convention as the backtrack stamp below.
    static void stampLiveTick(std::byte* entry, int tick) noexcept
    {
        setTickFields(entry, /*renderTick=*/tick + 1, 0.0f, /*playerTick=*/tick, 0.0f);
    }

    // Backtrack shot: rewinds BOTH timestamps to the record's server tick so the server lag-comp
    // interpolates the enemy there (simTime stamped as tick+1 like velocity).
    static void stampBacktrackTick(std::byte* entry, float simulationTime) noexcept
    {
        const float t = simulationTime / kTickInterval;
        const int tick = static_cast<int>(t);
        const float fraction = t - static_cast<float>(tick);
        const int tickPlusOne = tick + 1;
        setTickFields(entry, tickPlusOne, 0.0f, tickPlusOne, fraction);
    }

    static void setTickFields(std::byte* entry, int renderTick, float renderFrac,
                              int playerTick, float playerFrac) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + kHasBitsOffset, sizeof(hasBits));
        hasBits |= kTickHasBits;
        std::memcpy(entry + kHasBitsOffset, &hasBits, sizeof(hasBits));

        std::memcpy(entry + kRenderTickCountOffset, &renderTick, sizeof(renderTick));
        std::memcpy(entry + kRenderTickFractionOffset, &renderFrac, sizeof(renderFrac));
        std::memcpy(entry + kPlayerTickCountOffset, &playerTick, sizeof(playerTick));
        std::memcpy(entry + kPlayerTickFractionOffset, &playerFrac, sizeof(playerFrac));
    }

    // Zeroes the interpolation-info sub-messages the way velocity-cs2's fire_gun does. Present-only:
    // a message field the game left unset stays unset (an absent sub-message reads as defaults
    // server-side, which is exactly what the reference's has_sv_interp0() guard leaves behind).
    static void zeroInterpolationInfo(std::byte* entry) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + kHasBitsOffset, sizeof(hasBits));

        if (hasBits & kClInterpEntryHasBit)
            zeroClInterp(entry + kClInterpPtrOffset);
        if (hasBits & kSvInterp0EntryHasBit)
            zeroSvInterp(entry + kSvInterp0PtrOffset);
        if (hasBits & kSvInterp1EntryHasBit)
            zeroSvInterp(entry + kSvInterp1PtrOffset);
    }

    // CSGOInterpolationInfoPB_CL { float frac = 1; }
    static void zeroClInterp(std::byte* ptrField) noexcept
    {
        std::byte* interp = loadPointer(ptrField);
        if (!interp)
            return;
        setSubMessageFloat(interp, kInterpFracOffset, kInterpFracHasBit, 0.0f);
    }

    // CSGOInterpolationInfoPB { float frac = 1; int32 src_tick = 2; int32 dst_tick = 3; }
    // src/dst -1 ends any interpolation window straddling our rewrite (velocity: set_src_tick(-1),
    // set_dst_tick(-1), set_frac(0)).
    static void zeroSvInterp(std::byte* ptrField) noexcept
    {
        std::byte* interp = loadPointer(ptrField);
        if (!interp)
            return;
        setSubMessageFloat(interp, kInterpFracOffset, kInterpFracHasBit, 0.0f);
        setSubMessageInt(interp, kInterpSrcTickOffset, kInterpSrcTickHasBit, -1);
        setSubMessageInt(interp, kInterpDstTickOffset, kInterpDstTickHasBit, -1);
    }

    [[nodiscard]] static std::byte* loadPointer(std::byte* at) noexcept
    {
        std::byte* pointer = nullptr;
        std::memcpy(&pointer, at, sizeof(pointer));
        return pointer;
    }

    // Every generated protobuf message shares the header layout the entry constants above were
    // derived from: _has_bits_ at +0x10, first field at +0x18.
    static void setSubMessageFloat(std::byte* msg, int offset, std::uint32_t hasBit, float value) noexcept
    {
        std::memcpy(msg + offset, &value, sizeof(value));
        orSubMessageHasBits(msg, hasBit);
    }

    static void setSubMessageInt(std::byte* msg, int offset, std::uint32_t hasBit, std::int32_t value) noexcept
    {
        std::memcpy(msg + offset, &value, sizeof(value));
        orSubMessageHasBits(msg, hasBit);
    }

    static void orSubMessageHasBits(std::byte* msg, std::uint32_t bits) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, msg + kSubMessageHasBitsOffset, sizeof(hasBits));
        hasBits |= bits;
        std::memcpy(msg + kSubMessageHasBitsOffset, &hasBits, sizeof(hasBits));
    }

    // ---- CSGOUserCmdPB (the command wrapper) ----
    //
    // input_history is a protobuf repeated field on the command: Rep* at cmd+56, element pointers
    // at Rep+8. Each element is a CSGOInputHistoryEntryPB.
    static constexpr std::ptrdiff_t kInputHistoryRepOffset = 56;
    static constexpr std::ptrdiff_t kInputHistoryTotalSizeOffset = 52; // repeated_ptr_field total_size (probe only)
    static constexpr std::ptrdiff_t kRepElementsOffset = 8;

    // ---- CSGOInputHistoryEntryPB (one history element) ----
    //
    // Field offsets and their has-bits, derived from build 14177 libclient.so rather than assumed:
    //   * the per-entry fill fn (called once per entry by CreateMove, slot 26) writes
    //     render_tick_count @ +96 (has-bit 0x200), render_tick_fraction @ +100 (0x400),
    //     player_tick_count @ +104 (0x800), player_tick_fraction @ +108 (0x1000) -
    //     proving has-bits are assigned LSB-first in field-declaration order, and correcting the
    //     earlier belief that player_tick_count owned 0x400 (that is render_tick_FRACTION).
    //   * the deleting destructor releases nine consecutive message pointers starting at +0x18,
    //     which fixes the message-field offsets in declaration order:
    //     view_angles +0x18, cl_interp +0x20, sv_interp0 +0x28, sv_interp1 +0x30,
    //     player_interp +0x38, shoot_position +0x40, target_head_pos_check +0x48,
    //     target_abs_pos_check +0x50, target_abs_ang_check +0x58 - so the three fields we zero
    //     carry entry has-bits 0x2 / 0x4 / 0x8 (view_angles' 0x1 is proven by the fill fn).
    //   _has_bits_ sits at +16.
    static constexpr std::ptrdiff_t kHistoryViewAnglesOffset = 0x18;
    static constexpr std::ptrdiff_t kClInterpPtrOffset = 0x20;
    static constexpr std::ptrdiff_t kSvInterp0PtrOffset = 0x28;
    static constexpr std::ptrdiff_t kSvInterp1PtrOffset = 0x30;

    static constexpr std::uint32_t kViewAnglesEntryHasBit = 0x1;
    static constexpr std::uint32_t kClInterpEntryHasBit = 0x2;
    static constexpr std::uint32_t kSvInterp0EntryHasBit = 0x4;
    static constexpr std::uint32_t kSvInterp1EntryHasBit = 0x8;

    static constexpr std::ptrdiff_t kHasBitsOffset = 16;
    static constexpr std::uint32_t kRenderTickCountHasBit = 0x200;
    static constexpr std::uint32_t kRenderTickFractionHasBit = 0x400;
    static constexpr std::uint32_t kPlayerTickCountHasBit = 0x800;
    static constexpr std::uint32_t kPlayerTickFractionHasBit = 0x1000;
    static constexpr std::uint32_t kTickHasBits = kRenderTickCountHasBit | kRenderTickFractionHasBit
        | kPlayerTickCountHasBit | kPlayerTickFractionHasBit;

    static constexpr std::ptrdiff_t kRenderTickCountOffset = 96;
    static constexpr std::ptrdiff_t kRenderTickFractionOffset = 100;
    static constexpr std::ptrdiff_t kPlayerTickCountOffset = 104;
    static constexpr std::ptrdiff_t kPlayerTickFractionOffset = 108;

    // ---- the interpolation-info sub-messages ----
    static constexpr std::ptrdiff_t kSubMessageHasBitsOffset = 0x10;
    static constexpr std::ptrdiff_t kInterpFracOffset = 0x18;
    static constexpr std::ptrdiff_t kInterpSrcTickOffset = 0x1c;
    static constexpr std::ptrdiff_t kInterpDstTickOffset = 0x20;
    static constexpr std::uint32_t kInterpFracHasBit = 0x1;
    static constexpr std::uint32_t kInterpSrcTickHasBit = 0x2;
    static constexpr std::uint32_t kInterpDstTickHasBit = 0x4;

    // CMsgQAngle { float pitch; float yaw; float roll; } - one has-bit per declared field,
    // LSB-first like every message here. All three are re-claimed on a recycled entry.
    static constexpr std::uint32_t kViewAnglesSubMessageHasBits = 0x1 | 0x2 | 0x4;

    static constexpr float kTickInterval = 0.015625f; // 1/64

    HookContext& hookContext;
};
