#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <Features/Visuals/ModelGlow/PlayerModelGlow/PlayerModelGlow.h>
#include <Mocks/MockConfig.h>
#include <Mocks/MockHookContext.h>
#include <Mocks/MockPlayerController.h>
#include <Mocks/MockPlayerPawn.h>

std::uint64_t PlayerPawn_sceneObjectUpdater(cs2::C_CSPlayerPawn* playerPawn, void* unknown, bool unknownBool) noexcept
{
    return 0;
}

class PlayerModelGlowTest : public testing::Test {
protected:
    PlayerModelGlowTest()
    {
        EXPECT_CALL(mockHookContext, config()).WillRepeatedly(testing::ReturnRef(mockConfig));
    }

    testing::StrictMock<MockHookContext> mockHookContext{};
    testing::StrictMock<MockConfig> mockConfig{};
    testing::StrictMock<MockPlayerPawn> mockPlayerPawn{};
    testing::StrictMock<MockPlayerController> mockPlayerController{};
    FeaturesStates featuresStates{};
    PlayerModelGlow<MockHookContext> playerModelGlow{mockHookContext};
};

TEST_F(PlayerModelGlowTest, Disabled) {
    mockConfig.expectGetVariable<model_glow_vars::GlowPlayers>(false);
    EXPECT_FALSE(playerModelGlow.enabled());
}

TEST_F(PlayerModelGlowTest, Enabled) {
    mockConfig.expectGetVariable<model_glow_vars::GlowPlayers>(true);
    EXPECT_TRUE(playerModelGlow.enabled());
}

TEST_F(PlayerModelGlowTest, CorrectDeactivationFlagIsReturned) {
    EXPECT_EQ(playerModelGlow.deactivationFlag(), ModelGlowDeactivationFlags::PlayerModelGlowDeactivating);
}

TEST_F(PlayerModelGlowTest, CorrectOriginalSceneObjectUpdaterIsReturned) {
    EXPECT_CALL(mockHookContext, featuresStates()).WillOnce(testing::ReturnRef(featuresStates));
    EXPECT_THAT(playerModelGlow.originalSceneObjectUpdater(), testing::Ref(featuresStates.visualFeaturesStates.modelGlowState.originalPlayerPawnSceneObjectUpdater));
}

TEST_F(PlayerModelGlowTest, CorrectReplacementSceneObjectUpdaterIsReturned) {
    EXPECT_EQ(playerModelGlow.replacementSceneObjectUpdater(), &PlayerPawn_sceneObjectUpdater);
}

struct PlayerModelGlowShouldApplyTestParam {
    bool onlyEnemies{true};
    std::optional<bool> isAlive{true};
    Optional<int> health{100};
    bool isControlledByLocalPlayer{false};
    bool isTTorCT{true};
    std::optional<bool> isEnemy{true};
    bool expectPlayerPawnAccess{true};
    bool expectEnemyCheck{true};
    bool expectGlowApplied{true};
};

class PlayerModelGlowShouldApplyTest
    : public PlayerModelGlowTest,
      public testing::WithParamInterface<PlayerModelGlowShouldApplyTestParam> {
};

TEST_P(PlayerModelGlowShouldApplyTest, GlowIsAppliedWhenExpected) {
    EXPECT_CALL(mockConfig, getVariable(ConfigVariableTypes::indexOf<model_glow_vars::GlowOnlyEnemies>()))
        .WillRepeatedly(testing::Return(GetParam().onlyEnemies));

    if (GetParam().expectPlayerPawnAccess) {
        EXPECT_CALL(mockPlayerPawn, isAlive()).WillRepeatedly(testing::Return(GetParam().isAlive));
        EXPECT_CALL(mockPlayerPawn, health()).WillRepeatedly(testing::Return(GetParam().health));
        EXPECT_CALL(mockPlayerPawn, isControlledByLocalPlayer()).WillRepeatedly(testing::Return(GetParam().isControlledByLocalPlayer));
        EXPECT_CALL(mockPlayerPawn, isTTorCT()).WillRepeatedly(testing::Return(GetParam().isTTorCT));

        if (GetParam().expectEnemyCheck)
            EXPECT_CALL(mockPlayerPawn, isEnemy()).WillRepeatedly(testing::Return(GetParam().isEnemy));
    }

    EXPECT_EQ(playerModelGlow.shouldApplyGlow(mockPlayerPawn), GetParam().expectGlowApplied);
}

INSTANTIATE_TEST_SUITE_P(OnlyEnemies, PlayerModelGlowShouldApplyTest, testing::ValuesIn(
    std::to_array<PlayerModelGlowShouldApplyTestParam>({
        {
            .onlyEnemies = true,
            .isEnemy{true},
            .expectEnemyCheck = true,
            .expectGlowApplied = true
        },
        {
            .onlyEnemies = true,
            .isEnemy{false},
            .expectEnemyCheck = true,
            .expectGlowApplied = false
        },
        {
            .onlyEnemies = true,
            .isEnemy{std::nullopt},
            .expectEnemyCheck = true,
            .expectGlowApplied = true
        }
    })
));

INSTANTIATE_TEST_SUITE_P(NotOnlyEnemies, PlayerModelGlowShouldApplyTest, testing::ValuesIn(
    std::to_array<PlayerModelGlowShouldApplyTestParam>({
        {
            .onlyEnemies = false,
            .isEnemy{true},
            .expectEnemyCheck = false,
            .expectGlowApplied = true
        },
        {
            .onlyEnemies = false,
            .isEnemy{false},
            .expectEnemyCheck = false,
            .expectGlowApplied = true
        },
        {
            .onlyEnemies = false,
            .isEnemy{std::nullopt},
            .expectEnemyCheck = false,
            .expectGlowApplied = true
        }
    })
));

INSTANTIATE_TEST_SUITE_P(IsAlive, PlayerModelGlowShouldApplyTest, testing::ValuesIn(
    std::to_array<PlayerModelGlowShouldApplyTestParam>({
        {
            .isAlive{true},
            .expectGlowApplied = true
        },
        {
            .isAlive{false},
            .expectGlowApplied = false
        },
        {
            .isAlive{std::nullopt},
            .expectGlowApplied = true
        }
    })
));

INSTANTIATE_TEST_SUITE_P(Health, PlayerModelGlowShouldApplyTest, testing::ValuesIn(
    std::to_array<PlayerModelGlowShouldApplyTestParam>({
        {
            .health{100},
            .expectGlowApplied = true
        },
        {
            .health{120},
            .expectGlowApplied = true
        },
        {
            .health{0},
            .expectGlowApplied = false
        },
        {
            .health{std::nullopt},
            .expectGlowApplied = true
        }
    })
));

INSTANTIATE_TEST_SUITE_P(IsControlledByLocalPlayer, PlayerModelGlowShouldApplyTest, testing::ValuesIn(
    std::to_array<PlayerModelGlowShouldApplyTestParam>({
        {
            .isControlledByLocalPlayer = false,
            .expectGlowApplied = true
        },
        {
            .isControlledByLocalPlayer = true,
            .expectGlowApplied = false
        }
    })
));

INSTANTIATE_TEST_SUITE_P(IsTTorCT, PlayerModelGlowShouldApplyTest, testing::ValuesIn(
    std::to_array<PlayerModelGlowShouldApplyTestParam>({
        {
            .isTTorCT = true,
            .expectGlowApplied = true
        },
        {
            .isTTorCT = false,
            .expectGlowApplied = false
        }
    })
));

struct PlayerModelGlowEnemyAllyColorTestParam {
    std::optional<bool> isEnemy;
    std::optional<color::Rgba> enemyColor{};
    std::optional<color::Rgba> allyColor{};
    cs2::Color expectedColor;
};

class PlayerModelGlowEnemyAllyColorTest
    : public PlayerModelGlowTest,
      public testing::WithParamInterface<PlayerModelGlowEnemyAllyColorTestParam> {
};

TEST_P(PlayerModelGlowEnemyAllyColorTest, CorrectColorIsReturned) {
    if (GetParam().enemyColor.has_value())
        mockConfig.expectGetVariable<model_glow_vars::EnemyColor>(model_glow_vars::EnemyColor::ValueType{GetParam().enemyColor.value()});
    if (GetParam().allyColor.has_value())
        mockConfig.expectGetVariable<model_glow_vars::AllyColor>(model_glow_vars::AllyColor::ValueType{GetParam().allyColor.value()});

    EXPECT_CALL(mockPlayerPawn, isEnemy()).WillOnce(testing::Return(GetParam().isEnemy));

    EXPECT_EQ(playerModelGlow.color(mockPlayerPawn), GetParam().expectedColor);
}

INSTANTIATE_TEST_SUITE_P(UnknownIfIsEnemy, PlayerModelGlowEnemyAllyColorTest,
    testing::Values(PlayerModelGlowEnemyAllyColorTestParam{.isEnemy{std::nullopt}, .expectedColor{model_glow_params::kFallbackColor}}));

static_assert(model_glow_vars::EnemyColor::kDefaultValue == color::Rgba{255, 0, 0, 255}, "Update the tests below");
static_assert(model_glow_vars::AllyColor::kDefaultValue == color::Rgba{0, 255, 0, 255}, "Update the tests below");

INSTANTIATE_TEST_SUITE_P(DefaultConfigVars, PlayerModelGlowEnemyAllyColorTest,
    testing::ValuesIn(std::to_array<PlayerModelGlowEnemyAllyColorTestParam>({
        {.isEnemy{true}, .enemyColor{color::Rgba{255, 0, 0, 255}}, .expectedColor{cs2::Color{255, 0, 0}}},
        {.isEnemy{false}, .allyColor{color::Rgba{0, 255, 0, 255}}, .expectedColor{cs2::Color{0, 255, 0}}}
    }))
);

INSTANTIATE_TEST_SUITE_P(NonDefaultConfigVars, PlayerModelGlowEnemyAllyColorTest,
    testing::ValuesIn(std::to_array<PlayerModelGlowEnemyAllyColorTestParam>({
        {.isEnemy{true}, .enemyColor{color::Rgba{10, 20, 30, 200}}, .expectedColor{cs2::Color{10, 20, 30, 200}}},
        {.isEnemy{false}, .allyColor{color::Rgba{40, 50, 60, 90}}, .expectedColor{cs2::Color{40, 50, 60, 90}}}
    }))
);
