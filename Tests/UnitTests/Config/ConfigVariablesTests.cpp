#include <type_traits>

#include <Utils/ColorUtils.h>

#include <gtest/gtest.h>
#include <Config/ConfigVariables.h>

class ConfigVariablesTest : public testing::Test {
public:
    template <typename ConfigVariable>
    [[nodiscard]] static constexpr auto getNonDefaultValue()
    {
        if constexpr (std::is_same_v<typename ConfigVariable::ValueType, bool>)
            return !ConfigVariable::kDefaultValue;
        else if constexpr (std::is_enum_v<typename ConfigVariable::ValueType>)
            return typename ConfigVariable::ValueType{static_cast<std::underlying_type_t<typename ConfigVariable::ValueType>>(ConfigVariable::kDefaultValue) + 1};
        else if constexpr (IsRangeConstrained<typename ConfigVariable::ValueType>::value) {
            if (ConfigVariable::kDefaultValue != ConfigVariable::ValueType::kMin)
                return typename ConfigVariable::ValueType{ConfigVariable::ValueType::kMin};
            return typename ConfigVariable::ValueType{ConfigVariable::ValueType::kMax};
        } else if constexpr (std::is_same_v<typename ConfigVariable::ValueType, color::Rgba>) {
            static_assert(color::Rgba{0x12u, 0x34u, 0x56u, 0xABu} != ConfigVariable::kDefaultValue,
                          "Rgba test value must differ from every Rgba config var's default");
            return color::Rgba{0x12u, 0x34u, 0x56u, 0xABu};
        } else if constexpr (std::is_integral_v<typename ConfigVariable::ValueType>) {
            return static_cast<typename ConfigVariable::ValueType>(ConfigVariable::kDefaultValue + 1);
        } else
            static_assert(!std::is_same_v<ConfigVariable, ConfigVariable>, "Unsupported type");
    }

    ConfigVariables configVariables;
};

TEST_F(ConfigVariablesTest, EachVariableHasDefaultValueAfterConstruction) {
    ConfigVariableTypes::forEach([this] <typename ConfigVariable> (std::type_identity<ConfigVariable>) {
        EXPECT_EQ(configVariables.getVariableValue<ConfigVariable>(), ConfigVariable::kDefaultValue);
    });
}

TEST_F(ConfigVariablesTest, EachVariableCanBeSetToNonDefaultValue) {
    ConfigVariableTypes::forEach([this] <typename ConfigVariable> (std::type_identity<ConfigVariable>) {
        constexpr auto kNonDefaultValue{getNonDefaultValue<ConfigVariable>()};
        configVariables.storeVariableValue<ConfigVariable>(kNonDefaultValue);
        EXPECT_EQ(configVariables.getVariableValue<ConfigVariable>(), kNonDefaultValue);
    });
}

TEST_F(ConfigVariablesTest, ChangingVariableValueDoesNotAffectOtherVariables) {
    ConfigVariableTypes::forEach([this] <typename ConfigVariable> (std::type_identity<ConfigVariable>) {
        configVariables.storeVariableValue<ConfigVariable>(getNonDefaultValue<ConfigVariable>());
    });

    ConfigVariableTypes::forEach([this] <typename ConfigVariable> (std::type_identity<ConfigVariable>) {
        EXPECT_EQ(configVariables.getVariableValue<ConfigVariable>(), getNonDefaultValue<ConfigVariable>());
    });
}

TEST(RgbaTest, PacksChannelsIntoPackedRepresentation) {
    constexpr color::Rgba color{0x12u, 0x34u, 0x56u, 0xABu};
    constexpr std::uint32_t packed{0x123456ABu};
    EXPECT_EQ(static_cast<std::uint32_t>(color), 0x123456ABu);
    EXPECT_EQ(color.r(), 0x12);
    EXPECT_EQ(color.g(), 0x34);
    EXPECT_EQ(color.b(), 0x56);
    EXPECT_EQ(color.a(), 0xAB);
    EXPECT_EQ(color, color::Rgba{packed});
}

TEST(RgbaTest, ConstructsFromPackedRepresentation) {
    constexpr color::Rgba color{color::Rgba{0x123456ABu}};
    constexpr color::Rgba expected{0x12u, 0x34u, 0x56u, 0xABu};
    EXPECT_EQ(color.r(), 0x12);
    EXPECT_EQ(color.g(), 0x34);
    EXPECT_EQ(color.b(), 0x56);
    EXPECT_EQ(color.a(), 0xAB);
    EXPECT_EQ(color, expected);
}
