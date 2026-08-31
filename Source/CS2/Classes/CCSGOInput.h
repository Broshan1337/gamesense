#pragma once

#include <cstdint>

namespace cs2
{

// The client's user-command message. Opaque here: CS2's user command is a protobuf
// (CSGOUserCmdPB wrapping CBaseUserCmdPB), so its fields are reached through offsets worked out
// separately rather than by declaring the layout.
struct CUserCmd;

// CS2's input system. A single process-lifetime object - and notably a STATIC GLOBAL OBJECT rather
// than a pointer: the client constructs it in place at a fixed address in libclient.so (see
// CSGOInputPointer for how that address is recovered), so unlike CViewRender there is no
// pointer-to-pointer to track across level loads.
//
// RE trail (libclient.so):
//  - `_ZTI10CCSGOInput` @ 0x43e7ad0, found from its typeinfo-name string "10CCSGOInput".
//  - vtable `_ZTV10CCSGOInput` @ 0x43ed1e8, so slot N sits at 0x43ed1f8 + 8*N. Slot 0 holds the
//    destructor sub_1A68730, which confirms the base is measured from the right place.
//  - The instance is constructed at 0x4818680 by sub_1A7D3D0.
struct CCSGOInput {
    // Builds the movement command for a frame. Signature confirmed from the prologue of
    // sub_1ABA9F0 (rdi=this, esi=slot, rdx=cmd), which is also the function containing the
    // "cl: CreateMove - Frame %d, cmd %d, ..." log string - the landmark used to find it.
    using CreateMove = void(CCSGOInput* thisptr, int slot, CUserCmd* cmd);

    // Vtable slot of CreateMove. Derived as (0x43ed2c8 - 0x43ed1f8) / 8 = 26, where 0x43ed2c8 is
    // where sub_1ABA9F0's address is stored inside the vtable.
    //
    // A Windows reference would call this slot 25: the Itanium ABI emits two destructor slots
    // where MSVC emits one, so Windows slot N is Linux slot N+1 - the same shift already
    // documented for CGameEvent.
    static constexpr int kCreateMoveVtableSlot = 26;

    // Slot 6 (sub_18CD5E0) is the function that actually FILLS the command, and it is the ONLY
    // writer of the movement fields anywhere in libclient. It drains the queued input samples into
    // its own movement members and copies those into the command's protobuf - and, critically, it
    // also emits the per-subtick analog deltas from the same source. Editing the finished protobuf
    // reproduces only the first of those; steering from the input side reproduces all of them,
    // because the game's own code does the work.
    //
    // Takes no command parameter (it loads the command from a global) and returns immediately
    // unless the slot argument is 0.
    static constexpr int kBuildUserCmdVtableSlot = 6;
    using BuildUserCmd = std::uint64_t(CCSGOInput* thisptr, int slot, int frameNumber);

    // Slot 7 (sub_1ABA600) is handed the command and copies its three button words into the
    // CInButtonStatePB submessage - `buttons_pb`, field 3 - then serialises {viewangles, buttons}
    // into the `move_crc` bytes field.
    //
    // This is the only place we can legitimately reach the command outside CreateMove: slot 6 gets
    // its command from `sub_15DAC30(localObject, tick)`, a call rather than a global, so there is
    // no pointer to resolve there. Pressing a button in a slot 7 PRE-hook lands before both the
    // buttons_pb copy and the checksum that covers it.
    static constexpr int kWriteMoveCrcVtableSlot = 7;
    using WriteMoveCrc = std::uint64_t(CCSGOInput* thisptr, CUserCmd* cmd);

    // The movement members slot 6 copies into the command's protobuf, read at 0x18cd866 as three
    // consecutive floats on `this`.
    static constexpr int kForwardMoveOffset = 0x270;
    static constexpr int kLeftMoveOffset = 0x274;
    static constexpr int kUpMoveOffset = 0x278;

    // Accumulated button state, two 64-bit words. sub_18A8F40 ORs each input sample's buttons in
    // here, sub_18A93F0 clears both (`*(OWORD*)(this+608) = 0`) at the start of a frame, and
    // sub_18AAA20 tests them (`and rax, [this+260h]`) - so this is the live "what is held down".
    //
    // Analog movement alone was verified NOT to move the player even when the game itself built the
    // whole command from it, so the button that normally accompanies it is the missing half.
    static constexpr int kButtonStateOffset = 0x260;

    // Button masks, read out of the game's own name/mask table rather than assumed from Source
    // convention: 0x20-byte entries of {const char* name, uint64 mask}, with "IN_FORWARD" at
    // 0x430c2a0 -> 0x8, "IN_MOVELEFT" at 0x430c340 -> 0x200 and "IN_MOVERIGHT" at 0x430c360 ->
    // 0x400.
    struct Buttons {
        // "IN_ATTACK" at 0x430c240 -> 0x1. Read out of the table rather than assumed from Source
        // convention, and that was not paranoia: the masks are NOT one bit per entry. 0x40 is
        // skipped entirely between the 0x20 entry and the 0x80 one, which is why IN_MOVELEFT lands
        // on 0x200 only five entries after IN_FORWARD's 0x8. Counting bits off a neighbour would
        // have produced the wrong mask for anything past IN_DUCK.
        static constexpr std::uint64_t kAttack = 0x1;
        static constexpr std::uint64_t kJump = 0x2;
        static constexpr std::uint64_t kForward = 0x8;
        static constexpr std::uint64_t kBack = 0x10;
        static constexpr std::uint64_t kMoveLeft = 0x200;
        static constexpr std::uint64_t kMoveRight = 0x400;
    };

    // Raw (punch-free) view angles on the input object, FrameworkCS2-derived @0x7C0 ("found
    // inside the sync view angles function") and NOT re-verified on this build. Consumers
    // (third person, view punch removal) sanity-check the values every frame and fail closed
    // when they are out of an angle's valid range instead of steering the camera into garbage.
    static constexpr int kViewAnglesOffset = 0x7C0;

    // The queue of raw input samples that feeds those members. sub_18A8F40, called from the top of
    // slot 6, walks it and does `*(qword*)(this+624) = *(qword*)(sample+24)` - assignment, not
    // accumulation, so the LAST consumed sample wins - plus `*(dword*)(this+632) =
    // *(dword*)(sample+32)` for upmove.
    //
    // Writing here rather than into the finished command is the whole point: a sample edited before
    // slot 6 runs propagates into the movement members, the protobuf fields AND the subtick analog
    // deltas, all by the game's own code.
    struct InputSampleQueue {
        static constexpr int kCountOffset = 2896;
        static constexpr int kBufferOffset = 2904;
        static constexpr int kSampleStride = 1088;

        static constexpr int kForwardMoveOffset = 24;
        static constexpr int kLeftMoveOffset = 28;

        // The sample's own button word, OR-ed into this+0x260 by the same loop:
        // `*(qword*)(this+608) |= *(qword*)(sample+8)`.
        static constexpr int kButtonStateOffset = 8;

        // Guards against a corrupt or uninitialised count being taken at face value and walking the
        // process off a cliff. The queue holds one entry per input event in a frame; anything near
        // this is already far past plausible.
        static constexpr int kMaxPlausibleCount = 256;
    };
};

}
