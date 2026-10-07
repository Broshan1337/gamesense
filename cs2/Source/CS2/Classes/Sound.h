#pragma once

#include "CUtlFilenameSymbolTable.h"
#include "CUtlVector.h"
#include "Vector.h"

#include <cstddef>

#include <Utils/Pad.h>

namespace cs2
{

struct CSfxTable {
    PAD(40); 
    FileNameHandle_t fileNameHandle;
};

struct ChannelInfo1 {
    CSfxTable* sfx;
    int guid;
    PAD(52); 
};

static_assert(sizeof(ChannelInfo1) == 64);







struct ChannelInfo2 {
    Vector origin;
    int guid;
};

static_assert(sizeof(ChannelInfo2) == 16);

struct SoundChannels {
    
    
    CUtlVector<ChannelInfo1> channelInfo1;
    
    
    
    PAD(0x68);
    CUtlVector<ChannelInfo2> channelInfo2;
};

static_assert(offsetof(SoundChannels, channelInfo2) == 0x80);

}
