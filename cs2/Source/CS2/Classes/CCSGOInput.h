#pragma once

#include <cstdint>

namespace cs2
{




struct CUserCmd;











struct CCSGOInput {
    
    
    
    using CreateMove = void(CCSGOInput* thisptr, int slot, CUserCmd* cmd);

    
    
    
    
    
    
    static constexpr int kCreateMoveVtableSlot = 26;

    
    
    
    
    
    
    
    
    
    static constexpr int kBuildUserCmdVtableSlot = 6;
    using BuildUserCmd = std::uint64_t(CCSGOInput* thisptr, int slot, int frameNumber);

    
    
    
    
    
    
    
    
    static constexpr int kWriteMoveCrcVtableSlot = 7;
    using WriteMoveCrc = std::uint64_t(CCSGOInput* thisptr, CUserCmd* cmd);

    
    
    static constexpr int kForwardMoveOffset = 0x270;
    static constexpr int kLeftMoveOffset = 0x274;
    static constexpr int kUpMoveOffset = 0x278;

    
    
    
    
    
    
    static constexpr int kButtonStateOffset = 0x260;

    
    
    
    
    struct Buttons {
        
        
        
        
        
        static constexpr std::uint64_t kAttack = 0x1;
        static constexpr std::uint64_t kJump = 0x2;
        static constexpr std::uint64_t kForward = 0x8;
        static constexpr std::uint64_t kBack = 0x10;
        static constexpr std::uint64_t kMoveLeft = 0x200;
        static constexpr std::uint64_t kMoveRight = 0x400;
    };

    
    
    
    
    static constexpr int kViewAnglesOffset = 0x7C0;

    
    
    
    
    
    
    
    
    struct InputSampleQueue {
        static constexpr int kCountOffset = 2896;
        static constexpr int kBufferOffset = 2904;
        static constexpr int kSampleStride = 1088;

        static constexpr int kForwardMoveOffset = 24;
        static constexpr int kLeftMoveOffset = 28;

        
        
        static constexpr int kButtonStateOffset = 8;

        
        
        
        static constexpr int kMaxPlausibleCount = 256;
    };
};

}
