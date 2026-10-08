#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <Features/Combat/Autowall/Penetration.h>
#include <Features/Combat/Autowall/DamageCache.h>
#include <GameClient/Tracing/Tracing.h>

namespace {
struct Walls {
    struct Wall { float entry, exit; void* entity{}; };
    std::vector<Wall> walls;
    Tracing::Result operator()(const cs2::Vector& start, const cs2::Vector& end, void*) const
    {
        Tracing::Result result{false, 1.0f, end, {}, nullptr, true};
        const float delta = end.x - start.x;
        for (const auto& wall : walls) {
            if (start.x > wall.entry && start.x < wall.exit)
                return {true, 0.0f, start, {}, wall.entity, true};
            const float surface = delta > 0.0f ? wall.entry : wall.exit;
            const float fraction = (surface - start.x) / delta;
            if (fraction >= 0.0f && fraction < result.fraction)
                result = {true, fraction, {surface, 0, 0}, {delta > 0.0f ? -1.0f : 1.0f, 0, 0}, wall.entity, true};
        }
        return result;
    }
};
constexpr penetration::Bullet bullet{100.0f, 2.0f, 1.0f, 8192.0f};
Optional<penetration::Impact> shoot(const Walls& walls, float distance = 100.0f,
    penetration::Bullet ammo = bullet, penetration::Limits limits = {})
{
    penetration::TraceBudget budget;
    return penetration::simulate({}, {distance, 0, 0}, nullptr, nullptr, ammo, limits, budget, walls,
        [](void*) { return false; });
}
}

TEST(AutowallTest, DirectShotSupportsNonPenetratingWeaponAndAppliesFalloffOnce)
{
    auto ammo = bullet;
    ammo.power = 0.0f;
    ammo.rangeModifier = 0.98f;
    const auto result = shoot({}, 1000.0f, ammo);
    ASSERT_TRUE(result.hasValue());
    EXPECT_NEAR(result.value().damage, 100.0f * 0.98f * 0.98f, 0.0001f);
    EXPECT_FLOAT_EQ(result.value().distance, 1000.0f);
    EXPECT_FLOAT_EQ(result.value().thickness, 0.0f);
    EXPECT_EQ(result.value().penetrations, 0);
    EXPECT_FALSE(shoot({{{10, 12}}}, 100.0f, ammo).hasValue());
}

TEST(AutowallTest, ChargesWallLossAtTheWallAndThenDecaysRemainingFlight)
{
    auto ammo = bullet;
    ammo.rangeModifier = 0.8f;
    const auto result = shoot({{{100, 110}}}, 1000.0f, ammo);
    ASSERT_TRUE(result.hasValue());
    const double atExit = 100.0 * std::pow(0.8, 110.0 / 500.0);
    const double afterWall = atExit * 0.84 - 5.625 - 100.0 / 24.0;
    const double expected = afterWall * std::pow(0.8, 890.0 / 500.0);
    EXPECT_NEAR(result.value().damage, expected, 0.0001);
    EXPECT_FLOAT_EQ(result.value().thickness, 10.0f);
    EXPECT_EQ(result.value().penetrations, 1);
}

TEST(AutowallTest, NarrowAirGapsCountAsSeparateLayersWithoutBecomingSolidThickness)
{
    const auto result = shoot({{{10, 10.25f}, {10.75f, 11.0f}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().penetrations, 2);
    EXPECT_NEAR(result.value().thickness, 0.5f, 0.00001f);
    const float first = 100.0f * 0.84f - 5.625f - 0.25f * 0.25f / 24.0f;
    EXPECT_NEAR(result.value().damage, first * 0.84f - 5.625f - 0.25f * 0.25f / 24.0f, 0.0001f);
}

TEST(AutowallTest, EnforcesTotalThicknessAndFourWallLimit)
{
    const Walls walls{{{10, 12}, {20, 22}, {30, 32}, {40, 42}}};
    const auto four = shoot(walls);
    ASSERT_TRUE(four.hasValue());
    EXPECT_EQ(four.value().penetrations, 4);
    EXPECT_FLOAT_EQ(four.value().thickness, 8.0f);
    EXPECT_FALSE(shoot({{{10, 12}, {20, 22}, {30, 32}, {40, 42}, {50, 52}}}).hasValue());
    EXPECT_FALSE(shoot(walls, 100, bullet, {90, 7.99f, 4}).hasValue());
    EXPECT_TRUE(shoot(walls, 100, bullet, {90, 8, 4}).hasValue());
    EXPECT_FALSE(shoot(walls, 100, bullet, {90, 360, 3}).hasValue());
}

TEST(AutowallTest, ProbesLastFractionalIntervalAndExitsNearTarget)
{
    EXPECT_TRUE(shoot({{{10, 10.12f}}}, 10.3f).hasValue());
    auto ammo = bullet;
    ammo.damage = 1000.0f;
    EXPECT_TRUE(shoot({{{10, 99.99f}}}, 101, ammo).hasValue());
    EXPECT_FALSE(shoot({{{10, 101}}}, 102, ammo).hasValue());
}

TEST(AutowallTest, TraceBudgetBoundsWorkAndNeverTurnsExhaustionIntoVisibility)
{
    penetration::TraceBudget budget{3};
    int calls{};
    const Walls walls{{{10, 50}}};
    const auto trace = [&](const auto& start, const auto& end, void* skip) {
        ++calls;
        return walls(start, end, skip);
    };
    EXPECT_FALSE(penetration::simulate({}, {100, 0, 0}, nullptr, nullptr, bullet, {}, budget, trace,
        [](void*) { return false; }).hasValue());
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(budget.remaining, 0);
    EXPECT_FALSE(penetration::simulate({}, {100, 0, 0}, nullptr, nullptr, bullet, {}, budget, trace,
        [](void*) { return false; }).hasValue());
    EXPECT_EQ(calls, 3);
}

TEST(AutowallTest, PenetratesApprovedPropsButBlocksOtherEntitiesAndWrongExitEntity)
{
    int prop{}, other{};
    const Walls walls{{{10, 12, &prop}}};
    penetration::TraceBudget budget;
    const auto allowed = [&](void* entity) { return entity == &prop; };
    const auto result = penetration::simulate({}, {100, 0, 0}, nullptr, nullptr, bullet, {}, budget, walls, allowed);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().penetrations, 1);
    EXPECT_FALSE(shoot(walls).hasValue());
    budget = {};
    const auto wrong = [&](const auto& start, const auto& end, void* skip) {
        auto result = walls(start, end, skip);
        if (end.x < start.x && result.didHit) result.hitEntity = &other;
        return result;
    };
    EXPECT_FALSE(penetration::simulate({}, {100, 0, 0}, nullptr, nullptr, bullet, {}, budget, wrong, allowed).hasValue());
}

TEST(AutowallTest, AcceptsIntendedTargetButNeverAnotherPlayer)
{
    int target{}, other{};
    const Walls targetOnly{{{80, 90, &target}}};
    penetration::TraceBudget budget;
    const auto result = penetration::simulate({}, {100, 0, 0}, nullptr, &target, bullet, {}, budget, targetOnly,
        [](void*) { return false; });
    ASSERT_TRUE(result.hasValue());
    EXPECT_FLOAT_EQ(result.value().distance, 80.0f);
    EXPECT_EQ(result.value().penetrations, 0);
    budget = {};
    const Walls blocked{{{60, 70, &other}, {80, 90, &target}}};
    EXPECT_FALSE(penetration::simulate({}, {100, 0, 0}, nullptr, &target, bullet, {}, budget, blocked,
        [](void*) { return false; }).hasValue());
}

TEST(AutowallTest, RejectsInvalidWeaponDataRangeAndOffRayResults)
{
    auto ammo = bullet;
    ammo.maxRange = 99.0f;
    EXPECT_FALSE(shoot({}, 100, ammo).hasValue());
    for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        ammo = bullet; ammo.rangeModifier = invalid;
        EXPECT_FALSE(shoot({}, 100, ammo).hasValue());
    }
    for (int fault = 0; fault < 4; ++fault) {
        penetration::TraceBudget budget;
        const auto malformed = [&](const auto&, const auto& end, void*) {
            Tracing::Result result{false, 1, end, {}, nullptr, true};
            if (fault == 0) result.valid = false;
            if (fault == 1) result.fraction = 0.5f;
            if (fault == 2) result.endPos.y = 100.0f;
            if (fault == 3) result.normal.x = std::numeric_limits<float>::quiet_NaN();
            return result;
        };
        EXPECT_FALSE(penetration::simulate({}, {100, 0, 0}, nullptr, nullptr, bullet, {}, budget, malformed,
            [](void*) { return false; }).hasValue());
    }
}

TEST(AutowallTest, ObliqueWallsUseRayThicknessAndNeverInfiniteRetry)
{
    const auto angled = [](const cs2::Vector& start, const cs2::Vector& end, void*) {
        if (start.x > 10 && start.x < 12)
            return Tracing::Result{true, 0, start, {}, nullptr, true};
        const bool forward = end.x > start.x;
        const float x = forward ? 10.0f : 12.0f;
        const float fraction = (x - start.x) / (end.x - start.x);
        if (fraction < 0 || fraction >= 1)
            return Tracing::Result{false, 1, end, {}, nullptr, true};
        return Tracing::Result{true, fraction, {x, start.y + (end.y-start.y)*fraction, 0},
            {forward ? -1.0f : 1.0f, 0, 0}, nullptr, true};
    };
    penetration::TraceBudget budget;
    const auto result = penetration::simulate({}, {100, 100, 0}, nullptr, nullptr, bullet, {}, budget, angled,
        [](void*) { return false; });
    ASSERT_TRUE(result.hasValue());
    EXPECT_NEAR(result.value().thickness, std::sqrt(8.0f), 0.0001f);
}

TEST(AutowallCacheTest, SeparatesEyePointTargetAndHitgroupAndResetsPerCommand)
{
    penetration::DamageCache cache;
    int target{}, other{};
    cache.store({}, {100, 0, 0}, &target, 1, -1.0f);
    ASSERT_TRUE(cache.find({}, {100, 0, 0}, &target, 1).hasValue());
    EXPECT_FLOAT_EQ(cache.find({}, {100, 0, 0}, &target, 1).value(), -1.0f);
    EXPECT_FALSE(cache.find({1, 0, 0}, {100, 0, 0}, &target, 1).hasValue());
    EXPECT_FALSE(cache.find({}, {101, 0, 0}, &target, 1).hasValue());
    EXPECT_FALSE(cache.find({}, {100, 0, 0}, &other, 1).hasValue());
    EXPECT_FALSE(cache.find({}, {100, 0, 0}, &target, 2).hasValue());
    cache.reset();
    EXPECT_FALSE(cache.find({}, {100, 0, 0}, &target, 1).hasValue());
    for (int i = 0; i < 1000; ++i) cache.store({}, {float(i), 0, 0}, &target, 1, float(i));
    EXPECT_FLOAT_EQ(cache.find({}, {999, 0, 0}, &target, 1).value(), 999.0f);
    EXPECT_FALSE(cache.find({}, {100, 0, 0}, &target, 1).hasValue());
}

TEST(AutowallTraceTest, UnavailableAndMalformedTracesCannotClaimVisibleTarget)
{
    int target{};
    EXPECT_FALSE(Tracing::Result{}.reaches(&target));
    EXPECT_FALSE((Tracing::Result{false, 0.5f, {}, {}, nullptr, true}).reaches(&target));
    EXPECT_FALSE((Tracing::Result{true, 0.5f, {}, {}, &target, false}).reaches(&target));
    EXPECT_TRUE((Tracing::Result{false, 1.0f, {}, {}, nullptr, true}).reaches(&target));
}
