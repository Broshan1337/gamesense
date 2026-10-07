#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <UI/ImGui/ShadowStamp.h>







namespace
{

constexpr int size = VulkanHook::shadow_texture::kStampSize;
constexpr int margin = VulkanHook::shadow_texture::kMargin;

struct Stamp {
    std::vector<std::uint8_t> bytes;

    Stamp() : bytes(static_cast<std::size_t>(size) * size * 4u)
    {
        VulkanHook::shadow_texture::generateShadowStamp(bytes.data());
    }

    [[nodiscard]] int alpha(int x, int y) const noexcept
    {
        return bytes[4u * (static_cast<std::size_t>(y) * size + x) + 3];
    }
};

}

TEST(ShadowStampTest, CoreIsOpaque) {
    const Stamp stamp;
    
    EXPECT_GE(stamp.alpha(size / 2, size / 2), 250);
    EXPECT_GE(stamp.alpha(size / 2 - 10, size / 2 + 10), 250);
}

TEST(ShadowStampTest, BoxEdgeIsHalfCoverage) {
    const Stamp stamp;
    
    
    const int a = stamp.alpha(size / 2, margin);
    EXPECT_GE(a, 110);
    EXPECT_LE(a, 145);
}

TEST(ShadowStampTest, StampCornerIsEmpty) {
    const Stamp stamp;
    
    
    EXPECT_LE(stamp.alpha(0, 0), 2);
    EXPECT_LE(stamp.alpha(size - 1, 0), 2);
    EXPECT_LE(stamp.alpha(0, size - 1), 2);
    EXPECT_LE(stamp.alpha(size - 1, size - 1), 2);
}

TEST(ShadowStampTest, TailFadesToNearlyZeroAtStampBorder) {
    const Stamp stamp;
    
    
    const int a = stamp.alpha(size / 2, 0);
    EXPECT_GE(a, 2);
    EXPECT_LE(a, 12);
}

TEST(ShadowStampTest, ProfileDecaysMonotonicallyOutsideTheBox) {
    const Stamp stamp;
    int previous = 256;
    for (int y = margin; y >= 0; y -= 4) {
        const int a = stamp.alpha(size / 2, y);
        EXPECT_LT(a, previous);
        previous = a;
    }
}

TEST(ShadowStampTest, ExactlySymmetricOnBothAxes) {
    const Stamp stamp;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            EXPECT_EQ(stamp.alpha(x, y), stamp.alpha(size - 1 - x, y)) << "x mirror at " << x << ',' << y;
            EXPECT_EQ(stamp.alpha(x, y), stamp.alpha(x, size - 1 - y)) << "y mirror at " << x << ',' << y;
        }
    }
}

TEST(ShadowStampTest, RgbChannelsAreWhiteForTintMultiplication) {
    const Stamp stamp;
    
    for (int y = 0; y < size; y += 7) {
        for (int x = 0; x < size; x += 7) {
            const auto* texel = &stamp.bytes[4u * (static_cast<std::size_t>(y) * size + x)];
            EXPECT_EQ(texel[0], 255);
            EXPECT_EQ(texel[1], 255);
            EXPECT_EQ(texel[2], 255);
        }
    }
}
