#pragma once

#include <cstdint>

namespace cs2
{

// One entry of CBaseUserCmdPB's `subtick_moves` (field 18). The message is, from the descriptor:
//
//   button = 1 (uint64), pressed = 2 (bool), when = 3 (float),
//   analog_forward_delta = 4, analog_left_delta = 5, pitch_delta = 8, yaw_delta = 9
//
// Seven fields, so has-bits 0..6. Every offset below is taken from CCSGOInput slot 6 building these
// for real input, not from guessing the layout:
//   movss [rax+24h] / or 4      -> when
//   movss [rax+28h] / or 8      -> analog_forward_delta
//   movss [rax+2Ch] / or 10h    -> analog_left_delta
//   mov   [rax+18h], rdi        -> button      (has-bits |= 5 alongside `when`)
//   mov   [rax+20h], dil        -> pressed     (has-bits |= 7)
//   movss [rax+30h] / or 20h    -> pitch_delta
//   movss [rax+34h] / or 40h    -> yaw_delta
struct CSubtickMoveStep {
    static constexpr int kHasBitsOffset = 0x10;

    static constexpr int kButtonOffset = 0x18;
    static constexpr int kPressedOffset = 0x20;
    static constexpr int kWhenOffset = 0x24;
    static constexpr int kAnalogForwardDeltaOffset = 0x28;
    static constexpr int kAnalogLeftDeltaOffset = 0x2C;
    // View-angle adjustments, confirmed the same way as everything above:
    //   movss [rax+30h] / or 20h   -> pitch_delta
    //   movss [rax+34h] / or 40h   -> yaw_delta
    static constexpr int kPitchDeltaOffset = 0x30;
    static constexpr int kYawDeltaOffset = 0x34;

    static constexpr std::uint32_t kButtonHasBit = 0x1;
    static constexpr std::uint32_t kPressedHasBit = 0x2;
    static constexpr std::uint32_t kWhenHasBit = 0x4;
    static constexpr std::uint32_t kAnalogForwardDeltaHasBit = 0x8;
    static constexpr std::uint32_t kAnalogLeftDeltaHasBit = 0x10;
    static constexpr std::uint32_t kPitchDeltaHasBit = 0x20;
    static constexpr std::uint32_t kYawDeltaHasBit = 0x40;
};

// CS2's user command. Its payload is a protobuf (CSGOUserCmdPB wrapping CBaseUserCmdPB), so rather
// than declaring the generated layout this records only the offsets actually needed, all recovered
// by reverse engineering.
//
// Source of the layout: sub_18A0620 in libclient.so, which CreateMove calls as (this, slot, cmd).
// That function is the game's own `cl_` square-walk debug feature - it prints
// "CL:  %d commands per side at speed %f" and "CL:  turning to %s after %d steps" - and it writes
// movement and view angles into the command itself, which is exactly the operation we need.
//
// Protobuf semantics that matter: a field is only sent if its HAS-BIT is set. Writing the value
// alone is not enough; the corresponding bit in the message's has-bits word must be set too.
struct CUserCmd {
    // Native command sequence (legacy_command_number in the protobuf stays zero).
    static constexpr int kCommandNumberOffset = 8;

    // Pointer to the CBaseUserCmdPB. Lazily created by the game, but anything running AFTER the
    // original CreateMove will always find it already allocated - so the correct handling of a null
    // here is to do nothing, not to reimplement the game's allocation path.
    static constexpr int kBaseMessageOffset = 64;

    // Has-bits word of the OUTER message; the game sets bit 0 before touching the base message.
    static constexpr int kOuterHasBitsOffset = 32;
    static constexpr std::uint32_t kBaseMessageHasBit = 1;

    // The BUTTON words, three 64-bit masks living on the command itself rather than inside the
    // protobuf - the same `cmd->buttons.buttonstate1` the reference implementation writes.
    //
    // Both ends of their journey are confirmed. Slot 6 reads them (`cmp qword ptr [r13+60h], 0` at
    // 0x18cda31, and the +0x68/+0x70 pair beside it) and takes a different path when they are set,
    // which is where the subtick button events come from. Slot 7 (sub_1ABA600) then copies all
    // three into CInButtonStatePB - `*(v8+24) = *(a2+96)`, `*(v10+32) = *(a2+104)`,
    // `*(v12+40) = *(a2+112)` with has-bits 1/2/4 - which is the message the descriptor calls
    // `buttons_pb`, field 3.
    //
    // Nothing writes them from CCSGOInput's own accumulator at input+0x260, which is why pressing a
    // button there did not reach the command.
    static constexpr int kButtonState1Offset = 96;
    static constexpr int kButtonState2Offset = 104;
    static constexpr int kButtonState3Offset = 112;

    // The rest of the OUTER CSGOUserCmdPB, read straight out of the game's own
    // CSGOUserCmdPB::Clear() (vtable slot 4, sub_13F70E0):
    //
    //   result = *(int*)(pb + 32);  if > 0 { v3 = *(qword*)(pb + 40); ... }  -> input_history
    //   v6 = *(dword*)(pb + 16);                                             -> has-bits
    //   if (v6 & 1)   clear *(qword*)(pb + 48)                               -> base, has-bit 0x1
    //   *(dword*)(pb + 56) = 0                                               -> the four bools
    //   if (v6 & 0x60) *(qword*)(pb + 60) = -1        -> attack1 AND attack2, has-bits 0x20 / 0x40
    //
    // Those are offsets into the protobuf, which sits at +16 inside the command - which is exactly
    // why this file's already-verified constants are 16 higher (has-bits 16 -> 32, base 48 -> 64).
    // Everything below is therefore the Clear() offset plus 16.
    //
    // `attack1_start_history_index` is the field that makes a shot real. Its declared default is
    // **-1**, meaning "no attack began in this command", and the server believes it: set the attack
    // button and the subtick steps without it and the client happily predicts a shot the server
    // never fires. That was the entire "fake bullets" bug.
    static constexpr int kInputHistorySizeOffset = 48;
    static constexpr int kAttack1StartHistoryIndexOffset = 76;
    static constexpr std::uint32_t kAttack1StartHistoryIndexHasBit = 0x20;

    // "24CFixedSizeCircularBufferI23CSGOInputHistoryEntryPBLi32EiE" in the binary - the history is a
    // fixed 32-entry buffer, so a size outside 1..32 means the layout above is not what we think it
    // is and nothing should be written.
    static constexpr int kMaxInputHistoryEntries = 32;

    // The wire-side input_history repeated field of CSGOUserCmdPB - the entries the SERVER parses.
    // This is NOT the local circular buffer above; the circular buffer mirrors what this protobuf
    // field carried last command.
    //
    // Layout reconciles every constant this tree already trusted: kInputHistorySizeOffset (the
    // current element count) sits at +48 and SubtickShotWriter probes a total_size at +52 with the
    // Rep* at +56 - which is exactly a RepeatedPtrFieldBase whose head lives at cmd+40:
    //   arena @ +0 (=cmd+40), current_size @ +8 (=cmd+48), total_size @ +12 (=cmd+52),
    //   Rep*   @ +16 (=cmd+56), and Rep holds {int32 allocated_size, void* elements[]}
    // like the BaseMessage::SubtickMoves field above (same embedded protobuf generation). Anchors
    // below are absolute; *_FIELD-relative mirrors exist next to their users in GameClient/InputHistory.h.
    struct InputHistory {
        static constexpr int kFieldOffset = 40;
        static constexpr int kCurrentSizeOffset = 48; // == kInputHistorySizeOffset, field-relative +8
        static constexpr int kTotalSizeOffset = 52;   // field-relative +12 (probe-only per SubtickShotWriter)
        static constexpr int kRepOffset = 56;         // == documented kInputHistoryRepOffset, field-relative +16
        static constexpr int kRepAllocatedSizeOffset = 0; // identical shape to SubtickMoves' Rep
        static constexpr int kRepElementsOffset = 8;

        // One entry (CSGOInputHistoryEntryPB) - offsets proven in SubtickShotWriter.h's
        // build-14177 RE notes and reused here so there is exactly one source of truth per fact:
        // _has_bits_ at +0x10 LSB-first in declaration order (view_angles owns 0x1), the
        // view_angles CMsgQAngle sub-message at +0x18, render_tick_count at +0x60,
        // render_tick_fraction at +0x64.
        static constexpr std::ptrdiff_t kEntryHasBitsOffset = 0x10;
        static constexpr std::ptrdiff_t kEntryViewAnglesOffset = 0x18;
        static constexpr std::ptrdiff_t kEntryRenderTickCountOffset = 0x60;
        static constexpr std::ptrdiff_t kEntryRenderTickFractionOffset = 0x64;
        static constexpr std::uint32_t kEntryViewAnglesHasBit = 0x0001;
        static constexpr std::uint32_t kEntryRenderTickCountHasBit = 0x0200;
        static constexpr std::uint32_t kEntryRenderTickFractionHasBit = 0x0400;
        static constexpr std::uint32_t kEntryPlayerTickCountHasBit = 0x0800;
        static constexpr std::uint32_t kEntryPlayerTickFractionHasBit = 0x1000;
        // What the reference emitter stamps onto fresh entries: VIEW_ANGLES plus the four tick-
        // context fields it fills alongside (see Features/Game/FvaEmulator.h).
        //
        // Build-14178 verification (the entries here are the 0x78-byte class, confirmed by its
        // Clear() reaching +0x74): the scalar block spanning +0x60..+0x77 is cleared under the
        // has-bit mask 0x7e00 - six scalar fields where build 14177 had four (0x200..0x1000).
        // Two fields were therefore APPENDED (the new 0x2000/0x4000 at +0x70 and the -1-default
        // at +0x74), which keeps the original render_tick_count @ +0x60 and render_tick_fraction
        // @ +0x64 exactly where the 14177 RE put them. Appended-not-inserted is also what the
        // field-declaration-order rule predicts.
        static constexpr std::uint32_t kEmittedEntryHasBits =
            kEntryViewAnglesHasBit | kEntryRenderTickCountHasBit | kEntryRenderTickFractionHasBit
            | kEntryPlayerTickCountHasBit | kEntryPlayerTickFractionHasBit;
    };

    struct BaseMessage {
        // The buttons sub-message, and the ONLY copy of the button state the server ever sees.
        //
        // Critically, it is slot 6 (BuildUserCmd) that fills this from the command's raw button
        // words - `mov rdx,[r13+60h] / mov [rcx+18h],rdx` and the same for +0x68 -> +0x20 and
        // +0x70 -> +0x28, each has-bit set only when the word is non-zero. Slot 7 does NOT: it
        // reads those same raw words into a throwaway message purely to checksum them into
        // move_crc.
        //
        // That ordering is what defeated the first four triggerbot attempts. Writing a raw button
        // word from a slot 7 hook is too late - slot 6 has already copied, so buttons_pb never
        // learns about it. The client still predicts from the raw word, which is exactly why a shot
        // animated locally and never existed server-side. Anything that must reach the server has
        // to be written HERE, into buttons_pb itself.
        static constexpr int kButtonsPbOffset = 0x38;
        static constexpr std::uint32_t kButtonsPbHasBit = 0x2;

        struct ButtonsPb {
            static constexpr int kHasBitsOffset = 0x10;
            static constexpr int kButtonState1Offset = 0x18;
            static constexpr int kButtonState2Offset = 0x20;
            static constexpr int kButtonState3Offset = 0x28;

            static constexpr std::uint32_t kButtonState1HasBit = 0x1;
            static constexpr std::uint32_t kButtonState2HasBit = 0x2;
            static constexpr std::uint32_t kButtonState3HasBit = 0x4;
        };

        static constexpr int kHasBitsOffset = 16;

        // `subtick_moves`, a protobuf RepeatedPtrFieldBase living at base+0x18. Slot 6 both walks it
        // inline and grows it, which pins the whole layout:
        //   mov rdi, [r14+18h] / call <new CSubtickMoveStep> / lea rdi, [r14+18h] / call <add>
        //   movsxd rax, [r14+20h]  /  cmp eax, [rdx]  /  mov rax, [rdx+rax*8+8]
        // giving the standard { Arena* arena; int current_size; int total_size; Rep* rep; } with the
        // Rep's allocated count first and its element pointers starting 8 bytes in.
        struct SubtickMoves {
            static constexpr int kFieldOffset = 0x18;

            static constexpr int kArenaOffset = 0;
            static constexpr int kCurrentSizeOffset = 8;
            static constexpr int kRepOffset = 16;

            static constexpr int kRepAllocatedSizeOffset = 0;
            static constexpr int kRepElementsOffset = 8;

            // The game refuses to add past this and logs "Client reached the maximum number of
            // sub-tick moves this tick" (the `n32 == 32` test in sub_18A8F40), so neither do we.
            static constexpr int kMaxSteps = 32;
        };

        // The movement triple, CONFIRMED against the game's own writer rather than inferred.
        // CCSGOInput vtable slot 6 (sub_18CD5E0) copies its three member floats into exactly these
        // offsets with exactly these has-bits:
        //
        //   movss xmm2, [this+270h] -> or [base+10h], 40h  / movss [base+58h], xmm2   forwardmove
        //   movss xmm1, [this+274h] -> or [base+10h], 80h  / movss [base+5Ch], xmm1   leftmove
        //   movss xmm0, [this+278h] -> or [base+10h], 100h / movss [base+60h], xmm0   upmove
        //
        // These are NOT legacy fields - an earlier reading of the descriptor concluded they were,
        // which the writer above disproves outright.
        //
        // Scale: normalised, not CS:GO speed units. The square-walk debug feature substitutes 1.0f
        // when its speed cvar is zero, and the working reference implementation uses +-1.0f.
        //
        // Note the game's idiom when a value is exactly 0.0f: it CLEARS the has-bit and writes 0,
        // rather than sending a zero. Not writing at all is the equivalent, and is what we do.
        static constexpr int kForwardMoveOffset = 88;
        static constexpr int kLeftMoveOffset = 92;
        static constexpr int kUpMoveOffset = 96;

        // Raw mouse movement for this tick, straight from CCSGOInput's own counter
        // (`movsx r14d, word ptr [this+27Ch]` -> `mov [base+70h], r14d` with has-bit 0x1000).
        //
        // Preferred over differencing the view yaw across ticks for the autostrafe: this is the
        // actual input for THIS command, where a yaw difference also picks up recoil control,
        // subtick angle adjustments and anything else that moved the view.
        static constexpr int kMouseDxOffset = 112;

        // The per-command RNG seed (CBaseUserCmdPB.random_seed). Cross-validated port from the
        // Windows-layout RE in FORFUTURETESTS/mytest (+0x6C there): every anchor overlapping this
        // tree's own Linux-derived constants matches byte-for-byte - buttons_pb@0x38,
        // viewangles@64, forwardmove/leftmove/upmove@88/92/96, mousedx@112 - which pins protobuf's
        // generated member order as compiler-independent, making 108 (=0x6C) trustworthy here.
        // LSB-first has-bits run in field-declaration order (SubtickShotWriter's proof): buttons_pb
        // owns 0x2 and viewangles 0x4 around it, so the RNG seed slot is 0x800 on this build too.
        static constexpr int kRandomSeedOffset = 108;
        static constexpr std::uint32_t kRandomSeedHasBit = 0x800;

        static constexpr std::uint32_t kForwardMoveHasBit = 0x40;
        static constexpr std::uint32_t kLeftMoveHasBit = 0x80;
        static constexpr std::uint32_t kUpMoveHasBit = 0x100;

        // CMsgQAngle submessage holding the view angles the command will be sent with.
        static constexpr int kViewAnglesOffset = 64;
        static constexpr std::uint32_t kViewAnglesHasBit = 0x4;

        struct ViewAngles {
            // Proved by the same debug feature, which writes 180.0f / 90.0f / -90.0f into the yaw
            // slot for its WEST / NORTH / SOUTH turns.
            static constexpr int kPitchOffset = 24;
            static constexpr int kYawOffset = 28;
            static constexpr int kRollOffset = 32;
        };
    };
};

}
