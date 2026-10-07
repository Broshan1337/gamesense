#include <gtest/gtest.h>
#include <limits>
#include <Features/Combat/Autowall/EnginePenetration.h>
#include <Features/Combat/Autowall/HealthDamage.h>
#include <GameClient/Tracing/BulletSimulation.h>

namespace {
using namespace penetration;
struct EngineFixture : testing::Test {
    engine::TraceData data;
    Bullet bullet{100, 2, 0.98f, 1000};
    Limits limits;
    TraceBudget budget;
    int target{}, blocker{};
    int calls{};
    bool stop{};
    bool badDamage{};
    int group{3};
    void SetUp() override
    {
        data.delta = {1000, 0, 0};
        data.count = 6;
        for (auto& element : data.storage) element.entityHandle = 0xffffffff;
        data.storage[4].entityHandle = 42;
        data.updateCount = 3;
        data.updates[0] = {0, .2f, 0, 0, 0, 1, 0, {}};
        data.updates[1] = {.2f, .22f, 0, 0, 1, 2, 1, {}};
        data.updates[2] = {.22f, .5f, 0, 0, 2, 4, 0, {}};
    }
    Optional<Impact> shoot()
    {
        return engine::walk(data, bullet, limits, &target, budget,
            [&](auto&, auto& state, auto& update) {
                ++calls;
                // Deliberately supplies engine damage; the walker must not
                // substitute its own neutral material model or decay twice.
                if (update.flags & 1) { state.damage -= 25; --state.remaining; }
                else state.damage = decay(state.damage, (update.exit - update.enter) * state.traceLength, state.rangeModifier);
                if (badDamage) state.damage = std::numeric_limits<float>::quiet_NaN();
                return stop;
            }, [&](std::uint32_t handle) -> void* {
                if (handle == 42) return &target;
                if (handle == 43) return &blocker;
                return nullptr;
            }, [&](const auto&) { return group; }, [](void*) { return false; });
    }
};
}

TEST_F(EngineFixture, PreservesEngineMaterialLossFalloffAndActualHitgroup)
{
    const auto impact = shoot();
    ASSERT_TRUE(impact.hasValue());
    EXPECT_NEAR(impact.value().damage, decay(decay(100, 200, .98f) - 25, 280, .98f), .0001f);
    EXPECT_FLOAT_EQ(impact.value().distance, 500);
    EXPECT_NEAR(impact.value().thickness, 20, .0001f);
    EXPECT_EQ(impact.value().penetrations, 1);
    EXPECT_EQ(impact.value().hitgroup, 3);
    EXPECT_EQ(calls, 3);
}
TEST_F(EngineFixture, DirectHitWorksWithZeroPenetrationPower)
{
    bullet.power = 0;
    data.updateCount = 1;
    data.updates[0].exit = .5f;
    data.updates[0].exitIndex = 4;
    ASSERT_TRUE(shoot().hasValue());
    EXPECT_EQ(calls, 1);
}
TEST_F(EngineFixture, ZeroPowerCannotCrossSolid)
{
    bullet.power = 0;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 1);
}
TEST_F(EngineFixture, EndpointWithoutTargetIsNotAHit)
{
    data.storage[4].entityHandle = 0xffffffff;
    EXPECT_FALSE(shoot().hasValue());
}
TEST_F(EngineFixture, OtherPlayerStopsShotBeforePenetration)
{
    data.storage[1].entityHandle = 43;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 1);
}
TEST_F(EngineFixture, ThicknessLimitStopsBeforeEngineWallCall)
{
    limits.maxTotalThickness = 10;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 1);
}
TEST_F(EngineFixture, ExhaustedBudgetAndEngineFailureFailClosed)
{
    budget.remaining = 0;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 0);
    budget.remaining = 2;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 2);
    budget.remaining = 10;
    stop = true;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 3);
}
TEST_F(EngineFixture, RejectsMalformedIndicesBeforeAnyEngineCall)
{
    data.updates[2].exitIndex = 6;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 0);
}
TEST_F(EngineFixture, RejectsMalformedFractionAndOversizedLists)
{
    data.updates[1].enter = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(shoot().hasValue());
    data.updates[1].enter = .2f;
    data.updateCount = 5000;
    EXPECT_FALSE(shoot().hasValue());
    EXPECT_EQ(calls, 0);
}
TEST_F(EngineFixture, InvalidEngineDamageAndHitgroupFailClosed)
{
    badDamage = true;
    EXPECT_FALSE(shoot().hasValue());
    badDamage = false;
    group = -1;
    EXPECT_FALSE(shoot().hasValue());
}
TEST_F(EngineFixture, CannotHitBeyondWeaponRangeOrTraceCutoff)
{
    bullet.maxRange = 400;
    EXPECT_FALSE(shoot().hasValue());
    bullet.maxRange = 1000;
    data.maxFraction = .4f;
    EXPECT_FALSE(shoot().hasValue());
}
TEST_F(EngineFixture, AllowsEngineExtensionNeededToFindWallExit)
{
    data.delta.x = 1090;
    EXPECT_TRUE(shoot().hasValue());
}

TEST(EngineHealthDamage, UsesStruckGroupAndHelmetAfterPenetration)
{
    EXPECT_FLOAT_EQ(penetration::healthDamage(10, 1, 100, false, 1, 4).value(), 40);
    EXPECT_FLOAT_EQ(penetration::healthDamage(10, 1, 100, true, 1, 4).value(), 20);
    EXPECT_FLOAT_EQ(penetration::healthDamage(10, 2, 100, false, 1, 4).value(), 5);
    EXPECT_FLOAT_EQ(penetration::healthDamage(10, 6, 100, false, 1, 4).value(), 7);
    EXPECT_FLOAT_EQ(penetration::healthDamage(1.1f, 2, 100, false, 1, 4).value(), 0);
}
TEST(EngineHealthDamage, CapsArmorConsumptionAndRejectsUnknownMetadata)
{
    EXPECT_FLOAT_EQ(penetration::healthDamage(100, 2, 10, false, 1, 4).value(), 80);
    EXPECT_FALSE(penetration::healthDamage(10, -1, 100, true, 1, 4).hasValue());
    EXPECT_FALSE(penetration::healthDamage(10, 1, -1, true, 1, 4).hasValue());
    EXPECT_FALSE(penetration::healthDamage(10, 1, 100, true, std::numeric_limits<float>::quiet_NaN(), 4).hasValue());
}

namespace {
struct FakeAllocator {
    void** vtable;
    void* freed[2]{};
    int count{};
    static void release(void* allocator, void* pointer)
    {
        auto& self = *static_cast<FakeAllocator*>(allocator);
        if (self.count < 2) self.freed[self.count] = pointer;
        ++self.count;
    }
};
}
TEST(EngineOwnership, EmbeddedAndBorrowedBuffersNeverReachAllocator)
{
    void* functions[5]{};
    functions[4] = reinterpret_cast<void*>(&FakeAllocator::release);
    FakeAllocator allocator{functions};
    void* address = &allocator;
    bullet_simulation::Binding binding;
    binding.allocatorSlot = &address;
    { bullet_simulation::TraceOwner owner{binding}; }
    EXPECT_EQ(allocator.count, 0);
    penetration::engine::Element borrowed{};
    {
        bullet_simulation::TraceOwner owner{binding};
        owner.data.elements = &borrowed;
        // External storage uses the sign bit in the grow-size field.
        owner.data.growth = static_cast<int>(0x80000000u);
    }
    EXPECT_EQ(allocator.count, 0);
}
TEST(EngineOwnership, SpilledBuffersAreReleasedThroughEngineAllocator)
{
    void* functions[5]{};
    functions[4] = reinterpret_cast<void*>(&FakeAllocator::release);
    FakeAllocator allocator{functions};
    void* address = &allocator;
    bullet_simulation::Binding binding;
    binding.allocatorSlot = &address;
    penetration::engine::Element spilledElements{};
    penetration::engine::Update spilledUpdates{};
    {
        bullet_simulation::TraceOwner owner{binding};
        owner.data.elements = &spilledElements;
        owner.data.updates = &spilledUpdates;
        owner.data.growth = owner.data.updateGrowth = 0;
    }
    ASSERT_EQ(allocator.count, 2);
    EXPECT_EQ(allocator.freed[0], &spilledElements);
    EXPECT_EQ(allocator.freed[1], &spilledUpdates);
}
