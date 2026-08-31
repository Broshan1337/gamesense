#include <cstddef>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include <Config/ConfigParams.h>
#include <Config/ConfigSchema.h>
#include <Config/ConfigVariableTypes.h>

#include <Mocks/MockConfig.h>
#include <Mocks/MockConfigConversion.h>
#include <Mocks/MockHookContext.h>

class ConfigSchemaTest : public testing::Test {
protected:
    testing::StrictMock<MockHookContext> mockHookContext;
    testing::StrictMock<MockConfigConversion> mockConfigConversion;
    testing::StrictMock<MockConfig> mockConfig;

    ConfigSchema<MockHookContext> configSchema{mockHookContext};
    using IndexInNestingLevel = std::size_t;
    std::vector<IndexInNestingLevel> nestingLevels;
    std::set<std::size_t> configVariableIndexes;

    static inline std::vector<std::string> conversionPath;

    [[nodiscard]] static std::string joinPath(const char8_t* id)
    {
        std::string result;
        for (const auto& part : conversionPath) {
            result += part;
            result += '.';
        }
        if (id) result += reinterpret_cast<const char*>(id);
        return result;
    }

    // Conversions kept in the schema as parse-and-discard placeholders for backward compatibility
    // with old config files (see ConfigSchema.h). Their load/save lambdas are intentional no-ops,
    // so they must not demand a MockConfig call. Matched by full object path because some of these
    // ids collide with legitimate keys elsewhere (e.g. Aimbot.Fov vs LegitAimbot.Fov).
    static const inline std::set<std::string> discardOnlyPaths{
        "Combat.Aimbot.Fov",
        "Combat.Aimbot.DrawFov",
        "Combat.Aimbot.FovCircleHue",
        "Combat.LegitAimbot.FovCircleHue",
        "Combat.Aimbot.SeedCorrectionMode",
        "Visuals.ViewmodelMod.Enabled",
        "Visuals.PlayerList.PosX",
        "Visuals.PlayerList.PosY",
        "Sound.Visualizations.Chams.Enabled",
        "Sound.Visualizations.ImpactMarkers.Enabled",
        "Sound.Visualizations.BulletTracers.Enabled",
        "Sound.Visualizations.GrenadeTrajectory.Enabled",
        "Sound.Visualizations.OffScreenArrows.Enabled",
        "Visuals.ModelGlow.Players.ColorMode",
        "Visuals.ModelGlow.Hues.PlayerBlue",
        "Visuals.ModelGlow.Hues.PlayerGreen",
        "Visuals.ModelGlow.Hues.PlayerYellow",
        "Visuals.ModelGlow.Hues.PlayerOrange",
        "Visuals.ModelGlow.Hues.PlayerPurple",
        "Visuals.ModelGlow.Hues.TeamT",
        "Visuals.ModelGlow.Hues.TeamCT",
        "Visuals.ModelGlow.Hues.LowHealth",
        "Visuals.ModelGlow.Hues.HighHealth",
        "Visuals.ModelGlow.Hues.Enemy",
        "Visuals.ModelGlow.Hues.Ally",
        "Visuals.OutlineGlow.Players.ColorMode",
        "Visuals.OutlineGlow.Hues.PlayerBlue",
        "Visuals.OutlineGlow.Hues.PlayerGreen",
        "Visuals.OutlineGlow.Hues.PlayerYellow",
        "Visuals.OutlineGlow.Hues.PlayerOrange",
        "Visuals.OutlineGlow.Hues.PlayerPurple",
        "Visuals.OutlineGlow.Hues.TeamT",
        "Visuals.OutlineGlow.Hues.TeamCT",
        "Visuals.OutlineGlow.Hues.LowHealth",
        "Visuals.OutlineGlow.Hues.HighHealth",
        "Visuals.OutlineGlow.Hues.Enemy",
        "Visuals.OutlineGlow.Hues.Ally"};
};

TEST_F(ConfigSchemaTest, SchemaIsValid) {
    EXPECT_CALL(mockConfigConversion, beginRoot()).WillOnce(testing::Invoke([this] {
        EXPECT_EQ(nestingLevels.size(), 0);
        EXPECT_LE(nestingLevels.size(), config_params::kMaxNestingLevel);
        nestingLevels.push_back(IndexInNestingLevel{});
    }));

    EXPECT_CALL(mockConfigConversion, endRoot()).WillOnce(testing::Invoke([this] {
        EXPECT_EQ(nestingLevels.size(), 1);
        nestingLevels.clear();
    }));

    EXPECT_CALL(mockConfigConversion, beginObject(testing::_)).WillRepeatedly(testing::Invoke([this] {
        EXPECT_GT(nestingLevels.size(), 0);
        EXPECT_LE(nestingLevels.size(), config_params::kMaxNestingLevel);
        nestingLevels.push_back(IndexInNestingLevel{});
    }));

    EXPECT_CALL(mockConfigConversion, endObject()).WillRepeatedly(testing::Invoke([this] {
        EXPECT_GT(nestingLevels.size(), 1);
        nestingLevels.pop_back();
        EXPECT_LT(nestingLevels.back(), config_params::kMaxObjectIndex);
        ++nestingLevels.back();
    }));

    EXPECT_CALL(mockConfigConversion, boolean(testing::_, testing::_, testing::_)).WillRepeatedly(testing::Invoke([this] {
        EXPECT_GT(nestingLevels.size(), 0);
        EXPECT_LT(nestingLevels.back(), config_params::kMaxObjectIndex);
        ++nestingLevels.back();
    }));

    EXPECT_CALL(mockConfigConversion, uint(testing::_, testing::_, testing::_)).WillRepeatedly(testing::Invoke([this] {
        EXPECT_GT(nestingLevels.size(), 0);
        EXPECT_LT(nestingLevels.back(), config_params::kMaxObjectIndex);
        ++nestingLevels.back();
    }));

    EXPECT_CALL(mockConfigConversion, floating(testing::_, testing::_, testing::_)).WillRepeatedly(testing::Invoke([this] {
        EXPECT_GT(nestingLevels.size(), 0);
        EXPECT_LT(nestingLevels.back(), config_params::kMaxObjectIndex);
        ++nestingLevels.back();
    }));

    configSchema.performConversion(mockConfigConversion);
}

TEST_F(ConfigSchemaTest, EachConfigVariableIsLoadedOnce) {
    EXPECT_CALL(mockConfigConversion, beginRoot());
    EXPECT_CALL(mockConfigConversion, endRoot());
    EXPECT_CALL(mockConfigConversion, beginObject(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([this](const char8_t* id) { conversionPath.emplace_back(reinterpret_cast<const char*>(id)); }));
    EXPECT_CALL(mockConfigConversion, endObject()).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([this] { conversionPath.pop_back(); }));

    EXPECT_CALL(mockHookContext, config()).WillRepeatedly(testing::ReturnRef(mockConfig));

    EXPECT_CALL(mockConfigConversion, boolean(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::WithArgs<0, 1>(testing::Invoke([this](const char8_t* id, auto valueSetter) {
            if (!id || discardOnlyPaths.contains(joinPath(id)))
                return;
            EXPECT_CALL(mockConfig, setVariableWithoutAutoSave(testing::_, testing::_))
                .WillOnce(testing::WithArg<0>(testing::Invoke([this](std::size_t configVariableIndex) {
                    EXPECT_FALSE(configVariableIndexes.contains(configVariableIndex));
                    configVariableIndexes.insert(configVariableIndex);
                })));
            valueSetter(bool{});
        })));

    EXPECT_CALL(mockConfigConversion, uint(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::WithArgs<0, 1>(testing::Invoke([this](const char8_t* id, auto valueSetter) {
            if (!id || discardOnlyPaths.contains(joinPath(id)))
                return;
            EXPECT_CALL(mockConfig, setVariableWithoutAutoSave(testing::_, testing::_))
                .WillOnce(testing::WithArg<0>(testing::Invoke([this](std::size_t configVariableIndex) {
                    EXPECT_FALSE(configVariableIndexes.contains(configVariableIndex));
                    configVariableIndexes.insert(configVariableIndex);
                })));
            valueSetter(std::uint64_t{});
        })));

    EXPECT_CALL(mockConfigConversion, floating(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::WithArgs<0, 1>(testing::Invoke([this](const char8_t* id, auto valueSetter) {
            if (!id || discardOnlyPaths.contains(joinPath(id)))
                return;
            EXPECT_CALL(mockConfig, setVariableWithoutAutoSave(testing::_, testing::_))
                .WillOnce(testing::WithArg<0>(testing::Invoke([this](std::size_t configVariableIndex) {
                    EXPECT_FALSE(configVariableIndexes.contains(configVariableIndex));
                    configVariableIndexes.insert(configVariableIndex);
                })));
            valueSetter(float{});
        })));

    configSchema.performConversion(mockConfigConversion);
    EXPECT_EQ(configVariableIndexes.size(), ConfigVariableTypes::size());
}

TEST_F(ConfigSchemaTest, EachConfigVariableIsSavedOnce) {
    EXPECT_CALL(mockConfigConversion, beginRoot());
    EXPECT_CALL(mockConfigConversion, endRoot());
    EXPECT_CALL(mockConfigConversion, beginObject(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([this](const char8_t* id) { conversionPath.emplace_back(reinterpret_cast<const char*>(id)); }));
    EXPECT_CALL(mockConfigConversion, endObject()).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([this] { conversionPath.pop_back(); }));

    EXPECT_CALL(mockHookContext, config()).WillRepeatedly(testing::ReturnRef(mockConfig));

    EXPECT_CALL(mockConfigConversion, boolean(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::WithArgs<0, 2>(testing::Invoke([this](const char8_t* id, auto valueGetter) {
            if (!id || discardOnlyPaths.contains(joinPath(id)))
                return;
            EXPECT_CALL(mockConfig, getVariable(testing::_))
                .WillOnce(testing::WithArg<0>(testing::Invoke([this](std::size_t configVariableIndex) {
                    EXPECT_FALSE(configVariableIndexes.contains(configVariableIndex));
                    configVariableIndexes.insert(configVariableIndex);
                    return std::any{};
            })));
            valueGetter();
        })));

    EXPECT_CALL(mockConfigConversion, uint(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::WithArgs<0, 2>(testing::Invoke([this](const char8_t* id, auto valueGetter) {
            if (!id || discardOnlyPaths.contains(joinPath(id)))
                return;
            EXPECT_CALL(mockConfig, getVariable(testing::_))
                .WillOnce(testing::WithArg<0>(testing::Invoke([this](std::size_t configVariableIndex) {
                    EXPECT_FALSE(configVariableIndexes.contains(configVariableIndex));
                    configVariableIndexes.insert(configVariableIndex);
                    return std::any{};
            })));
            valueGetter();
        })));

    EXPECT_CALL(mockConfigConversion, floating(testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::WithArgs<0, 2>(testing::Invoke([this](const char8_t* id, auto valueGetter) {
            if (!id || discardOnlyPaths.contains(joinPath(id)))
                return;
            EXPECT_CALL(mockConfig, getVariable(testing::_))
                .WillOnce(testing::WithArg<0>(testing::Invoke([this](std::size_t configVariableIndex) {
                    EXPECT_FALSE(configVariableIndexes.contains(configVariableIndex));
                    configVariableIndexes.insert(configVariableIndex);
                    return std::any{};
            })));
            valueGetter();
        })));

    configSchema.performConversion(mockConfigConversion);
    EXPECT_EQ(configVariableIndexes.size(), ConfigVariableTypes::size());
}
