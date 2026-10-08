#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include <Hooks/Graphics/VulkanResolutionChain.h>

namespace
{





constexpr std::size_t kTextSize = 256;

struct SyntheticModule {
    
    
    std::byte text[kTextSize]{};

    
    
    
    
    
    
    static constexpr std::size_t kIterA = 0x00;
    static constexpr std::size_t kIterB = 0x17;
    static constexpr std::size_t kNameA = 0x2E;
    static constexpr std::size_t kNameB = 0x50;
    static constexpr std::size_t kSlot = 0x60;

    SyntheticModule()
    {
        writeIteration(kIterA, kNameA);
        writeIteration(kIterB, kNameB);
        std::memcpy(text + kNameA, "vkQueuePresentKHR", 18);
        std::memcpy(text + kNameB, "vkCmdPipelineBarrier2KHR", 25);
    }

    void writeIteration(std::size_t at, std::size_t nameOffset)
    {
        auto* b = text + at;
        const auto write32 = [b](std::size_t offset, std::int32_t value) {
            std::memcpy(b + offset, &value, sizeof(value));
        };
        using namespace vulkan_resolution_chain;
        b[0] = std::byte{0x48}; b[1] = std::byte{0x8D}; b[2] = std::byte{0x35};
        write32(kLeaDispOffset, static_cast<std::int32_t>(nameOffset - (at + 7)));
        b[7] = std::byte{0x48}; b[8] = std::byte{0x89}; b[9] = std::byte{0xDF};
        b[10] = std::byte{0x48}; b[11] = std::byte{0x89}; b[12] = std::byte{0x05};
        write32(kStoreDispOffset, static_cast<std::int32_t>(kSlot - (at + 17)));
        b[17] = std::byte{0xFF}; b[18] = std::byte{0x15};
        write32(19, 0x30);
    }
};

TEST(VulkanResolutionChainNameTest, DecodesNameFromLeaDisplacement) {
    SyntheticModule module;
    char name[vulkan_resolution_chain::kMaxNameLength];
    vulkan_resolution_chain::readName(module.text + SyntheticModule::kIterA, name);
    EXPECT_STREQ(name, "vkQueuePresentKHR");

    vulkan_resolution_chain::readName(module.text + SyntheticModule::kIterB, name);
    EXPECT_STREQ(name, "vkCmdPipelineBarrier2KHR");
}

TEST(VulkanResolutionChainNameTest, TruncatesLongNames) {
    SyntheticModule module;
    
    
    auto* b = module.text + SyntheticModule::kIterA;
    std::memset(module.text + 0x80, 0x78, vulkan_resolution_chain::kMaxNameLength * 2);
    const std::int32_t disp = static_cast<std::int32_t>(0x80 - (SyntheticModule::kIterA + 7));
    std::memcpy(b + vulkan_resolution_chain::kLeaDispOffset, &disp, sizeof(disp));

    char name[vulkan_resolution_chain::kMaxNameLength];
    vulkan_resolution_chain::readName(module.text + SyntheticModule::kIterA, name);
    EXPECT_EQ(std::strlen(name), vulkan_resolution_chain::kMaxNameLength - 1);
}

TEST(VulkanResolutionChainSlotTest, StoreDisplacementYieldsSlotAddress) {
    SyntheticModule module;
    
    auto* slot = vulkan_resolution_chain::storeSlot(module.text + SyntheticModule::kIterB);
    EXPECT_EQ(slot, reinterpret_cast<volatile std::uint64_t*>(module.text + SyntheticModule::kSlot));

    
    *slot = 0xDEADBEEF;
    std::uint64_t value = 0;
    std::memcpy(&value, module.text + SyntheticModule::kSlot, sizeof(value));
    EXPECT_EQ(value, 0xDEADBEEF);
}

}
