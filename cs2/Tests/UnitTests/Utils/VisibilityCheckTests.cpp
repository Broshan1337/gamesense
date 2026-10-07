#include <gtest/gtest.h>
#include <Features/Combat/VisibilityCheck.h>

TEST(VisibilityCheckTest, FailedTraceNeverCountsAsVisible) {
    const Tracing::Result failed{false, 1.0f, {}, {}, nullptr};
    EXPECT_FALSE(failed.reaches(nullptr));
    EXPECT_FALSE(VisibilityCheck::passes({VisibilityResult::State::Unknown, 0}, 100));
}

TEST(VisibilityCheckTest, ClearTraceAndTargetHitCountAsVisible) {
    int target;
    const Tracing::Result clear{false, 1.0f, {}, {}, nullptr, true};
    const Tracing::Result hit{true, 0.6f, {}, {}, &target, true};
    EXPECT_TRUE(clear.reaches(&target));
    EXPECT_TRUE(hit.reaches(&target));
}

TEST(VisibilityCheckTest, WorldAndOtherEntitiesBlockEvenNearTraceEnd) {
    int target, blocker;
    const Tracing::Result world{true, 0.99f, {}, {}, nullptr, true};
    const Tracing::Result entity{true, 0.99f, {}, {}, &blocker, true};
    EXPECT_FALSE(world.reaches(&target));
    EXPECT_FALSE(world.reaches(nullptr));
    EXPECT_FALSE(entity.reaches(&target));
    EXPECT_FALSE(VisibilityCheck::passes({VisibilityResult::State::EntityBlocked, 0}, 100));
}
