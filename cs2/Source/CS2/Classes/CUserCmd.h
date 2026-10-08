#pragma once

#include <cstdint>

namespace cs2
{















struct CSubtickMoveStep {
    static constexpr int kHasBitsOffset = 0x10;

    static constexpr int kButtonOffset = 0x18;
    static constexpr int kPressedOffset = 0x20;
    static constexpr int kWhenOffset = 0x24;
    static constexpr int kAnalogForwardDeltaOffset = 0x28;
    static constexpr int kAnalogLeftDeltaOffset = 0x2C;
    
    
    
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












struct CUserCmd {
    // Native command sequence (legacy_command_number in the protobuf stays zero).
    static constexpr int kCommandNumberOffset = 8;

    
    
    
    static constexpr int kBaseMessageOffset = 64;

    
    static constexpr int kOuterHasBitsOffset = 32;
    static constexpr std::uint32_t kBaseMessageHasBit = 1;

    
    
    
    
    
    
    
    
    
    
    
    
    static constexpr int kButtonState1Offset = 96;
    static constexpr int kButtonState2Offset = 104;
    static constexpr int kButtonState3Offset = 112;

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    static constexpr int kInputHistorySizeOffset = 48;
    static constexpr int kAttack1StartHistoryIndexOffset = 76;
    static constexpr std::uint32_t kAttack1StartHistoryIndexHasBit = 0x20;

    
    
    
    static constexpr int kMaxInputHistoryEntries = 32;

    
    
    
    
    
    
    
    
    
    
    
    struct InputHistory {
        static constexpr int kFieldOffset = 40;
        static constexpr int kCurrentSizeOffset = 48; 
        static constexpr int kTotalSizeOffset = 52;   
        static constexpr int kRepOffset = 56;         
        static constexpr int kRepAllocatedSizeOffset = 0; 
        static constexpr int kRepElementsOffset = 8;

        
        
        
        
        
        static constexpr std::ptrdiff_t kEntryHasBitsOffset = 0x10;
        static constexpr std::ptrdiff_t kEntryViewAnglesOffset = 0x18;
        static constexpr std::ptrdiff_t kEntryRenderTickCountOffset = 0x60;
        static constexpr std::ptrdiff_t kEntryRenderTickFractionOffset = 0x64;
        static constexpr std::uint32_t kEntryViewAnglesHasBit = 0x0001;
        static constexpr std::uint32_t kEntryRenderTickCountHasBit = 0x0200;
        static constexpr std::uint32_t kEntryRenderTickFractionHasBit = 0x0400;
        static constexpr std::uint32_t kEntryPlayerTickCountHasBit = 0x0800;
        static constexpr std::uint32_t kEntryPlayerTickFractionHasBit = 0x1000;
        
        
        
        
        
        
        
        
        
        
        static constexpr std::uint32_t kEmittedEntryHasBits =
            kEntryViewAnglesHasBit | kEntryRenderTickCountHasBit | kEntryRenderTickFractionHasBit
            | kEntryPlayerTickCountHasBit | kEntryPlayerTickFractionHasBit;
    };

    struct BaseMessage {
        
        
        
        
        
        
        
        
        
        
        
        
        
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

        
        
        
        
        
        
        struct SubtickMoves {
            static constexpr int kFieldOffset = 0x18;

            static constexpr int kArenaOffset = 0;
            static constexpr int kCurrentSizeOffset = 8;
            static constexpr int kRepOffset = 16;

            static constexpr int kRepAllocatedSizeOffset = 0;
            static constexpr int kRepElementsOffset = 8;

            
            
            static constexpr int kMaxSteps = 32;
        };

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        static constexpr int kForwardMoveOffset = 88;
        static constexpr int kLeftMoveOffset = 92;
        static constexpr int kUpMoveOffset = 96;

        
        
        
        
        
        
        static constexpr int kMouseDxOffset = 112;

        
        
        
        
        
        
        
        static constexpr int kRandomSeedOffset = 108;
        static constexpr std::uint32_t kRandomSeedHasBit = 0x800;

        static constexpr std::uint32_t kForwardMoveHasBit = 0x40;
        static constexpr std::uint32_t kLeftMoveHasBit = 0x80;
        static constexpr std::uint32_t kUpMoveHasBit = 0x100;

        
        static constexpr int kViewAnglesOffset = 64;
        static constexpr std::uint32_t kViewAnglesHasBit = 0x4;

        struct ViewAngles {
            
            
            static constexpr int kPitchOffset = 24;
            static constexpr int kYawOffset = 28;
            static constexpr int kRollOffset = 32;
        };
    };
};

}
