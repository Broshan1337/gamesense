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
        lua::localPlayerIndexQuery = nullptr;
        lua::entityFromIndexQuery = nullptr;
        lua::schemaFieldOffsetQuery = nullptr;
        lua::playerListQuery = nullptr;
    }

    void TearDown() override
    {
        lua::unloadAll();
        lua::localPlayerIndexQuery = nullptr;
        lua::entityFromIndexQuery = nullptr;
        lua::schemaFieldOffsetQuery = nullptr;
        lua::playerListQuery = nullptr;
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

    [[nodiscard]] static bool readWholeFile(const char* name, char* buffer, std::size_t bufferSize)
    {
        char path[256];
        std::snprintf(path, sizeof(path), "/tmp/ns_lua_unit_tests/%s", name);
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0)
            return false;
        const ssize_t bytes = ::read(fd, buffer, bufferSize - 1);
        ::close(fd);
        if (bytes < 0)
            return false;
        buffer[bytes] = '\0';
        return true;
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

// ---- entity API (bridges) ----

namespace
{

alignas(16) unsigned char fakeEntityMemory[64];

int fakeLocalPlayerIndex() noexcept
{
    return 1;
}

void* fakeEntityFromIndex(int index) noexcept
{
    return index == 1 ? fakeEntityMemory : nullptr;
}

int fakeSchemaFieldOffset(const char* className, const char* fieldName) noexcept
{
    if (std::strcmp(className, "C_BaseEntity") == 0) {
        if (std::strcmp(fieldName, "m_iHealth") == 0)
            return 8;
        if (std::strcmp(fieldName, "m_flRatio") == 0)
            return 16;
    }
    if (std::strcmp(className, "CCSPlayerController") == 0 && std::strcmp(fieldName, "m_iszPlayerName") == 0)
        return 24;
    return -1;
}

lua::PlayerListEntry fakePlayers[2] = {{1, 7}, {2, 9}};

int fakePlayerList(lua::PlayerListEntry* out, int max) noexcept
{
    int count = 0;
    for (const auto& entry : fakePlayers) {
        if (count >= max)
            break;
        out[count++] = entry;
    }
    return count;
}

void installFakeEntityBridges()
{
    std::memset(fakeEntityMemory, 0, sizeof(fakeEntityMemory));
    const std::int32_t health = 100;
    std::memcpy(fakeEntityMemory + 8, &health, sizeof(health));
    const float ratio = 0.5f;
    std::memcpy(fakeEntityMemory + 16, &ratio, sizeof(ratio));
    std::memcpy(fakeEntityMemory + 24, "Bot Bob", 8);

    lua::localPlayerIndexQuery = fakeLocalPlayerIndex;
    lua::entityFromIndexQuery = fakeEntityFromIndex;
    lua::schemaFieldOffsetQuery = fakeSchemaFieldOffset;
    lua::playerListQuery = fakePlayerList;
}

}

TEST_F(LuaManagerTests, EntityApiReturnsNilWithoutBridges)
{
    ASSERT_TRUE(writeScript("entitynil.lua",
        "assert(entity.get_local_player() == nil)\n"
        "assert(entity.get_players() ~= nil and next(entity.get_players()) == nil)\n"
        "assert(entity.get_player_pawn(1) == nil)\n"
        "assert(entity.get_prop(1, \"C_BaseEntity\", \"m_iHealth\") == nil)\n"));
    const int slot = loadSlot("entitynil.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, EntityApiReadsValuesThroughBridges)
{
    installFakeEntityBridges();
    ASSERT_TRUE(writeScript("entityread.lua",
        "local lp = entity.get_local_player()\n"
        "assert(lp == 1, 'local player')\n"
        "assert(entity.get_prop(lp, \"C_BaseEntity\", \"m_iHealth\") == 100, 'int prop')\n"
        "local f = entity.get_prop_float(lp, \"C_BaseEntity\", \"m_flRatio\")\n"
        "assert(f > 0.49 and f < 0.51, 'float prop')\n"
        "assert(entity.get_prop_string(lp, \"CCSPlayerController\", \"m_iszPlayerName\") == \"Bot Bob\", 'string prop')\n"
        "assert(entity.get_prop(2, \"C_BaseEntity\", \"m_iHealth\") == nil, 'unknown entity')\n"
        "assert(entity.get_prop(lp, \"C_BaseEntity\", \"m_missing\") == nil, 'unknown field')\n"
        "assert(entity.get_prop(lp, \"C_Unknown\", \"m_iHealth\") == nil, 'unknown class')\n"
        "assert(not pcall(entity.get_prop, lp, \"\", \"m_iHealth\"), 'empty class name')\n"
        "assert(not pcall(entity.get_prop, 99999, \"C_BaseEntity\", \"m_iHealth\"), 'index out of range')\n"
        "local players = entity.get_players()\n"
        "assert(#players == 2 and players[1] == 1 and players[2] == 2, 'players')\n"
        "assert(entity.get_player_pawn(1) == 7, 'pawn 1')\n"
        "assert(entity.get_player_pawn(2) == 9, 'pawn 2')\n"
        "assert(entity.get_player_pawn(3) == nil, 'pawn none')\n"));
    const int slot = loadSlot("entityread.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

// ---- gui.* (script menu items + sidecar persistence) ----

TEST_F(LuaManagerTests, GuiItemsRestoreSidecarValuesAndSaveOnUnload)
{
    // Pre-seed the sidecar as if an earlier session had saved state for this script.
    ASSERT_TRUE(writeScript("persist.lua.gui", "c\tEnable Things\t0\ns\tAmount\t77\t0\t100\n"));

    ASSERT_TRUE(writeScript("persist.lua",
        "local c = gui.checkbox(\"Enable Things\", true)\n"
        "assert(c == 1, 'first id')\n"
        "assert(gui.get(c) == false, 'saved checkbox value wins')\n"
        "local s = gui.slider(\"Amount\", 0, 100, 42)\n"
        "assert(s == 2, 'second id')\n"
        "assert(gui.get(s) == 77, 'saved slider value wins')\n"
        "gui.set(s, 5)\n"
        "assert(gui.get(s) == 5, 'set from script')\n"
        "assert(not pcall(gui.get, 99), 'invalid id errors')\n"));
    const int slot = loadSlot("persist.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;

    // Unload persists the current values (checkbox 0 from the seed, slider changed to 5).
    lua::unloadScript(slot);
    char sidecar[512];
    ASSERT_TRUE(readWholeFile("persist.lua.gui", sidecar, sizeof(sidecar)));
    EXPECT_NE(std::strstr(sidecar, "c\tEnable Things\t0"), nullptr) << sidecar;
    EXPECT_NE(std::strstr(sidecar, "s\tAmount\t5\t0\t100"), nullptr) << sidecar;
}

TEST_F(LuaManagerTests, GuiSliderClampsSetValuesAndValidatesLabels)
{
    ASSERT_TRUE(writeScript("guivalid.lua",
        "local s = gui.slider(\"Level\", 0, 10, 5)\n"
        "gui.set(s, 999)\n"
        "assert(gui.get(s) == 10, 'clamped to max')\n"
        "gui.set(s, -5)\n"
        "assert(gui.get(s) == 0, 'clamped to min')\n"
        "assert(not pcall(gui.checkbox, \"has=equals\"), 'forbidden label char')\n"
        "assert(not pcall(gui.slider, \"Level\", 50, 1, 5), 'min above max')\n"));
    const int slot = loadSlot("guivalid.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

// ---- event args ----

TEST_F(LuaManagerTests, EventArgsReachCallbacksAsTables)
{
    ASSERT_TRUE(writeScript("eventargs.lua",
        "client.set_event_callback(\"player_hurt\", function(e)\n"
        "    assert(e ~= nil, 'no event table')\n"
        "    assert(e.userid == 3, 'userid')\n"
        "    assert(e.attacker == 65535, 'attacker nobody marker')\n"
        "    assert(e.dmg_health == 76, 'dmg_health')\n"
        "end)\n"));
    const int slot = loadSlot("eventargs.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);

    lua::EventArg args[3] = {
        {"userid", false, 3, 0.0f},
        {"attacker", false, 65535, 0.0f},
        {"dmg_health", false, 76, 0.0f},
    };
    lua::dispatchEvent("player_hurt", args, 3);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, DispatchWithoutArgsPassesNilEvent)
{
    ASSERT_TRUE(writeScript("noargs.lua",
        "client.set_event_callback(\"round_start\", function(e)\n"
        "    assert(e == nil, 'expected nil event')\n"
        "end)\n"));
    const int slot = loadSlot("noargs.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);
    lua::dispatchEvent("round_start");
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

}
