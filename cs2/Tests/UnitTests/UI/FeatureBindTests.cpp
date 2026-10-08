#include <gtest/gtest.h>
#include <UI/ImGui/Neverlose/FeatureBindState.h>
#include <Config/ConfigOverrideState.h>

namespace {
class FeatureBindTests : public testing::Test {
protected:
    static inline double value;
    feature_binds::Entry entry;
    void TearDown() override
    {
        config_overrides::values[0] = {};
        config_overrides::restoreBeforeLoad = nullptr;
    }
    void SetUp() override
    {
        value = 70.0;
        entry.key = 1;
        entry.numeric = true;
        entry.minimum = 0;
        entry.maximum = 100;
        entry.boundValue = 25;
        entry.get = [] { return value; };
        entry.set = [](double next) { value = next; return true; };
    }
};
TEST_F(FeatureBindTests, AutoSaveKeepsBaseValueWhileOverrideIsActive)
{
    config_overrides::registerValue(0, &entry.active, &entry.restoreValue);
    entry.update(true);
    EXPECT_EQ(value, 25);
    EXPECT_EQ(config_overrides::valueForSave(0, value), 70);
    entry.update(false);
    entry.update(true);
    EXPECT_EQ(config_overrides::valueForSave(0, value), 70);
}
TEST_F(FeatureBindTests, LoadingAnotherConfigClearsOldOverrideBeforeReadingNewValues)
{
    static feature_binds::Entry* current;
    current = &entry;
    config_overrides::restoreBeforeLoad = [] { current->restore(); };
    entry.update(true);
    config_overrides::restore();
    value = 95; // value supplied by the newly loaded config
    entry.update(false);
    EXPECT_EQ(value, 95);
    EXPECT_FALSE(entry.active);
    entry.update(true);
    EXPECT_EQ(value, 25);
    entry.update(false);
    entry.update(true);
    EXPECT_EQ(value, 95);
}
TEST_F(FeatureBindTests, HoldOverridesAndRestoresOriginalValue)
{
    entry.holdMode = true;
    entry.update(true);
    EXPECT_EQ(value, 25);
    entry.update(true);
    entry.update(false);
    EXPECT_EQ(value, 70);
    EXPECT_FALSE(entry.active);
}
TEST_F(FeatureBindTests, ToggleDoesNotRestoreOnReleaseOrRepeat)
{
    entry.update(true);
    entry.update(true);
    entry.update(false);
    EXPECT_EQ(value, 25);
    entry.update(true);
    EXPECT_EQ(value, 70);
}
TEST_F(FeatureBindTests, UnbindingRestoresActiveOverride)
{
    entry.update(true);
    entry.key = 0;
    entry.update(false);
    EXPECT_EQ(value, 70);
}
TEST_F(FeatureBindTests, RebindingOrChangingModeCanRestoreWithoutAnotherKeyPress)
{
    entry.update(true);
    entry.restore();
    EXPECT_EQ(value, 70);
    EXPECT_FALSE(entry.active);
    EXPECT_FALSE(entry.lastKeyDown);
}
TEST_F(FeatureBindTests, EditingActiveBoundValuePreservesOriginal)
{
    entry.update(true);
    entry.setBoundValue(95);
    EXPECT_EQ(value, 95);
    entry.update(false);
    entry.update(true);
    EXPECT_EQ(value, 70);
}
TEST_F(FeatureBindTests, BoundValueClampsAndRejectsNonfiniteValues)
{
    entry.setBoundValue(500);
    EXPECT_EQ(entry.boundValue, 100);
    entry.setBoundValue(-20);
    EXPECT_EQ(entry.boundValue, 0);
    entry.setBoundValue(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(entry.boundValue, 0);
}
TEST_F(FeatureBindTests, BooleanToggleBehaviorIsPreserved)
{
    entry.numeric = false;
    value = 0;
    entry.update(true);
    EXPECT_EQ(value, 1);
    entry.update(false);
    entry.update(true);
    EXPECT_EQ(value, 0);
}
TEST_F(FeatureBindTests, BooleanHoldRestoresAlreadyEnabledFeature)
{
    entry.numeric = false;
    entry.holdMode = true;
    value = 1;
    entry.update(true);
    entry.update(false);
    EXPECT_EQ(value, 1);
}
}

#include <UI/ImGui/Neverlose/FeatureBindRecord.h>

TEST(FeatureBindRecordTests, LoadsLegacyToggleAndNewNumericFormats)
{
    feature_binds::Record record;
    ASSERT_TRUE(feature_binds::parseRecord("abcdef0123456789 249 1", record, 253));
    EXPECT_EQ(record.id, 0xabcdef0123456789ull);
    EXPECT_EQ(record.key, 249);
    EXPECT_TRUE(record.holdMode);
    EXPECT_FALSE(record.hasValue);
    ASSERT_TRUE(feature_binds::parseRecord("12 17 0 -52.125\r", record, 253));
    EXPECT_TRUE(record.hasValue);
    EXPECT_EQ(record.value, -52.125);
}
TEST(FeatureBindRecordTests, RejectsOverflowAndMalformedFields)
{
    const char* invalid[] = {"", "# comment", "fffffffffffffffff 1 0", "12 99999999999999999999999 0",
        "12 254 0", "12 -1 0", "12 1 2", "12 1", "12 1 0 garbage", "12 1 0 nan",
        "12 1 0 inf", "12 1 0 1e999", "12 1 0 80 trailing"};
    for (auto* line : invalid) {
        feature_binds::Record record;
        EXPECT_FALSE(feature_binds::parseRecord(line, record, 253)) << line;
    }
}

TEST(FeatureBindValueTests, ZeroIsAnOverrideRatherThanAnOffSwitch)
{
    static double value = 80;
    feature_binds::Entry entry;
    entry.key = 1;
    entry.numeric = true;
    entry.boundValue = 0;
    entry.get = [] { return value; };
    entry.set = [](double next) { value = next; return true; };
    entry.update(true);
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(entry.active);
    entry.update(false);
    entry.update(true);
    EXPECT_EQ(value, 80);
}
TEST(FeatureBindValueTests, IntegerTargetsRoundButFloatTargetsKeepDecimals)
{
    feature_binds::Entry entry;
    entry.minimum = -100;
    entry.maximum = 100;
    entry.integral = true;
    entry.setBoundValue(25.8);
    EXPECT_EQ(entry.boundValue, 26);
    entry.integral = false;
    entry.setBoundValue(-25.8);
    EXPECT_EQ(entry.boundValue, -25.8);
}
