#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <UI/ImGui/ShadowStamp.h>

// Pins the closed-form gaussian stamp behind the menu's soft shadows (erfc of the rounded-box
// SDF). The UI's 9-slice drawing leans on exactly these properties: an opaque core, ~50%
// coverage at the box edge, monotone decay to a near-zero tail at the stamp border, and exact
// two-axis symmetry - the symmetry is what makes adjacent 9-slice pieces share their boundary
// stamp row/column without a visible seam.

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
    // Deep inside the box the gaussian is saturated; truncation can land at 254.
    EXPECT_GE(stamp.alpha(size / 2, size / 2), 250);
    EXPECT_GE(stamp.alpha(size / 2 - 10, size / 2 + 10), 250);
}

TEST(ShadowStampTest, BoxEdgeIsHalfCoverage) {
    const Stamp stamp;
    // Mid top edge, one pixel row in from the boundary: the convolved edge sits at ~50%
    // (pixel-center sampling nudges it slightly one way or the other).
    const int a = stamp.alpha(size / 2, margin);
    EXPECT_GE(a, 110);
    EXPECT_LE(a, 145);
}

TEST(ShadowStampTest, StampCornerIsEmpty) {
    const Stamp stamp;
    // The stamp corners are far outside the blurred box - there must be no shadow bleed there,
    // or the 9-slice corner pieces would show square halos around rounded corners.
    EXPECT_LE(stamp.alpha(0, 0), 2);
    EXPECT_LE(stamp.alpha(size - 1, 0), 2);
    EXPECT_LE(stamp.alpha(0, size - 1), 2);
    EXPECT_LE(stamp.alpha(size - 1, size - 1), 2);
}

TEST(ShadowStampTest, TailFadesToNearlyZeroAtStampBorder) {
    const Stamp stamp;
    // One full margin outside the box edge (mid top edge): sigma = margin/2 puts the profile
    // at ~2.5% here, which the UI multiplies down further - the visible shadow tail.
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
    // The UI tints the stamp black; RGB must be 255 so a future tint change recolors it.
    for (int y = 0; y < size; y += 7) {
        for (int x = 0; x < size; x += 7) {
            const auto* texel = &stamp.bytes[4u * (static_cast<std::size_t>(y) * size + x)];
            EXPECT_EQ(texel[0], 255);
            EXPECT_EQ(texel[1], 255);
            EXPECT_EQ(texel[2], 255);
        }
    }
}
