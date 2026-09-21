#include <gtest/gtest.h>

#include <cmath>

#include <Utils/FvaMath.h>

// Pins the angle math of the FVA view-angle emitter. The reference implementation used
// std::remainderf(dx, 360) plus an interval nudge; fold180() reproduces that contract with
// truncation arithmetic because this project links -nostdlib, so these tests exist to prove the
// replacement is byte-equivalent on the values the wire will actually see.

TEST(FvaMathTest, Fold180KeepsSmallAnglesUntouched) {
    EXPECT_FLOAT_EQ(fva_math::fold180(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(fva_math::fold180(90.0f), 90.0f);
    EXPECT_FLOAT_EQ(fva_math::fold180(-45.5f), -45.5f);
    EXPECT_FLOAT_EQ(fva_math::fold180(179.9f), 179.9f);
}

TEST(FvaMathTest, Fold180FoldsFullCirclesToZero) {
    EXPECT_FLOAT_EQ(fva_math::fold180(360.0f), 0.0f);
    EXPECT_FLOAT_EQ(fva_math::fold180(720.0f), 0.0f);
    EXPECT_FLOAT_EQ(fva_math::fold180(-360.0f), 0.0f);
}

TEST(FvaMathTest, Fold180PicksShortPathAcrossWrapBoundary) {
    // A turn of +181 degrees is the same as going -179.
    EXPECT_NEAR(fva_math::fold180(181.0f), -179.0f, 0.0001f);
    EXPECT_NEAR(fva_math::fold180(-181.0f), 179.0f, 0.0001f);
    // The classic spin case: several accumulated rotations land wherever the remainder says.
    EXPECT_NEAR(fva_math::fold180(1079.0f), -1.0f, 0.0001f);   // 1080-1
}

TEST(FvaMathTest, Fold180MatchesRemainderfEndpointParity) {
    // std::remainderf(x, 360) returns (-180, 180]: exactly -180 must read back as +180 and
    // exactly +180 stays +180. The reference emitter relied on this parity when deciding chain
    // directions across the boundary, so getting it wrong flips interpolation side.
    EXPECT_FLOAT_EQ(fva_math::fold180(-180.0f), 180.0f);
    EXPECT_FLOAT_EQ(fva_math::fold180(180.0f), 180.0f);
}

TEST(FvaMathTest, Fold180HandlesNaNAsZeroRotation) {
    EXPECT_FLOAT_EQ(fva_math::fold180(std::nanf("")), 0.0f);
}

TEST(FvaMathTest, InterpolatedEndsExactlyOnTargetAndNeverOvershoots) {
    for (int count = 1; count <= 15; ++count) {
        const auto value = fva_math::interpolated(10.0f, 20.0f, count - 1, count);
        EXPECT_FLOAT_EQ(value, 20.0f);
        for (int i = 0; i < count - 1; ++i)
            ASSERT_GT(fva_math::interpolated(10.0f, 20.0f, i, count), 10.0f);
    }
}

TEST(FvaMathTest, InterpolatedFollowsWrappedShortPathAroundTheGlobe) {
    // from 170 to -170: shortest path goes through +180 (a +20 degree hop), not through zero
    // (the -340 one). The LAST entry must land exactly on the target modulo one full circle -
    // raw sums can exceed 180 when the chain leaves the wrap boundary behind on purpose.
    const float start = 170.0f;
    const float end = -170.0f;
    const float first = fva_math::interpolated(start, end, 0, 4);
    const float last = fva_math::interpolated(start, end, 3, 4);
    EXPECT_GT(first, start);
    EXPECT_LT(first, 200.0f);
    EXPECT_FLOAT_EQ(fva_math::fold180(last - end), 0.0f);
}

TEST(FvaMathTest, WhenAtSpansBaseThroughBasePlusSpanStrictlyIncreasing) {
    const int count = 8;
    float previous = -1.0f;
    for (int i = 0; i < count; ++i) {
        const float when = fva_math::whenAt(i, count);
        EXPECT_GT(when, previous);
        previous = when;
    }
    EXPECT_FLOAT_EQ(fva_math::whenAt(count - 1, count), fva_math::kWhenBase + fva_math::kWhenSpan);
    EXPECT_FLOAT_EQ(fva_math::whenAt(0, count), fva_math::kWhenBase + fva_math::kWhenSpan / count);
}

TEST(FvaMathTest, PlannedEntriesRespectsRequestAndRoom) {
    EXPECT_EQ(fva_math::plannedEntries(0, 10), 0);
    EXPECT_EQ(fva_math::plannedEntries(5, 0), 0);
    EXPECT_EQ(fva_math::plannedEntries(-3, 10), 0);
    EXPECT_EQ(fva_math::plannedEntries(8, 32), 8);
    EXPECT_EQ(fva_math::plannedEntries(8, 4), 4);
}

TEST(FvaMathTest, IsDiscontinuityFlagsOnlyHalfCircleJumps) {
    EXPECT_FALSE(fva_math::isDiscontinuity(10.0f, 20.0f));
    EXPECT_FALSE(fva_math::isDiscontinuity(359.0f, 1.0f)); // wrapped motion stays continuity
    EXPECT_TRUE(fva_math::isDiscontinuity(0.0f, 180.0f));
    EXPECT_TRUE(fva_math::isDiscontinuity(0.0f, -180.0f));
    EXPECT_TRUE(fva_math::isDiscontinuity(10.0f, std::nanf("")));
}
