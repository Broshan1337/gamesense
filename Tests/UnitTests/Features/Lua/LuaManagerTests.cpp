#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include <Features/Lua/LuaManager.h>

namespace
{

// All tests share one scripts directory; the framework state is global, so tests run in a
// fixed order-friendly pattern: each test uses a fresh script name.
class LuaManagerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::snprintf(lua::scriptsDirPath, sizeof(lua::scriptsDirPath), "/tmp/ns_lua_unit_tests");
        ::mkdir(lua::scriptsDirPath, 0777);
        lua::unloadAll();
        lua::menuOpenQuery = []() noexcept { return false; };
    }

    void TearDown() override
    {
        lua::unloadAll();
    }

    [[nodiscard]] static bool writeScript(const char* name, const char* content)
    {
        char path[256];
        std::snprintf(path, sizeof(path), "/tmp/ns_lua_unit_tests/%s", name);
        const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd < 0)
            return false;
        const auto length = std::strlen(content);
        const ssize_t written = ::write(fd, content, length);
        ::close(fd);
        return written == static_cast<ssize_t>(length);
    }

    [[nodiscard]] static int loadSlot(const char* name)
    {
        return lua::load(name) ? lua::loadedIndex(name) : -1;
    }
};

TEST_F(LuaManagerTests, ValidScriptNameAcceptsNormalNamesAndRejectsTraversalAndNonLua)
{
    EXPECT_TRUE(lua::validScriptName("myscript.lua"));
    EXPECT_TRUE(lua::validScriptName("My Script (v2).lua"));
    EXPECT_FALSE(lua::validScriptName("script.txt"));
    EXPECT_FALSE(lua::validScriptName("lua"));
    EXPECT_FALSE(lua::validScriptName("../etc/passwd.lua")); // '/' rejected
    EXPECT_FALSE(lua::validScriptName("..lua"));             // leading dot-dot segment rejected
    EXPECT_FALSE(lua::validScriptName(""));
    EXPECT_FALSE(lua::validScriptName(nullptr));
}

TEST_F(LuaManagerTests, LoadRunsScriptAndRegistersPaintCallback)
{
    ASSERT_TRUE(writeScript("good.lua", "client.set_event_callback(\"paint\", function() end)\n"));
    const int slot = loadSlot("good.lua");
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(lua::scripts[slot].hasPaint);
    EXPECT_FALSE(lua::scripts[slot].errored);
    EXPECT_EQ(lua::scripts[slot].lastError[0], '\0');
}

TEST_F(LuaManagerTests, RuntimeErrorIsContainedAndAutoDisablesTheScript)
{
    ASSERT_TRUE(writeScript("broken.lua", "error('boom at runtime')\n"));
    // load() reports failure for a top-level error, but the script stays loaded-and-errored
    // (the menu displays lastError instead of silently dropping it).
    ASSERT_FALSE(lua::load("broken.lua"));
    const int slot = lua::loadedIndex("broken.lua");
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(lua::scripts[slot].errored);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "boom at runtime"), nullptr);

    // Dispatching into an errored script is a no-op, not a crash.
    lua::dispatchEvent("player_hurt");
    lua::dispatchPaint(nullptr);
    lua::dispatchTick();
}

TEST_F(LuaManagerTests, ErrorInsideCallbackIsCaughtWithTraceback)
{
    ASSERT_TRUE(writeScript("cberr.lua", "client.set_event_callback(\"player_hurt\", function()\nerror('cb failure')\nend)\n"));
    const int slot = loadSlot("cberr.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);
    lua::dispatchEvent("player_hurt");
    EXPECT_TRUE(lua::scripts[slot].errored);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "cb failure"), nullptr);
}

TEST_F(LuaManagerTests, InfiniteLoopIsStoppedByInstructionBudget)
{
    ASSERT_TRUE(writeScript("spin.lua", "client.set_event_callback(\"paint\", function()\nwhile true do end\nend)\n"));
    const int slot = loadSlot("spin.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);

    // Paint dispatch must return (budget abort), with the script auto-disabled. nullptr draw
    // list: a renderer.* call inside the loop errors out instead of drawing (also covers the
    // null-draw-list guard).
    lua::dispatchPaint(nullptr);
    EXPECT_TRUE(lua::scripts[slot].errored);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "instruction budget exceeded"), nullptr);
}

TEST_F(LuaManagerTests, SandboxStripsDangerousLibraries)
{
    ASSERT_TRUE(writeScript("sandbox.lua",
        "assert(os == nil)\n"
        "assert(io == nil)\n"
        "assert(package == nil)\n"
        "assert(require == nil)\n"
        "assert(dofile == nil)\n"
        "assert(loadfile == nil)\n"
        "assert(ffi ~= nil)\n"
        "assert(bit ~= nil)\n"
        "assert(debug ~= nil and debug.traceback ~= nil and debug.getinfo == nil)\n"));
    const int slot = loadSlot("sandbox.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, BytecodeChunksAreRejectedByLoad)
{
    // ESC + garbage = a precompiled chunk - safeLoad must refuse it.
    ASSERT_TRUE(writeScript("bytecode.lua", "assert(not pcall(load, \"\\27LuaJIT-garbage\"))\n"));
    const int slot = loadSlot("bytecode.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, RendererOutsidePaintCallbackErrorsInsteadOfCrashing)
{
    ASSERT_TRUE(writeScript("renderer.lua", "client.set_event_callback(\"player_hurt\", function()\nrenderer.text(1, 2, \"x\", 255, 255, 255, 255)\nend)\n"));
    const int slot = loadSlot("renderer.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);
    lua::dispatchEvent("player_hurt");
    EXPECT_TRUE(lua::scripts[slot].errored);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "'paint' callback"), nullptr);
}

TEST_F(LuaManagerTests, IdaPatternParsingAndScanning)
{
    lua::PatternByte bytes[lua::kMaxPatternBytes]{};

    // parse: mixed case + wildcards ("??" and bare "?" are each ONE wildcard byte)
    ASSERT_EQ(lua::parseIdaPattern("48 8B 05 ?? ?", bytes, lua::kMaxPatternBytes), 5);
    EXPECT_FALSE(bytes[0].wildcard);
    EXPECT_EQ(bytes[0].value, 0x48);
    EXPECT_EQ(bytes[1].value, 0x8B);
    EXPECT_TRUE(bytes[3].wildcard);
    EXPECT_TRUE(bytes[4].wildcard);

    // parse: rejects garbage, empties, and overlong patterns
    EXPECT_EQ(lua::parseIdaPattern("48 ZZ 05", bytes, lua::kMaxPatternBytes), 0);
    EXPECT_EQ(lua::parseIdaPattern("   ", bytes, lua::kMaxPatternBytes), 0);
    EXPECT_EQ(lua::parseIdaPattern("11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00", bytes, 8), 0);

    // scan: find with wildcards, miss without
    const unsigned char data[] = {0x10, 0x48, 0x89, 0x99, 0xAB, 0x00, 0x48, 0x89, 0x05, 0xFF};
    ASSERT_EQ(lua::parseIdaPattern("48 89 ?? AB", bytes, lua::kMaxPatternBytes), 4);
    const auto* match = lua::scanMemoryPattern(data, sizeof(data), bytes, 4);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match - data, 1);
    EXPECT_EQ(lua::parseIdaPattern("48 89 05 AB", bytes, lua::kMaxPatternBytes), 4);
    EXPECT_EQ(lua::scanMemoryPattern(data, sizeof(data), bytes, 4), nullptr); // 05 != 99

    // scan: pattern longer than the buffer never matches (no overflow)
    EXPECT_EQ(lua::scanMemoryPattern(data, sizeof(data), bytes, lua::kMaxPatternBytes), nullptr);
    EXPECT_EQ(lua::scanMemoryPattern(nullptr, 10, bytes, 1), nullptr);
}

TEST_F(LuaManagerTests, PatternScanFromScriptErrorsOnGarbageBeforeTouchingModules)
{
    // Invalid pattern -> Lua error (auto-disable) without any module lookup (which the test
    // binary cannot perform - LinuxPlatformApi::dlopen is gmock-routed here). load() reports
    // false for a top-level error while keeping the slot loaded-and-errored.
    ASSERT_TRUE(writeScript("badpattern.lua", "memory.pattern_scan(\"libc.so.6\", \"not a pattern ZZ\")\n"));
    ASSERT_FALSE(lua::load("badpattern.lua"));
    const int slot = lua::loadedIndex("badpattern.lua");
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(lua::scripts[slot].errored);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "invalid or too long pattern"), nullptr);
}

TEST_F(LuaManagerTests, HttpGetRejectsShellInjectionInUrl)
{
    ASSERT_TRUE(writeScript("httpbad.lua", "assert(not pcall(http.get, \"http://x/' ; rm -rf /\", function() end))\n"));
    const int slot = loadSlot("httpbad.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    // No request should have been spawned.
    for (const auto& httpSlot : lua::httpSlots)
        EXPECT_FALSE(httpSlot.active);
}

TEST_F(LuaManagerTests, UnloadCleansTheSlot)
{
    ASSERT_TRUE(writeScript("tounload.lua", "x = 1\n"));
    const int slot = loadSlot("tounload.lua");
    ASSERT_GE(slot, 0);
    lua::unloadScript(slot);
    EXPECT_EQ(lua::loadedIndex("tounload.lua"), -1);
    EXPECT_EQ(lua::scripts[slot].L, nullptr);
}

TEST_F(LuaManagerTests, ReloadReplacesTheState)
{
    ASSERT_TRUE(writeScript("reload.lua", "client.set_event_callback(\"paint\", function() end)\n"));
    ASSERT_GE(loadSlot("reload.lua"), 0);
    void* firstState = lua::scripts[lua::loadedIndex("reload.lua")].L;
    ASSERT_NE(firstState, nullptr);

    ASSERT_GE(loadSlot("reload.lua"), 0); // second load replaces the first state
    const int slot = lua::loadedIndex("reload.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);
    // The new state may legally reuse the closed state's address (same allocator), so only
    // liveness is asserted here.
    EXPECT_NE(lua::scripts[slot].L, nullptr);
}

}
