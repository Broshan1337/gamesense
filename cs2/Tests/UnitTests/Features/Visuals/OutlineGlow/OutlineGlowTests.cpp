#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <Features/Visuals/OutlineGlow/OutlineGlow.h>
#include <Mocks/MockConfig.h>
#include <Mocks/MockGlowProperty.h>
#include <Mocks/MockHookContext.h>
#include <Mocks/MockModelEntity.h>
#include <Mocks/MockPlayerController.h>
#include <Mocks/MockSmokeGrenadeProjectile.h>

class OutlineGlowTest : public testing::Test {
protected:
    testing::StrictMock<MockHookContext> mockHookContext;
    testing::StrictMock<MockConfig> mockConfig;
    testing::StrictMock<MockBaseEntity> mockBaseEntity;
    testing::StrictMock<MockModelEntity> mockModelEntity;
    testing::StrictMock<MockGlowProperty> mockGlowProperty;
    testing::StrictMock<MockPlayerPawn> mockPlayerPawn;
    testing::StrictMock<MockPlantedC4> mockPlantedC4;
    OutlineGlow<MockHookContext> outlineGlow{mockHookContext};
};

struct OutlineGlowInactiveTestParam {
    bool outlineGlowEnabled{};
    bool glowEnabled{};
    bool isGlowAppliedByTheGame{false};
};

class OutlineGlowInactiveTest
    : public OutlineGlowTest,
      public testing::WithParamInterface<OutlineGlowInactiveTestParam> {
protected:
    OutlineGlowInactiveTest()
    {
        EXPECT_CALL(mockHookContext, config()).WillRepeatedly(testing::ReturnRef(mockConfig));
        mockConfig.expectGetVariable<outline_glow_vars::Enabled>().WillRepeatedly(testing::Return(GetParam().outlineGlowEnabled));
        if (GetParam().isGlowAppliedByTheGame) {
            EXPECT_CALL(mockBaseEntity, asModelEntity()).WillRepeatedly(testing::ReturnRef(mockModelEntity));
            EXPECT_CALL(mockModelEntity, glowProperty()).WillOnce(testing::ReturnRef(mockGlowProperty));
            EXPECT_CALL(mockGlowProperty, isGlowing()).WillOnce(testing::Return(true));
        }
    }
};

TEST_P(OutlineGlowInactiveTest, DefuseKit) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowDefuseKits>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(DefuseKitOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::CBaseAnimGraph>()});
}

TEST_P(OutlineGlowInactiveTest, DroppedBomb) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowDroppedBomb>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(DroppedBombOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_C4>()});
}

TEST_P(OutlineGlowInactiveTest, GrenadeProjectile) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowGrenadeProjectiles>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(GrenadeProjectileOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_FlashbangProjectile>()});
}

TEST_P(OutlineGlowInactiveTest, Hostage) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowHostages>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(HostageOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_Hostage>()});
}

TEST_P(OutlineGlowInactiveTest, Player) {
    EXPECT_CALL(mockPlayerPawn, baseEntity()).WillRepeatedly(testing::ReturnRef(mockBaseEntity));

    mockConfig.expectGetVariable<outline_glow_vars::GlowPlayers>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(PlayerOutlineGlow{mockHookContext}, mockPlayerPawn, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_CSPlayerPawn>()});
}

TEST_P(OutlineGlowInactiveTest, TickingBomb) {
    EXPECT_CALL(mockPlantedC4, baseEntity()).WillRepeatedly(testing::ReturnRef(mockBaseEntity));

    mockConfig.expectGetVariable<outline_glow_vars::GlowTickingBomb>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(TickingBombOutlineGlow{mockHookContext}, mockPlantedC4, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::CPlantedC4>()});
}

TEST_P(OutlineGlowInactiveTest, Weapon) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowWeapons>().WillRepeatedly(testing::Return(GetParam().glowEnabled));
    outlineGlow.applyGlow()(WeaponOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_DEagle>()});
}

INSTANTIATE_TEST_SUITE_P(, OutlineGlowInactiveTest, testing::ValuesIn(
    std::to_array<OutlineGlowInactiveTestParam>({
        {.outlineGlowEnabled = false, .glowEnabled = false},
        {.outlineGlowEnabled = true, .glowEnabled = false},
        {.outlineGlowEnabled = false, .glowEnabled = true},
        {.outlineGlowEnabled = true, .glowEnabled = true, .isGlowAppliedByTheGame = true}
    })
));

class OutlineGlowActiveTest : public OutlineGlowTest {
protected:
    OutlineGlowActiveTest()
    {
        EXPECT_CALL(mockHookContext, config()).WillRepeatedly(testing::ReturnRef(mockConfig));
        mockConfig.expectGetVariable<outline_glow_vars::Enabled>(true);
        EXPECT_CALL(mockBaseEntity, asModelEntity()).WillOnce(testing::ReturnRef(mockModelEntity));
        EXPECT_CALL(mockModelEntity, glowProperty()).WillOnce(testing::ReturnRef(mockGlowProperty));
        EXPECT_CALL(mockGlowProperty, isGlowing()).WillOnce(testing::Return(false));
    }
};

TEST_F(OutlineGlowActiveTest, DefuseKit) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowDefuseKits>(true);
    mockConfig.expectGetVariable<outline_glow_vars::DefuseKitColor>(outline_glow_vars::DefuseKitColor::ValueType{color::Rgba{0, 213, 255, 255}});
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{0, 213, 255}, outline_glow_params::kDefuseKitGlowRange));
    outlineGlow.applyGlow()(DefuseKitOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::CBaseAnimGraph>()});
}

TEST_F(OutlineGlowActiveTest, DroppedBomb) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowDroppedBomb>(true);
    mockConfig.expectGetVariable<outline_glow_vars::DroppedBombColor>(outline_glow_vars::DroppedBombColor::ValueType{color::Rgba{255, 213, 77, 255}});
    EXPECT_CALL(mockBaseEntity, hasOwner()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{255, 213, 77}, 0));
    outlineGlow.applyGlow()(DroppedBombOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_C4>()});
}

TEST_F(OutlineGlowActiveTest, GrenadeProjectile) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowGrenadeProjectiles>(true);
    mockConfig.expectGetVariable<outline_glow_vars::FlashbangColor>(outline_glow_vars::FlashbangColor::ValueType{color::Rgba{64, 131, 255, 255}});
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{64, 131, 255, 255}, 0));
    outlineGlow.applyGlow()(GrenadeProjectileOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_FlashbangProjectile>()});
}

TEST_F(OutlineGlowActiveTest, Hostage) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowHostages>(true);
    mockConfig.expectGetVariable<outline_glow_vars::HostageColor>(outline_glow_vars::HostageColor::ValueType{color::Rgba{255, 200, 50, 255}});
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{255, 200, 50, 255}, 0));
    outlineGlow.applyGlow()(HostageOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_Hostage>()});
}

TEST_F(OutlineGlowActiveTest, Player) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowPlayers>(true);
    mockConfig.expectGetVariable<outline_glow_vars::GlowOnlyEnemies>(false);
    mockConfig.expectGetVariable<outline_glow_vars::EnemyColor>(outline_glow_vars::EnemyColor::ValueType{color::Rgba{255, 0, 165, outline_glow_params::kGlowAlpha}});

    EXPECT_CALL(mockPlayerPawn, isAlive()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, health()).WillOnce(testing::Return(100));
    EXPECT_CALL(mockPlayerPawn, isControlledByLocalPlayer()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockPlayerPawn, isTTorCT()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, isEnemy()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, hasImmunity()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockPlayerPawn, baseEntity()).WillRepeatedly(testing::ReturnRef(mockBaseEntity));

    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{255, 0, 165, outline_glow_params::kGlowAlpha}, 0));
    outlineGlow.applyGlow()(PlayerOutlineGlow{mockHookContext}, mockPlayerPawn, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_CSPlayerPawn>()});
}

TEST_F(OutlineGlowActiveTest, TickingBomb) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowTickingBomb>(true);
    mockConfig.expectGetVariable<outline_glow_vars::TickingBombColor>(outline_glow_vars::TickingBombColor::ValueType{color::Rgba{255, 0, 0, 255}});
    EXPECT_CALL(mockPlantedC4, baseEntity()).WillRepeatedly(testing::ReturnRef(mockBaseEntity));
    EXPECT_CALL(mockPlantedC4, isTicking()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{255, 0, 0}, 0));
    outlineGlow.applyGlow()(TickingBombOutlineGlow{mockHookContext}, mockPlantedC4, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::CPlantedC4>()});
}

TEST_F(OutlineGlowActiveTest, Weapon) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowWeapons>(true);
    EXPECT_CALL(mockBaseEntity, hasOwner()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(outline_glow_params::kFallbackColor, outline_glow_params::kWeaponGlowRange));
    outlineGlow.applyGlow()(WeaponOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_DEagle>()});
}

class OutlineGlowNonDefaultAlphaTest : public OutlineGlowActiveTest {
};

TEST_F(OutlineGlowNonDefaultAlphaTest, ImmunePlayerHasCorrectAlpha) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowPlayers>(true);
    mockConfig.expectGetVariable<outline_glow_vars::GlowOnlyEnemies>(true);
    mockConfig.expectGetVariable<outline_glow_vars::EnemyColor>(outline_glow_vars::EnemyColor::ValueType{color::Rgba{127, 255, 127, outline_glow_params::kGlowAlpha}});

    EXPECT_CALL(mockPlayerPawn, isAlive()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, health()).WillOnce(testing::Return(100));
    EXPECT_CALL(mockPlayerPawn, isControlledByLocalPlayer()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockPlayerPawn, isTTorCT()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, isEnemy()).Times(2).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, hasImmunity()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, baseEntity()).WillRepeatedly(testing::ReturnRef(mockBaseEntity));

    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{127, 255, 127, outline_glow_params::kImmunePlayerGlowAlpha}, 0));
    outlineGlow.applyGlow()(PlayerOutlineGlow{mockHookContext}, mockPlayerPawn, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_CSPlayerPawn>()});
}

TEST_F(OutlineGlowNonDefaultAlphaTest, ImmunePlayerHasCorrectFallbackColorAlpha) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowPlayers>(true);
    mockConfig.expectGetVariable<outline_glow_vars::GlowOnlyEnemies>(true);

    EXPECT_CALL(mockPlayerPawn, isAlive()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, health()).WillOnce(testing::Return(100));
    EXPECT_CALL(mockPlayerPawn, isControlledByLocalPlayer()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockPlayerPawn, isTTorCT()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, isEnemy()).Times(2).WillRepeatedly(testing::Return(std::nullopt));
    EXPECT_CALL(mockPlayerPawn, hasImmunity()).WillOnce(testing::Return(true));
    EXPECT_CALL(mockPlayerPawn, baseEntity()).WillRepeatedly(testing::ReturnRef(mockBaseEntity));

    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{191, 191, 191, outline_glow_params::kImmunePlayerGlowAlpha}, 0));
    outlineGlow.applyGlow()(PlayerOutlineGlow{mockHookContext}, mockPlayerPawn, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_CSPlayerPawn>()});
}

class OutlineGlowNonDefaultRangeTest : public OutlineGlowActiveTest {
};

TEST_F(OutlineGlowNonDefaultRangeTest, NonDefaultGlowRangeIsUsedForDefuseKits) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowDefuseKits>(true);
    mockConfig.expectGetVariable<outline_glow_vars::DefuseKitColor>(outline_glow_vars::DefuseKitColor::ValueType{color::Rgba{0, 213, 255, 255}});

    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{0, 213, 255, 255}, outline_glow_params::kDefuseKitGlowRange));
    outlineGlow.applyGlow()(DefuseKitOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::CBaseAnimGraph>()});
}

TEST_F(OutlineGlowNonDefaultRangeTest, NonDefaultGlowRangeIsUsedForWeapons) {
    mockConfig.expectGetVariable<outline_glow_vars::GlowWeapons>(true);
    mockConfig.expectGetVariable<outline_glow_vars::MolotovColor>(outline_glow_vars::MolotovColor::ValueType{color::Rgba{255, 128, 0, 255}});

    EXPECT_CALL(mockBaseEntity, hasOwner()).WillOnce(testing::Return(false));
    EXPECT_CALL(mockBaseEntity, applyGlowRecursively(cs2::Color{255, 128, 0, 255}, outline_glow_params::kWeaponGlowRange));
    outlineGlow.applyGlow()(WeaponOutlineGlow{mockHookContext}, mockBaseEntity, EntityTypeInfo{EntityTypeInfo::indexOf<cs2::C_MolotovGrenade>()});
}
