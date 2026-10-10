#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Config/Config.h>
#include <Mocks/MockMemoryAllocator.h>

namespace {
struct Context;
struct NoChanges { explicit NoChanges(Context&) {} };
struct Directory { std::string path; const char* get() const { return path.c_str(); } };
struct Context {
    Directory directory;
    ConfigState state;
    Config<Context, NoChanges> cfg{*this};
    auto& configState() { return state; }
    auto& configState() const { return const_cast<ConfigState&>(state); }
    auto& osirisDirectoryPath() { return directory; }
    auto& config() { return cfg; }
};
class ConfigWorkflowTest : public testing::Test {
protected:
    void SetUp() override {
        allocator = MockMemoryAllocator::create();
        EXPECT_CALL(*allocator, allocate(testing::_)).WillRepeatedly([](std::size_t n) { return reinterpret_cast<std::byte*>(std::malloc(n)); });
        EXPECT_CALL(*allocator, deallocate(testing::_, testing::_)).WillRepeatedly([](std::byte* p, std::size_t) { std::free(p); });
        char name[] = "/tmp/ns-config-test-XXXXXX";
        const auto* created = mkdtemp(name);
        ASSERT_NE(created, nullptr);
        directory = created;
        context = std::make_unique<Context>();
        context->directory.path = directory;
        context->cfg.init();
    }
    void TearDown() override { context.reset(); std::filesystem::remove_all(directory); }
    void frames() { for (int i = 0; i < 4; ++i) { context->cfg.update(); context->cfg.performFileOperation(); } }
    int index(const char* name) {
        for (int i = 0; i < context->cfg.listedConfigCount(); ++i)
            if (std::string{context->cfg.listedConfigName(i)} == name) return i;
        ADD_FAILURE() << "Missing config " << name; return 0;
    }
    void write(const char* name, const char* body) {
        std::filesystem::create_directory(std::filesystem::path{directory} / "configs");
        std::ofstream{std::filesystem::path{directory} / "configs" / name} << body;
        context->cfg.markConfigListDirty(); frames();
    }
    std::shared_ptr<MockMemoryAllocator::MockType> allocator;
    std::unique_ptr<Context> context;
    std::string directory;
};
}

TEST_F(ConfigWorkflowTest, ListsNewFilesAndLoadsWithoutOverwritingSelectedConfig) {
    auto& cfg = context->cfg;
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{40}); frames();
    EXPECT_EQ(cfg.listedConfigCount(), 1);
    ASSERT_TRUE(cfg.createAndSwitchToConfig("second")); frames();
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{80}); frames();
    EXPECT_EQ(cfg.listedConfigCount(), 2);
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{90}); // pending autosave
    cfg.switchToConfig(index("default.cfg")); frames();
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Strength>()), 40);
    cfg.switchToConfig(index("second.cfg")); frames();
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Strength>()), 90);
    EXPECT_TRUE(cfg.lastLoadSucceeded());
}
TEST_F(ConfigWorkflowTest, ReloadButtonWorksForActiveFileAndRejectsDuplicateNames) {
    auto& cfg = context->cfg;
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{30}); frames();
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{99});
    cfg.switchToConfig(index("default.cfg")); frames();
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Strength>()), 30);
    EXPECT_FALSE(cfg.createAndSwitchToConfig("default"));
}
TEST_F(ConfigWorkflowTest, LoadingPartialDocumentResetsAbsentSettingsToDefaults) {
    auto& cfg = context->cfg;
    cfg.setVariable<legit_aimbot_vars::Mode>(legit_aimbot_vars::Mode::ValueType{2}); frames();
    write("partial.cfg", R"({"Combat":{"LegitAimbot":{"Enabled":true}}})");
    cfg.switchToConfig(index("partial.cfg")); frames();
    EXPECT_TRUE(cfg.getVariable<legit_aimbot_vars::Enabled>());
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Mode>()), 0);
    EXPECT_TRUE(cfg.lastLoadSucceeded());
}
// A file saved by an OLDER schema revision (extra keys, renamed keys, extra
// nested objects) must load: unknown keys are skipped, not fatal (2026-10-10
// default.cfg brick: strict walker stalled on BulletTracers leftovers).
TEST_F(ConfigWorkflowTest, LoadingDocumentWithOlderSchemaLeftoversSkipsUnknownKeys) {
    auto& cfg = context->cfg;
    write("oldrev.cfg", R"({"LegacyTopLevel":{"Stuff":true},"Combat":{"LegitAimbot":{"Enabled":true,"RemovedSetting":5,"RenamedOld":{"Inner":false}},"GhostObject":{"A":1}},"Visuals":{"Hitmarker":{"Enabled":true,"OldColor":4294932670}},"Sound":{"Visualizations":{"ImpactMarkers":{"Enabled":true}}}})");
    cfg.switchToConfig(index("oldrev.cfg")); frames();
    EXPECT_TRUE(cfg.lastLoadSucceeded());
    EXPECT_TRUE(cfg.getVariable<legit_aimbot_vars::Enabled>());
    EXPECT_TRUE(cfg.getVariable<HitmarkerEnabled>());
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Strength>()), static_cast<int>(legit_aimbot_vars::Strength::kDefaultValue));
}
TEST_F(ConfigWorkflowTest, MalformedDocumentKeepsSettingsWithoutDebugAssertion) {
    auto& cfg = context->cfg;
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{42}); frames();
    write("bad.cfg", R"({"Combat":{"LegitAimbot":{"Strength":1}})");
    cfg.switchToConfig(index("bad.cfg")); frames();
    EXPECT_FALSE(cfg.lastLoadSucceeded());
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Strength>()), 42);
}
TEST_F(ConfigWorkflowTest, DuplicationIncludesPendingEditsAndLongNamesCompareSafely) {
    auto& cfg = context->cfg;
    ASSERT_TRUE(cfg.createAndSwitchToConfig("a_very_long_config_name")); frames();
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{72});
    ASSERT_TRUE(cfg.duplicateActiveConfig()); frames();
    EXPECT_EQ(std::string{cfg.activeConfigNameForDisplay()}, "a_very_long_config_name_copy.cfg");
    cfg.setVariable<legit_aimbot_vars::Strength>(legit_aimbot_vars::Strength::ValueType{1});
    cfg.switchToConfig(index("a_very_long_config_name.cfg")); frames();
    EXPECT_EQ(static_cast<int>(cfg.getVariable<legit_aimbot_vars::Strength>()), 72);
}
