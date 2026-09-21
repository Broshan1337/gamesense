#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <Features/Visuals/OutlineGlow/PlayerOutlineGlow/PlayerOutlineGlow.h>
#include <Mocks/MockConfig.h>
#include <Mocks/MockHookContext.h>
#include <Mocks/MockPlayerController.h>
#include <Mocks/MockPlayerPawn.h>

class PlayerOutlineGlowTest : public testing::Test {
protected:
    PlayerOutlineGlowTest()
    {
        EXPECT_CALL(mockHookContext, config()).WillRepeatedly(testing::ReturnRef(mockConfig));
    }

    testing::StrictMock<MockHookContext> mockHookContext;
    testing::StrictMock<MockConfig> mockConfig;
    testing::StrictMock<MockPlayerPawn> mockPlayerPawn;
    testing::StrictMock<MockPlayerController> mockPlayerController;

    PlayerOutlineGlow<MockHookContext> playerOutlineGlow{mockHookContext};
};

TEST_F(PlayerOutlineGlowTest, Disabled) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowPlayers>(false);
    EXPECT_FALSE(playerOutlineGlow.enabled());
}

TEST_F(PlayerOutlineGlowTest, Enabled) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowPlayers>(true);
    EXPECT_TRUE(playerOutlineGlow.enabled());
}

struct PlayerOutlineGlowConditionTestParam {
    bool onlyEnemies{true};
    std::optional<bool> isAlive{true};
    Optional<int> health{100};
    bool isControlledByLocalPlayer{false};
    bool isTTorCT{true};
    std::optional<bool> isEnemy{true};
    bool expectEnemyCheck{true};
    bool expectGlowApplied{true};
};

class PlayerOutlineGlowConditionTest
    : public PlayerOutlineGlowTest,
      public testing::WithParamInterface<PlayerOutlineGlowConditionTestParam> {
};

TEST_P(PlayerOutlineGlowConditionTest, GlowIsAppliedWhenExpected) {
    EXPECT_CALL(mockConfig, getVariable(ConfigVariableTypes::indexOf<outline_glow_vars::GlowOnlyEnemies>()))
        .WillRepeatedly(testing::Return(GetParam().onlyEnemies));

    EXPECT_CALL(mockPlayerPawn, isAlive()).WillRepeatedly(testing::Return(GetParam().isAlive));
    EXPECT_CALL(mockPlayerPawn, health()).WillRepeatedly(testing::Return(GetParam().health));
    EXPECT_CALL(mockPlayerPawn, isControlledByLocalPlayer()).WillRepeatedly(testing::Return(GetParam().isControlledByLocalPlayer));
    EXPECT_CALL(mockPlayerPawn, isTTorCT()).WillRepeatedly(testing::Return(GetParam().isTTorCT));

    if (GetParam().expectEnemyCheck)
        EXPECT_CALL(mockPlayerPawn, isEnemy()).WillRepeatedly(testing::Return(GetParam().isEnemy));

    EXPECT_EQ(playerOutlineGlow.shouldApplyGlow(EntityTypeInfo{}, mockPlayerPawn), GetParam().expectGlowApplied);
}

INSTANTIATE_TEST_SUITE_P(OnlyEnemies, PlayerOutlineGlowConditionTest, testing::ValuesIn(
    std::to_array<PlayerOutlineGlowConditionTestParam>({
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

INSTANTIATE_TEST_SUITE_P(NotOnlyEnemies, PlayerOutlineGlowConditionTest, testing::ValuesIn(
    std::to_array<PlayerOutlineGlowConditionTestParam>({
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

INSTANTIATE_TEST_SUITE_P(IsAlive, PlayerOutlineGlowConditionTest, testing::ValuesIn(
    std::to_array<PlayerOutlineGlowConditionTestParam>({
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

INSTANTIATE_TEST_SUITE_P(Health, PlayerOutlineGlowConditionTest, testing::ValuesIn(
    std::to_array<PlayerOutlineGlowConditionTestParam>({
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

INSTANTIATE_TEST_SUITE_P(IsControlledByLocalPlayer, PlayerOutlineGlowConditionTest, testing::ValuesIn(
    std::to_array<PlayerOutlineGlowConditionTestParam>({
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

INSTANTIATE_TEST_SUITE_P(IsTTorCT, PlayerOutlineGlowConditionTest, testing::ValuesIn(
    std::to_array<PlayerOutlineGlowConditionTestParam>({
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

struct PlayerOutlineGlowEnemyAllyColorTestParam {
    std::optional<bool> isEnemy;
    std::optional<color::Rgba> enemyColor{};
    std::optional<color::Rgba> allyColor{};
    Optional<bool> hasImmunity{false};
    cs2::Color expectedColor;
};

class PlayerOutlineGlowEnemyAllyColorTest
    : public PlayerOutlineGlowTest,
      public testing::WithParamInterface<PlayerOutlineGlowEnemyAllyColorTestParam> {
};

TEST_P(PlayerOutlineGlowEnemyAllyColorTest, CorrectColorIsReturned) {
    if (GetParam().enemyColor.has_value())
        mockConfig.expectGetVariable<outline_glow_vars::EnemyColor>(outline_glow_vars::EnemyColor::ValueType{GetParam().enemyColor.value()});
    if (GetParam().allyColor.has_value())
        mockConfig.expectGetVariable<outline_glow_vars::AllyColor>(outline_glow_vars::AllyColor::ValueType{GetParam().allyColor.value()});

    EXPECT_CALL(mockPlayerPawn, isEnemy()).WillOnce(testing::Return(GetParam().isEnemy));
    EXPECT_CALL(mockPlayerPawn, hasImmunity()).WillRepeatedly(testing::Return(GetParam().hasImmunity));

    EXPECT_EQ(playerOutlineGlow.color(EntityTypeInfo{}, mockPlayerPawn), GetParam().expectedColor);
}

INSTANTIATE_TEST_SUITE_P(UnknownIfIsEnemy, PlayerOutlineGlowEnemyAllyColorTest,
    testing::Values(PlayerOutlineGlowEnemyAllyColorTestParam{
        .isEnemy{std::nullopt},
        .expectedColor{outline_glow_params::kFallbackColor.setAlpha(outline_glow_params::kGlowAlpha)}})
);

static_assert(outline_glow_vars::EnemyColor::kDefaultValue == color::Rgba{255, 0, 0, outline_glow_params::kGlowAlpha}, "Update the tests below");
static_assert(outline_glow_vars::AllyColor::kDefaultValue == color::Rgba{0, 255, 0, outline_glow_params::kGlowAlpha}, "Update the tests below");

INSTANTIATE_TEST_SUITE_P(DefaultConfigVars, PlayerOutlineGlowEnemyAllyColorTest,
    testing::ValuesIn(std::to_array<PlayerOutlineGlowEnemyAllyColorTestParam>({
        {.isEnemy{true}, .enemyColor{color::Rgba{255, 0, 0, outline_glow_params::kGlowAlpha}}, .expectedColor{cs2::Color{255, 0, 0, outline_glow_params::kGlowAlpha}}},
        {.isEnemy{false}, .allyColor{color::Rgba{0, 255, 0, outline_glow_params::kGlowAlpha}}, .expectedColor{cs2::Color{0, 255, 0, outline_glow_params::kGlowAlpha}}}
    }))
);

static_assert(outline_glow_params::kImmunePlayerGlowAlpha < outline_glow_params::kGlowAlpha, "Immunity must dim the glow");

INSTANTIATE_TEST_SUITE_P(ImmunePlayer, PlayerOutlineGlowEnemyAllyColorTest,
    testing::Values(PlayerOutlineGlowEnemyAllyColorTestParam{
        .isEnemy{true},
        .enemyColor{color::Rgba{255, 0, 0, outline_glow_params::kGlowAlpha}},
        .hasImmunity{true},
        .expectedColor{cs2::Color{255, 0, 0, outline_glow_params::kImmunePlayerGlowAlpha}}})
);

INSTANTIATE_TEST_SUITE_P(NonDefaultConfigVars, PlayerOutlineGlowEnemyAllyColorTest,
    testing::ValuesIn(std::to_array<PlayerOutlineGlowEnemyAllyColorTestParam>({
        {.isEnemy{true}, .enemyColor{color::Rgba{10, 20, 30, 200}}, .expectedColor{cs2::Color{10, 20, 30, 200}}},
        {.isEnemy{false}, .allyColor{color::Rgba{40, 50, 60, 90}}, .expectedColor{cs2::Color{40, 50, 60, 90}}}
    }))
);

struct PlayerOutlineGlowAlphaTestParam {
    Optional<bool> hasImmunity{};
    std::uint8_t expectedAlpha{};
};

class PlayerOutlineGlowAlphaTest
    : public PlayerOutlineGlowTest,
      public testing::WithParamInterface<PlayerOutlineGlowAlphaTestParam> {
};

TEST_P(PlayerOutlineGlowAlphaTest, CorrectGlowColorAlphaIsReturned) {
    EXPECT_CALL(mockPlayerPawn, hasImmunity()).WillOnce(testing::Return(GetParam().hasImmunity));
    EXPECT_EQ(playerOutlineGlow.getGlowColorAlpha(mockPlayerPawn), GetParam().expectedAlpha);
}

INSTANTIATE_TEST_SUITE_P(, PlayerOutlineGlowAlphaTest,
    testing::ValuesIn(std::to_array<PlayerOutlineGlowAlphaTestParam>({
        {.hasImmunity{std::nullopt}, .expectedAlpha = outline_glow_params::kGlowAlpha},
        {.hasImmunity{false}, .expectedAlpha = outline_glow_params::kGlowAlpha},
        {.hasImmunity{true}, .expectedAlpha = outline_glow_params::kImmunePlayerGlowAlpha}
    }))
);
