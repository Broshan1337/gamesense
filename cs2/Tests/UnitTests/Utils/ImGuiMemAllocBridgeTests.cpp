#include <cstddef>
#include <cstring>

#include <gtest/gtest.h>

#include <UI/ImGui/ImGuiMemAllocBridge.h>

namespace
{




constexpr std::byte kAllocFuncBytes[] = {
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x05}, std::byte{0x31}, std::byte{0xC1}, std::byte{0x2A}, std::byte{0x00},
    std::byte{0x48}, std::byte{0x89}, std::byte{0xFE},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x38},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x07},
    std::byte{0xFF}, std::byte{0x60}, std::byte{0x10}
};


constexpr std::byte kFreeFuncBytes[] = {
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x05}, std::byte{0xF1}, std::byte{0xC0}, std::byte{0x2A}, std::byte{0x00},
    std::byte{0x48}, std::byte{0x89}, std::byte{0xFE},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x38},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x07},
    std::byte{0xFF}, std::byte{0x60}, std::byte{0x20}
};


constexpr std::byte kGetSizeFuncBytes[] = {
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x05}, std::byte{0xD1}, std::byte{0xC0}, std::byte{0x2A}, std::byte{0x00},
    std::byte{0x48}, std::byte{0x89}, std::byte{0xFE},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x38},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x07},
    std::byte{0xFF}, std::byte{0xA0}, std::byte{0x90}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}
};

TEST(ImGuiMemAllocBridgeSlotTest, RecognizesCurrentAllocFuncWrapper) {
    EXPECT_EQ(ImGuiMemAllocBridge::slotFromCodeBytes(kAllocFuncBytes, sizeof(kAllocFuncBytes)), 0x10);
}

TEST(ImGuiMemAllocBridgeSlotTest, RecognizesCurrentFreeFuncWrapper) {
    EXPECT_EQ(ImGuiMemAllocBridge::slotFromCodeBytes(kFreeFuncBytes, sizeof(kFreeFuncBytes)), 0x20);
}

TEST(ImGuiMemAllocBridgeSlotTest, RecognizesDisp32JumpVariant) {
    EXPECT_EQ(ImGuiMemAllocBridge::slotFromCodeBytes(kGetSizeFuncBytes, sizeof(kGetSizeFuncBytes)), 0x90);
}

TEST(ImGuiMemAllocBridgeSlotTest, ReturnsZeroForUnrecognizedCode) {
    constexpr std::byte garbage[] = {
        std::byte{0x48}, std::byte{0x89}, std::byte{0xFE},
        std::byte{0x90}, std::byte{0x90}, std::byte{0x90}, std::byte{0x90}
    };
    EXPECT_EQ(ImGuiMemAllocBridge::slotFromCodeBytes(garbage, sizeof(garbage)), 0);
}

TEST(ImGuiMemAllocBridgeSlotTest, ReturnsZeroForEmptyBuffer) {
    constexpr std::byte empty[] = {std::byte{0x00}};
    EXPECT_EQ(ImGuiMemAllocBridge::slotFromCodeBytes(empty, 0), 0);
}

TEST(ImGuiMemAllocBridgeSlotTest, ScanToleratesFFBytesBeforeTheJump) {
    
    
    std::byte withFFEarlier[sizeof(kAllocFuncBytes)];
    std::memcpy(withFFEarlier, kAllocFuncBytes, sizeof(kAllocFuncBytes));
    withFFEarlier[3] = std::byte{0xFF};
    EXPECT_EQ(ImGuiMemAllocBridge::slotFromCodeBytes(withFFEarlier, sizeof(withFFEarlier)), 0x10);
}

}
