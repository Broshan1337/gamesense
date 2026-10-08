#include <array>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>
#include <Features/Combat/AimTarget.h>

namespace {
struct Enemy {
    cs2::C_BaseEntity entity{};
    bool enemy{true};
    bool alive{true};
    bool local{false};
    Optional<bool> immunity{false};
    int health{100};
    std::array<Optional<cs2::Vector>, 26> bones;
};
struct Node {
    Enemy* enemy;
    Optional<cs2::Vector> bonePosition(int index) const { return enemy->bones[index]; }
};
struct PawnBase {
    Enemy* enemy;
    Node gameSceneNode() const { return {enemy}; }
};
struct Pawn {
    Enemy* enemy;
    explicit operator bool() const { return enemy != nullptr; }
    bool isControlledByLocalPlayer() const { return enemy->local; }
    Optional<bool> isEnemy() const { return enemy->enemy; }
    Optional<bool> isAlive() const { return enemy->alive; }
    Optional<int> health() const { return enemy->health; }
    Optional<bool> hasImmunity() const { return enemy->immunity; }
    PawnBase baseEntity() const { return {enemy}; }
};
struct Classification {
    template <typename T> bool is() const { return std::is_same_v<T, cs2::C_CSPlayerPawn>; }
};
struct Base {
    Enemy* enemy;
    Classification classify() const { return {}; }
    template <template <typename> typename T> Pawn as() const { return {enemy}; }
};
struct Entities {
    std::vector<Enemy>* enemies;
    template <typename Fn> void forEachNetworkableEntityIdentity(Fn&& fn) const {
        for (auto& enemy : *enemies) {
            struct Identity { cs2::C_BaseEntity* entity; } identity{&enemy.entity};
            fn(identity);
        }
    }
};
struct Extrapolator {
    cs2::Vector predictedDelta(const Pawn&, int) const { return {}; }
};
struct Context {
    std::vector<Enemy> enemies;
    template <template <typename> typename T> auto make() {
        if constexpr (std::is_same_v<T<Context>, EntitySystem<Context>>)
            return Entities{&enemies};
        else
            return Extrapolator{};
    }
    template <template <typename> typename T> Base make(cs2::C_BaseEntity* entity) {
        for (auto& enemy : enemies)
            if (&enemy.entity == entity)
                return {&enemy};
        return {};
    }
};
using Targets = AimTarget<Context>;
constexpr Targets::HitboxFlags headOnly{true, false, false, false, false};
constexpr cs2::Vector eye{};
const auto acceptAll = [](const auto&) { return true; };
}

TEST(AimTargetTest, SkipsEveryBlockedEnemyWithoutRetryLimit) {
    Context context;
    context.enemies.resize(7);
    for (std::size_t i = 0; i < context.enemies.size(); ++i)
        context.enemies[i].bones[6] = cs2::Vector{100.0f, static_cast<float>(i + 1), 0.0f};
    auto* const visible = &context.enemies.back().entity;
    const auto result = Targets{context}.acquire(eye, 0, 0, 30, headOnly,
        [visible](const auto& candidate) { return candidate.entity == visible; });
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().entity, visible);
}

TEST(AimTargetTest, FallsBackToVisibleChestWhenHeadIsBlocked) {
    Context context;
    context.enemies.resize(1);
    context.enemies[0].bones[6] = cs2::Vector{100, 0, 10};
    context.enemies[0].bones[4] = cs2::Vector{100, 0, 0};
    const auto result = Targets{context}.acquire(eye, 0, 0, 30, {true, true, false, false, false},
        [](const auto& candidate) { return candidate.hitgroup == 2; });
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().hitgroup, 2);
}

TEST(AimTargetTest, RetainsIncumbentAndReacquiresWhenBlocked) {
    Context context;
    context.enemies.resize(2);
    context.enemies[0].bones[6] = cs2::Vector{100, 1, 0};
    context.enemies[1].bones[6] = cs2::Vector{100, 10, 0};
    auto* const incumbent = &context.enemies[1].entity;
    const auto kept = Targets{context}.acquire(eye, 0, 0, 30, headOnly, acceptAll, incumbent);
    ASSERT_TRUE(kept.hasValue());
    EXPECT_EQ(kept.value().entity, incumbent);
    const auto next = Targets{context}.acquire(eye, 0, 0, 30, headOnly,
        [incumbent](const auto& candidate) { return candidate.entity != incumbent; }, incumbent);
    ASSERT_TRUE(next.hasValue());
    EXPECT_EQ(next.value().entity, &context.enemies[0].entity);
}

TEST(AimTargetTest, DropsDeadIncumbentAndRejectsFriendliesAndLocalPlayer) {
    Context context;
    context.enemies.resize(4);
    for (auto& enemy : context.enemies)
        enemy.bones[6] = cs2::Vector{100, 0, 0};
    context.enemies[0].alive = false;
    context.enemies[1].enemy = false;
    context.enemies[2].local = true;
    const auto result = Targets{context}.acquire(eye, 0, 0, 30, headOnly, acceptAll, &context.enemies[0].entity);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().entity, &context.enemies[3].entity);
}

TEST(AimTargetTest, DropsIncumbentOutsideFovAndHandlesNoEnabledHitboxes) {
    Context context;
    context.enemies.resize(2);
    context.enemies[0].bones[6] = cs2::Vector{100, 100, 0};
    context.enemies[1].bones[6] = cs2::Vector{100, 1, 0};
    const auto result = Targets{context}.acquire(eye, 0, 0, 10, headOnly, acceptAll, &context.enemies[0].entity);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().entity, &context.enemies[1].entity);
    EXPECT_FALSE(Targets{context}.acquire(eye, 0, 0, 30, {}, acceptAll).hasValue());
}

TEST(AimTargetTest, ChoosesNearestEligibleEnemyWhilePreservingHitboxPriority) {
    Context context;
    context.enemies.resize(2);
    context.enemies[0].bones[6] = cs2::Vector{100, 5, 0};
    context.enemies[1].bones[6] = cs2::Vector{100, 10, 0};
    context.enemies[1].bones[4] = cs2::Vector{100, 0, 0};
    const auto result = Targets{context}.acquire(eye, 0, 0, 30, {true, true, false, false, false}, acceptAll);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().entity, &context.enemies[0].entity);
    EXPECT_EQ(result.value().hitgroup, 1);
}

TEST(AimTargetTest, ReturnsNoTargetWhenAllCandidatesAreBlocked) {
    Context context;
    context.enemies.resize(2);
    for (auto& enemy : context.enemies)
        enemy.bones[6] = cs2::Vector{100, 1, 0};
    EXPECT_FALSE(Targets{context}.acquire(eye, 0, 0, 30, headOnly,
        [](const auto&) { return false; }, &context.enemies[0].entity).hasValue());
}

TEST(AimTargetTest, SelectionModesRankEligibleEnemiesIndependently) {
    Context context;
    context.enemies.resize(3);
    context.enemies[0].bones[6] = cs2::Vector{1000, 0, 0};
    context.enemies[1].bones[6] = cs2::Vector{100, 10, 0};
    context.enemies[2].bones[6] = cs2::Vector{500, 100, 0};
    context.enemies[2].health = 10;
    for (int i = 0; i < 3; ++i) {
        const auto result = Targets{context}.acquire(eye, 0, 0, 30, headOnly, acceptAll, nullptr,
            static_cast<target_selection::Mode>(i));
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(result.value().entity, &context.enemies[i].entity);
    }
}

TEST(AimTargetTest, HealthSelectionUsesFovToBreakTiesAndStillFiltersBlockedTargets) {
    Context context;
    context.enemies.resize(3);
    context.enemies[0].bones[6] = cs2::Vector{100, 10, 0};
    context.enemies[1].bones[6] = cs2::Vector{100, 1, 0};
    context.enemies[2].bones[6] = cs2::Vector{100, 0, 0};
    context.enemies[2].health = 1;
    const auto result = Targets{context}.acquire(eye, 0, 0, 30, headOnly,
        [&](const auto& candidate) { return candidate.entity != &context.enemies[2].entity; },
        nullptr, target_selection::Mode::Health);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().entity, &context.enemies[1].entity);
}

TEST(AimTargetTest, DistanceModeRetainsValidLockAndReacquiresByDistanceWhenInvalid) {
    Context context;
    context.enemies.resize(2);
    context.enemies[0].bones[6] = cs2::Vector{100, 10, 0};
    context.enemies[1].bones[6] = cs2::Vector{1000, 0, 0};
    auto* const incumbent = &context.enemies[1].entity;
    const auto kept = Targets{context}.acquire(eye, 0, 0, 30, headOnly, acceptAll, incumbent,
        target_selection::Mode::Distance);
    ASSERT_TRUE(kept.hasValue());
    EXPECT_EQ(kept.value().entity, incumbent);
    context.enemies[1].alive = false;
    const auto next = Targets{context}.acquire(eye, 0, 0, 30, headOnly, acceptAll, incumbent,
        target_selection::Mode::Distance);
    ASSERT_TRUE(next.hasValue());
    EXPECT_EQ(next.value().entity, &context.enemies[0].entity);
}

TEST(AimTargetTest, RejectsNonFiniteAndCoincidentPoints) {
    Context context;
    context.enemies.resize(2);
    context.enemies[0].bones[6] = cs2::Vector{std::numeric_limits<float>::infinity(), 0, 0};
    context.enemies[1].bones[6] = eye;
    EXPECT_FALSE(Targets{context}.acquire(eye, 0, 0, 30, headOnly, acceptAll).hasValue());
}

TEST(AimTargetTest, RejectsImmuneAndUnknownImmunityTargets) {
    Context context;
    context.enemies.resize(3);
    for (auto& enemy : context.enemies) enemy.bones[6] = cs2::Vector{100,0,0};
    context.enemies[0].immunity=true;
    context.enemies[1].immunity={};
    const auto result=Targets{context}.acquire(eye,0,0,30,headOnly,acceptAll);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().entity,&context.enemies[2].entity);
}

TEST(AimTargetTest, RejectsNonFiniteViewAndEyeCoordinates) {
    Context context;
    context.enemies.resize(1);
    context.enemies[0].bones[6] = cs2::Vector{100,0,0};
    const float invalid=std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Targets{context}.acquire(eye,invalid,0,30,headOnly,acceptAll).hasValue());
    EXPECT_FALSE(Targets{context}.acquire({invalid,0,0},0,0,30,headOnly,acceptAll).hasValue());
    EXPECT_FALSE(Targets{context}.acquire(eye,0,0,invalid,headOnly,acceptAll).hasValue());
}

TEST(AimTargetTest, RanksBeforeVisibilitySoClosestTargetNeedsOnlyOneExpensiveCheck) {
    Context context; context.enemies.resize(32);
    for (int i = 0; i < 32; ++i) context.enemies[i].bones[6] = cs2::Vector{100, float(32 - i), 0};
    int calls = 0;
    const auto result = Targets{context}.acquire(eye, 0, 0, 30, headOnly, [&](const auto&) { ++calls; return true; });
    ASSERT_TRUE(result.hasValue()); EXPECT_EQ(result.value().entity, &context.enemies.back().entity);
    EXPECT_EQ(calls, 1);
}
