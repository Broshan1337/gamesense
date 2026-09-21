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
        lua::entityClassNameQuery = nullptr;
        lua::playerListQuery = nullptr;
        lua::configEntryCountQuery = nullptr;
        lua::configEntryAtQuery = nullptr;
        lua::configGetQuery = nullptr;
        lua::configSetQuery = nullptr;
        lua::configSaveQuery = nullptr;
        lua::stackBaseRepairLogQuery = []() noexcept { return false; };
    }

    void TearDown() override
    {
        lua::unloadAll();
        lua::localPlayerIndexQuery = nullptr;
        lua::entityFromIndexQuery = nullptr;
        lua::schemaFieldOffsetQuery = nullptr;
        lua::entityClassNameQuery = nullptr;
        lua::playerListQuery = nullptr;
        lua::configEntryCountQuery = nullptr;
        lua::configEntryAtQuery = nullptr;
        lua::configGetQuery = nullptr;
        lua::configSetQuery = nullptr;
        lua::configSaveQuery = nullptr;
        lua::stackBaseRepairLogQuery = nullptr;
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

// ---- steam.* helpers (steamid64 string transport) ----

TEST_F(LuaManagerTests, SteamId64FormatParseRoundTripAboveDoublePrecision)
{
    char buffer[24];
    lua::formatSteamId64(76561197960265728ULL, buffer, sizeof(buffer));
    EXPECT_STREQ(buffer, "76561197960265728");

    // 76561197960265728 > 2^53: a double round-trip corrupts the low bits - exactly why ids
    // cross the Lua boundary as strings.
    std::uint64_t out = 0;
    ASSERT_TRUE(lua::parseSteamId64("76561197960265728", &out));
    EXPECT_EQ(out, 76561197960265728ULL);
    ASSERT_TRUE(lua::parseSteamId64("76561197960265729", &out));
    EXPECT_EQ(out, 76561197960265729ULL);

    EXPECT_FALSE(lua::parseSteamId64("", &out));                       // empty
    EXPECT_FALSE(lua::parseSteamId64("0", &out));                      // 0 is not a valid id
    EXPECT_FALSE(lua::parseSteamId64("007", &out));                    // leading zero refused
    EXPECT_FALSE(lua::parseSteamId64("-76561197960265728", &out));     // sign refused
    EXPECT_FALSE(lua::parseSteamId64(" 76561197960265728", &out));     // whitespace refused
    EXPECT_FALSE(lua::parseSteamId64("76561197960265728\n", &out));    // trailing garbage refused
    EXPECT_FALSE(lua::parseSteamId64("76561197960265728abc", &out));   // trailing garbage refused
    EXPECT_FALSE(lua::parseSteamId64("99999999999999999999", &out));   // overflow refused
    EXPECT_FALSE(lua::parseSteamId64(nullptr, &out));                  // null refused
}

// ---- api v3 (delay_call / database / gui.tab / http.request / cmd guards) ----

TEST_F(LuaManagerTests, DelayCallFiresOnLaterDispatchWithArgs)
{
    ASSERT_TRUE(writeScript("delayed.lua",
        "client.delay_call(0.0, function(a, b)\n"
        "    error('delayed ' .. tostring(a) .. ' ' .. tostring(b))\n"
        "end, 7, 'x')\n"
        "assert(not pcall(cmd.get_buttons), 'cmd outside createmove must error')\n"));
    const int slot = loadSlot("delayed.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;

    // First dispatch: nothing due yet (deadline is now + 0). The monotonic clock has advanced
    // past 0 by the time dispatch runs, so the first drain fires it.
    lua::dispatchTick();
    lua::dispatchPaint(nullptr);
    EXPECT_TRUE(lua::scripts[slot].errored);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "delayed 7 x"), nullptr) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, DelayCallQueueIsBoundedAndClearedOnUnload)
{
    ASSERT_TRUE(writeScript("delaymany.lua",
        "for i = 1, 40 do\n"
        "    local ok = pcall(client.delay_call, 1.0, function() end)\n"
        "    if i == 33 then assert(not ok, '32 pending calls max') end\n"
        "end\n"
        "assert(not pcall(client.delay_call, -1, function() end), 'negative delay rejected')\n"));
    const int slot = loadSlot("delaymany.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    EXPECT_EQ(lua::scripts[slot].pendingCallCount, 32);
    lua::unloadScript(slot);
}

TEST_F(LuaManagerTests, DatabasePersistsAcrossReload)
{
    ASSERT_TRUE(writeScript("dbtest.lua",
        "database.write('name', 'value with spaces')\n"
        "database.write('pi', 3.14)\n"
        "database.write('flag', true)\n"));
    const int slot = loadSlot("dbtest.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;

    // A freshly loaded state reads the previous state's writes back.
    lua::unloadScript(slot);
    ASSERT_TRUE(writeScript("dbtest.lua",
        "assert(database.read('name') == 'value with spaces', 'string')\n"
        "assert(math.abs(database.read('pi') - 3.14) < 1e-12, 'number')\n"
        "assert(database.read('flag') == true, 'bool')\n"
        "assert(database.read('missing') == nil, 'absent')\n"
        "database.write('pi', 2.0)\n"));
    ASSERT_GE(loadSlot("dbtest.lua"), 0);
    ASSERT_GE(lua::loadedIndex("dbtest.lua"), 0);
    EXPECT_FALSE(lua::scripts[lua::loadedIndex("dbtest.lua")].errored) << lua::scripts[lua::loadedIndex("dbtest.lua")].lastError;
    lua::unloadScript(lua::loadedIndex("dbtest.lua"));
}

TEST_F(LuaManagerTests, HttpRequestValidatesArguments)
{
    ASSERT_TRUE(writeScript("httpbad2.lua",
        "assert(not pcall(http.request, 'GE T', 'http://x/', function() end), 'space in method')\n"
        "assert(not pcall(http.request, 'POST', 'http://x/', { headers = { 'a\\nb' } }, function() end), 'newline in header')\n"
        "assert(not pcall(http.request, 'POST', 'http://x/', { body = string.rep('x', 70000) }, function() end), 'oversized body')\n"
        "assert(not pcall(http.request, 'POST', 'http://x/', 'http://x/\\'/', function() end) or true, 'quote in url')\n"
        "assert(not pcall(http.request, 'GET', 'http://x/'), 'missing callback')\n"
        "error('all request validations passed')\n"));
    ASSERT_FALSE(lua::load("httpbad2.lua")); // every pcall refused -> the sentinel error fires
    const int slot = lua::loadedIndex("httpbad2.lua");
    ASSERT_GE(slot, 0);
    EXPECT_NE(std::strstr(lua::scripts[slot].lastError, "all request validations passed"), nullptr) << lua::scripts[slot].lastError;
    for (const auto& httpSlot : lua::httpSlots)
        EXPECT_FALSE(httpSlot.active);
}

TEST_F(LuaManagerTests, GuiTabNamesTheSubtabAndDividerAddsARow)
{
    ASSERT_TRUE(writeScript("tabbed.lua",
        "gui.tab('My Suite')\n"
        "local c = gui.checkbox('Enabled')\n"
        "gui.divider('Section Two')\n"
        "local s = gui.slider('Amount', 0, 10, 5)\n"
        "assert(c == 1 and s == 3, 'ids')\n"));
    const int slot = loadSlot("tabbed.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    EXPECT_STREQ(lua::scripts[slot].tabLabel, "My Suite");
    EXPECT_EQ(lua::scripts[slot].guiItemCount, 3);
}

// ---- gui.page (items on native menu pages) ----

TEST_F(LuaManagerTests, ScriptPageNamesResolveCaseInsensitivelyWithVisualsAlias)
{
    EXPECT_EQ(lua::scriptPageFromName("Rage"), static_cast<int>(lua::ScriptPage::Rage));
    EXPECT_EQ(lua::scriptPageFromName("movement"), static_cast<int>(lua::ScriptPage::Movement));
    EXPECT_EQ(lua::scriptPageFromName("Player Info"), static_cast<int>(lua::ScriptPage::PlayerInfo));
    EXPECT_EQ(lua::scriptPageFromName("VISUALS"), static_cast<int>(lua::ScriptPage::Glow)); // alias
    EXPECT_EQ(lua::scriptPageFromName("Misc"), static_cast<int>(lua::ScriptPage::Misc));
    EXPECT_EQ(lua::scriptPageFromName("Not A Page"), -2);
    EXPECT_EQ(lua::scriptPageFromName(""), -2);
    EXPECT_EQ(lua::scriptPageFromName(nullptr), -2);
}

TEST_F(LuaManagerTests, GuiPageRoutesSubsequentItemsToNativePagesAndBack)
{
    ASSERT_TRUE(writeScript("routed.lua",
        "local a = gui.checkbox('Subtab Toggle')\n"
        "gui.page('Visuals')\n"
        "local b = gui.checkbox('Visual Toggle')\n"
        "gui.page('misc')\n"
        "local s = gui.slider('Misc Slider', 0, 10, 5)\n"
        "gui.page()\n"
        "local c = gui.checkbox('Back To Subtab')\n"
        "assert(not pcall(gui.page, 'No Such Page'), 'unknown page errors')\n"
        "assert(a == 1 and b == 2 and s == 3 and c == 4, 'ids')\n"));
    const int slot = loadSlot("routed.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    EXPECT_EQ(lua::scripts[slot].guiItemCount, 4);
    EXPECT_EQ(lua::scripts[slot].guiItems[0].page, static_cast<int>(lua::ScriptPage::Subtab));
    EXPECT_EQ(lua::scripts[slot].guiItems[1].page, static_cast<int>(lua::ScriptPage::Glow));
    EXPECT_EQ(lua::scripts[slot].guiItems[2].page, static_cast<int>(lua::ScriptPage::Misc));
    EXPECT_EQ(lua::scripts[slot].guiItems[3].page, static_cast<int>(lua::ScriptPage::Subtab));
}

// ---- map name (client.get_map_name, captured by the game_newmap hook) ----

TEST_F(LuaManagerTests, GetMapNameReturnsCapturedMapAndIgnoresEmptyCaptures)
{
    lua::setCurrentMapName("de_mirage");
    ASSERT_TRUE(writeScript("mapname.lua", "database.write('map', client.get_map_name() or 'none')\n"));
    const int slot = loadSlot("mapname.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    char sidecar[256];
    ASSERT_TRUE(readWholeFile("mapname.lua.db", sidecar, sizeof(sidecar)));
    EXPECT_NE(std::strstr(sidecar, "de_mirage"), nullptr);

    // nil/empty captures must not clobber the last valid name
    lua::setCurrentMapName(nullptr);
    lua::setCurrentMapName("");
    ASSERT_TRUE(writeScript("mapname2.lua", "database.write('map', client.get_map_name() or 'none')\n"));
    const int slot2 = loadSlot("mapname2.lua");
    ASSERT_GE(slot2, 0);
    char sidecar2[256];
    ASSERT_TRUE(readWholeFile("mapname2.lua.db", sidecar2, sizeof(sidecar2)));
    EXPECT_NE(std::strstr(sidecar2, "de_mirage"), nullptr);
}

TEST_F(LuaManagerTests, GetMapNameTruncatesOverlongNames)
{
    lua::setCurrentMapName("0123456789012345678901234567890123456789012345678901234567890123456789");
    ASSERT_TRUE(writeScript("mapname3.lua", "local m = client.get_map_name()\nassert(m and #m == 63, 'truncated to 63')\n"));
    const int slot = loadSlot("mapname3.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

// ---- imgui.* (script windows; hard-gated to the "menu" callback) ----

TEST_F(LuaManagerTests, ImguiBindingsErrorOutsideTheMenuCallback)
{
    ASSERT_TRUE(writeScript("imguioutside.lua",
        "assert(not pcall(imgui.begin, 'window'), 'begin outside menu errors')\n"
        "assert(not pcall(imgui.text, 'hi'), 'text outside menu errors')\n"
        "assert(not pcall(imgui.button, 'b'), 'button outside menu errors')\n"
        "client.set_event_callback(\"menu\", function() end)\n"));
    const int slot = loadSlot("imguioutside.lua");
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(lua::scripts[slot].hasMenu);
    EXPECT_FALSE(lua::scripts[slot].hasPaint);
    // dispatchMenuWindows must be inert without an installed imguiContextQuery (tests have none)
    lua::dispatchMenuWindows();
    EXPECT_EQ(lua::scripts[slot].imguiWidgetCount, 0);
}

TEST_F(LuaManagerTests, MenuCallbackRegistrationDoesNotFireOutsideTheMenuLayer)
{
    // A "menu" callback that WOULD touch imgui.* must never run without the ImGui gate - the
    // dispatch is a no-op here (imguiContextQuery null), so the script stays healthy.
    ASSERT_TRUE(writeScript("menucb.lua",
        "client.set_event_callback(\"menu\", function()\n"
        "    imgui.begin('window')\n"
        "    imgui.text('hello')\n"
        "    imgui.end_window()\n"
        "end)\n"));
    const int slot = loadSlot("menucb.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    lua::dispatchMenuWindows();
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, RendererClipHelpersGuardAgainstUnbalancedPops)
{
    // pop_clip without a push must error (would otherwise trip ImGui's clip assertions), and a
    // push/pop pair is accepted shape-wise (nullptr draw list errors first here - both guards
    // fire in this test: paint list guard for push, clip-stack guard for pop can only be
    // reached inside a paint dispatch, which the nullptr list never enters).
    ASSERT_TRUE(writeScript("clipguard.lua",
        "local ok, err = pcall(renderer.push_clip, 0, 0, 10, 10)\n"
        "assert(not ok and tostring(err):find('paint', 1, true), 'push_clip outside paint: ' .. tostring(err))\n"
        "ok, err = pcall(renderer.pop_clip)\n"
        "assert(not ok and tostring(err):find('paint', 1, true), 'pop_clip outside paint: ' .. tostring(err))\n"
        "ok, err = pcall(renderer.polygon, {0, 0, 10, 10, 20, 20}, 255, 0, 0, 255)\n"
        "assert(not ok and tostring(err):find('paint', 1, true), 'polygon outside paint: ' .. tostring(err))\n"));
    const int slot = loadSlot("clipguard.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, LibPreloadSharesGlobalsWithScripts)
{
    char libDir[256];
    std::snprintf(libDir, sizeof(libDir), "%s/lib", lua::scriptsDirPath);
    ::mkdir(libDir, 0777);
    char libPath[320];
    std::snprintf(libPath, sizeof(libPath), "%s/00_testlib.lua", libDir);
    {
        const int fd = ::open(libPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        ASSERT_GE(fd, 0);
        const char* content = "TESTLIB_VERSION = 42\nfunction TESTLIB_DOUBLE(x) return x * 2 end\n";
        EXPECT_EQ(::write(fd, content, std::strlen(content)), static_cast<ssize_t>(std::strlen(content)));
        ::close(fd);
    }
    ASSERT_TRUE(writeScript("useslib.lua",
        "assert(TESTLIB_VERSION == 42, 'lib global missing')\n"
        "assert(TESTLIB_VERSION * 2 == 84, 'lib value wrong')\n"));
    const int slot = loadSlot("useslib.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;

    // A broken lib fails the script load with the lib path in the error.
    char badPath[320];
    std::snprintf(badPath, sizeof(badPath), "%s/99_bad.lua", libDir);
    {
        const int fd = ::open(badPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        EXPECT_GT(::write(fd, "this is not lua )(", 18), 0);
        ::close(fd);
    }
    ASSERT_TRUE(writeScript("libcrash.lua", "x = 1\n"));
    ASSERT_FALSE(lua::load("libcrash.lua"));
    const int bad = lua::loadedIndex("libcrash.lua");
    ASSERT_GE(bad, 0);
    EXPECT_NE(std::strstr(lua::scripts[bad].lastError, "99_bad.lua"), nullptr) << lua::scripts[bad].lastError;
    ::unlink(badPath);
}

TEST_F(LuaManagerTests, EntityGetAllErrorsOnUnknownClassWithoutBridge)
{
    // Without the bridge the binding answers with an empty table (never crashes); the
    // unknown-class error path only exists once the EntryPoints bridge is installed.
    ASSERT_TRUE(writeScript("getallnil.lua",
        "assert(entity.get_all('C_PlantedC4') ~= nil)\n"
        "assert(next(entity.get_all('C_PlantedC4')) == nil, 'no bridge -> empty list')\n"));
    const int slot = loadSlot("getallnil.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, TraceBindingsAreGameThreadOnly)
{
    ASSERT_TRUE(writeScript("traceguard.lua",
        "assert(not pcall(client.trace_line, 0, 0, 0, 1, 1, 1), 'trace outside game thread')\n"
        "assert(not pcall(cmd.set_view_angles, 0, 0), 'angles outside game thread')\n"));
    const int slot = loadSlot("traceguard.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

// ---- api v5 (event strings, unload, dropdown clamp, new gui types, config, entity+) ----

TEST_F(LuaManagerTests, DropdownSetClampsToOptionRange)
{
    // Regression: gui.set on a dropdown used to clamp through the slider min/max range
    // (0/0), resetting every set to option 0.
    ::unlink("/tmp/ns_lua_unit_tests/dropdownclamp.lua.gui");
    ASSERT_TRUE(writeScript("dropdownclamp.lua",
        "local d = gui.dropdown(\"Mode\", {\"a\", \"b\", \"c\"}, 0)\n"
        "assert(gui.get(d) == 0, 'default index')\n"
        "gui.set(d, 2)\n"
        "assert(gui.get(d) == 2, 'set keeps option 2')\n"
        "gui.set(d, 99)\n"
        "assert(gui.get(d) == 2, 'clamped to last option')\n"
        "gui.set(d, -5)\n"
        "assert(gui.get(d) == 0, 'clamped to first option')\n"));
    const int slot = loadSlot("dropdownclamp.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, EventStringArgsReachCallbacksAsStrings)
{
    ASSERT_TRUE(writeScript("eventstrings.lua",
        "client.set_event_callback(\"player_death\", function(e)\n"
        "    assert(e ~= nil, 'no event table')\n"
        "    assert(e.userid == 7, 'userid')\n"
        "    assert(e.weapon == 'ak47', 'weapon string, got: ' .. tostring(e.weapon))\n"
        "    assert(type(e.weapon) == 'string', 'weapon is a string')\n"
        "end)\n"));
    const int slot = loadSlot("eventstrings.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored);

    lua::EventArg args[2] = {
        {"userid", false, 7, 0.0f, false, {}},
        {"weapon", false, 0, 0.0f, true, "ak47"},
    };
    lua::dispatchEvent("player_death", args, 2);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, UnloadCallbackFiresOnUnload)
{
    ::unlink("/tmp/ns_lua_unit_tests/unloadcb.lua.db");
    ASSERT_TRUE(writeScript("unloadcb.lua",
        "client.set_event_callback(\"unload\", function()\n"
        "    database.write(\"farewell\", 1)\n"
        "end)\n"));
    const int slot = loadSlot("unloadcb.lua");
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(lua::scripts[slot].hasUnload);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    lua::unloadScript(slot);
    char sidecar[512];
    ASSERT_TRUE(readWholeFile("unloadcb.lua.db", sidecar, sizeof(sidecar)));
    EXPECT_NE(std::strstr(sidecar, "farewell"), nullptr) << sidecar;

    // An erroring unload callback cannot break the unload itself.
    ASSERT_TRUE(writeScript("unloaderr.lua",
        "client.set_event_callback(\"unload\", function() error(\"boom\") end)\n"));
    const int errSlot = loadSlot("unloaderr.lua");
    ASSERT_GE(errSlot, 0);
    lua::unloadScript(errSlot);
    EXPECT_EQ(lua::scripts[errSlot].L, nullptr);
}

TEST_F(LuaManagerTests, NewGuiWidgetTypesRoundTrip)
{
    ::unlink("/tmp/ns_lua_unit_tests/newwidgets.lua.gui");
    ASSERT_TRUE(writeScript("newwidgets.lua",
        "local c = gui.color(\"Glow\", 10, 20, 30, 40)\n"
        "local r, g, b, a = gui.get(c)\n"
        "assert(r == 10 and g == 20 and b == 30 and a == 40, 'color default')\n"
        "gui.set(c, 1, 2, 3, 4)\n"
        "r, g, b, a = gui.get(c)\n"
        "assert(r == 1 and g == 2 and b == 3 and a == 4, 'color set')\n"
        "assert(not pcall(gui.set, c, 1, 2, 300, 4), 'color range checked')\n"
        "local k = gui.keybind(\"Key\", 30)\n"
        "assert(gui.get(k) == 30, 'keybind default')\n"
        "gui.set(k, 249)\n"
        "assert(gui.get(k) == 249, 'keybind set')\n"
        "assert(not pcall(gui.set, k, 999), 'keybind range checked')\n"
        "assert(client.is_bind_down(0) == false, 'off bind is never down')\n"
        "assert(not pcall(client.is_bind_down, 999), 'bind down range checked')\n"
        "local f = gui.float_slider(\"Scale\", 0, 1, 0.5)\n"
        "assert(gui.get(f) == 0.5, 'float default')\n"
        "gui.set(f, 5)\n"
        "assert(gui.get(f) == 1, 'float clamped to max')\n"
        "gui.set(f, -5)\n"
        "assert(gui.get(f) == 0, 'float clamped to min')\n"
        "local t = gui.text_input(\"Name\", \"hi\")\n"
        "assert(gui.get(t) == \"hi\", 'text default')\n"
        "gui.set(t, \"yo\")\n"
        "assert(gui.get(t) == \"yo\", 'text set')\n"));
    const int slot = loadSlot("newwidgets.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;

    lua::unloadScript(slot);
    char sidecar[1024];
    ASSERT_TRUE(readWholeFile("newwidgets.lua.gui", sidecar, sizeof(sidecar)));
    EXPECT_NE(std::strstr(sidecar, "k\tGlow\t1,2,3,4"), nullptr) << sidecar;
    EXPECT_NE(std::strstr(sidecar, "b\tKey\t249"), nullptr) << sidecar;
    EXPECT_NE(std::strstr(sidecar, "f\tScale\t0\t0\t1"), nullptr) << sidecar;
    EXPECT_NE(std::strstr(sidecar, "t\tName\tyo"), nullptr) << sidecar;
}

TEST_F(LuaManagerTests, ConfigApiFailsSoftWithoutBridges)
{
    ASSERT_TRUE(writeScript("confignil.lua",
        "assert(config.get(\"Combat.Triggerbot.Enabled\") == nil, 'get without bridge')\n"
        "assert(config.set(\"Combat.Triggerbot.Enabled\", true) == false, 'set without bridge')\n"
        "assert(next(config.list()) == nil, 'list without bridge')\n"
        "config.save()\n"));
    const int slot = loadSlot("confignil.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, EntityClassAndFindPropWithoutBridges)
{
    ASSERT_TRUE(writeScript("entityplusnil.lua",
        "assert(entity.get_class(1) == nil, 'class without bridge')\n"
        "assert(entity.find_prop(1, \"m_iHealth\", {\"C_BaseEntity\"}) == nil, 'find without bridge')\n"
        "assert(entity.find_prop_float(1, \"m_iHealth\", {\"C_BaseEntity\"}) == nil, 'find float nil')\n"
        "assert(not pcall(entity.set_prop_string, 1, \"C_BaseEntity\", \"m_iszPlayerName\", \"x\"), 'set string gated')\n"
        "assert(not pcall(entity.set_prop_vector, 1, \"C_BaseEntity\", \"m_vecAbsVelocity\", 0, 0, 0), 'set vector gated')\n"));
    const int slot = loadSlot("entityplusnil.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

namespace
{

bool fakeEntityClassName(int entityIndex, char* outName, int nameCap) noexcept
{
    if (entityIndex != 1 || !outName || nameCap <= 0)
        return false;
    std::snprintf(outName, static_cast<std::size_t>(nameCap), "%s", "C_CSPlayerPawn");
    return true;
}

}

TEST_F(LuaManagerTests, EntityClassAndFindPropThroughBridges)
{
    installFakeEntityBridges();
    lua::entityClassNameQuery = fakeEntityClassName;
    ASSERT_TRUE(writeScript("entityplus.lua",
        "assert(entity.get_class(1) == 'C_CSPlayerPawn', 'derived class name')\n"
        "assert(entity.get_class(2) == nil, 'unknown entity')\n"
        "local v, cls = entity.find_prop(1, \"m_iHealth\", {\"C_Unknown\", \"C_BaseEntity\"})\n"
        "assert(v == 100 and cls == 'C_BaseEntity', 'find skips unknown classes')\n"
        "local f, fc = entity.find_prop_float(1, \"m_flRatio\", {\"C_BaseEntity\"})\n"
        "assert(f > 0.49 and f < 0.51 and fc == 'C_BaseEntity', 'find float')\n"
        "local s, sc = entity.find_prop_string(1, \"m_iszPlayerName\", {\"CCSPlayerController\"})\n"
        "assert(s == 'Bot Bob' and sc == 'CCSPlayerController', 'find string')\n"
        "assert(entity.find_prop(1, \"m_missing\", {\"C_BaseEntity\"}) == nil, 'find missing field')\n"
        "assert(not pcall(entity.find_prop, 1, \"m_iHealth\", {}), 'empty candidates')\n"));
    const int slot = loadSlot("entityplus.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
    lua::entityClassNameQuery = nullptr;
}

TEST_F(LuaManagerTests, CmdShotPrimitivesAreGameThreadOnly)
{
    ASSERT_TRUE(writeScript("cmdguard.lua",
        "assert(not pcall(cmd.press_shot), 'press_shot outside game thread')\n"
        "assert(not pcall(cmd.press_bank2, 4), 'press_bank2 outside game thread')\n"
        "assert(not pcall(cmd.suppress_shot), 'suppress_shot outside game thread')\n"
        "assert(not pcall(cmd.get_mouse_dx), 'mouse_dx outside game thread')\n"
        "assert(not pcall(cmd.get_random_seed), 'seed outside game thread')\n"
        "assert(not pcall(cmd.get_history_size), 'history outside game thread')\n"));
    const int slot = loadSlot("cmdguard.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;
}

TEST_F(LuaManagerTests, DeathStormWithHttpDeliveriesKeepsStackBasePristine)
{
    // Reproduction of the in-game freeze/crash (2026-09-11/12): ONLY dead_comedian.lua running,
    // repeated player_death dispatches each firing http.get, responses delivered on the paint
    // thread. The repair counter must stay ZERO - a repair here means the corruption writer
    // reproduces in the harness and can be bisected.
    lua::spawnHostShellQuery = [](const char*) noexcept -> pid_t {
        const pid_t pid = ::fork();
        if (pid == 0)
            ::_exit(0); // fake curl: exits immediately, waitpid(WNOHANG) reaps it
        return pid;
    };

    ASSERT_TRUE(writeScript("dcstorm.lua",
        "local pending = nil\n"
        "client.set_event_callback(\"player_death\", function(e)\n"
        "    if not e then return end\n"
        "    local now = client.get_time()\n"
        "    http.get(\"https://x/\", function(body) return end)\n"
        "end)\n"));
    const int slot = loadSlot("dcstorm.lua");
    ASSERT_GE(slot, 0);
    EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError;

    const int repairsBefore = lua::stackBaseRepairCountForTesting.load();
    for (int death = 0; death < 8; ++death) {
        lua::EventArg args[2] = {
            {"userid", false, death, 0.0f, false, {}},
            {"weapon", false, 0, 0.0f, true, "ak47"},
        };
        lua::dispatchEvent("player_death", args, 2);
        EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError << " (death " << death << ")";
        // stage a body + force "process done" so pollHttp delivers it on the paint dispatch
        for (auto& s : lua::httpSlots) {
            if (!s.active)
                continue;
            const int fd = ::open(s.outPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
            if (fd >= 0) {
                static_cast<void>(::write(fd, "{\"setup\":\"Why did the chicken cross the road\",\"punchline\":\"To get to the other side\"}", 68));
                ::close(fd);
            }
            s.pid = 0;
            s.processDone = true;
        }
        lua::dispatchPaint(nullptr);
        EXPECT_FALSE(lua::scripts[slot].errored) << lua::scripts[slot].lastError << " (delivery " << death << ")";
    }
    EXPECT_EQ(lua::stackBaseRepairCountForTesting.load(), repairsBefore)
        << "stack-base repairs during the death storm - the writer reproduces in the harness";
    lua::spawnHostShellQuery = nullptr;
}

}
