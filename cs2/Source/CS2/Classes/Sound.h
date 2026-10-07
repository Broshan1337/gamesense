#pragma once

#include "CUtlFilenameSymbolTable.h"
#include "CUtlVector.h"
#include "Vector.h"

#include <cstddef>

#include <Utils/Pad.h>

namespace cs2
{

struct CSfxTable {
    PAD(40); // TODO: get dynamically, was broken: 2024.05.23
    FileNameHandle_t fileNameHandle;
};

struct ChannelInfo1 {
    CSfxTable* sfx;
    int guid;
    PAD(52); // TODO: get sizeof dynamically, was broken: 2024.02.07, broken again (reverted to previous) 2024.05.23, broken again 2025.07.29
};

static_assert(sizeof(ChannelInfo1) == 64);

// 2026-10-07 (1.41.8.9): the second channel-info entry shrank from the long-standing
// {Vector origin; PAD(120)} (132 bytes) to a 16-byte record: the origin followed by the
// channel guid mirrored at +0xC. Verified in libsoundsystem.so (Oct 6 02:22 build) at two
// independent consumers, both indexing the second array from the same channel index as the
// first: `shl $0x4` stride, memory pointer loaded from container+0x88, origin.x read at
// entry+0x00 (movss) and guid low-24-bits compared against entry+0x0C.
struct ChannelInfo2 {
    Vector origin;
    int guid;
};

static_assert(sizeof(ChannelInfo2) == 16);

struct SoundChannels {
    // size@+0x00, memory@+0x08 (verified: `cmp (%rdx),%eax` bound, `shl $0x6` stride,
    // `add 0x8(%rdx),%rax` base).
    CUtlVector<ChannelInfo1> channelInfo1;
    // +0x18..+0x7F: fields we don't model. The 0x68 gap is real - the second array's vector
    // header starts at +0x80 (memory@+0x88) on 1.41.8.9; it sat directly behind channelInfo1
    // on older builds.
    PAD(0x68);
    CUtlVector<ChannelInfo2> channelInfo2;
};

static_assert(offsetof(SoundChannels, channelInfo2) == 0x80);

}
