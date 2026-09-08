#pragma once

// Script-facing Lua API.
//
// INCLUDED INSIDE namespace lua (from LuaManager.h) - do not include headers here and do not
// open a namespace: everything this file needs (lua.h, imgui.h, VerifyConsole,
// LinuxDynamicLibrary, the state structs and helpers) is already visible at the include point.
//
// API surface v2:
//   client.set_event_callback(name, fn)   - events: "paint" (present thread, every frame,
//                                           renderer valid inside), "createmove" (game thread,
//                                           once per input tick), plus any game event name
//                                           (e.g. "player_hurt", "weapon_fire"). Known events
//                                           pass an `event` table of NUMERIC fields to the
//                                           callback: player_hurt (userid/attacker 0-based
//                                           player slots, 65535 = nobody, dmg_health, health,
//                                           armor, dmg_armor, hitgroup), player_death (userid/
//                                           attacker/assister/headshot/dominated), weapon_fire
//                                           (userid), bullet_impact + *_detonate (userid,
//                                           x/y/z floats). Other events pass no arguments.
//   client.log(message)                   - engine console (VerifyConsole, throttled)
//   client.exec(command)                  - run a console command through the engine's client
//                                           command buffer (game-thread callbacks only) - chat
//                                           ("say ..."), radio ("playerchatwheel ..."), cvars,
//                                           "pause" on sv_pausable servers, anything console-able
//   client.get_time()                     - steady-clock seconds since module load; the only
//                                           timing source (os.clock is sandboxed away)
//   client.get_screen_size()              - width, height
//   client.is_menu_open()
//   cvar.get_int(name) / cvar.get_float(name) - read a runtime convar (nil = absent/wrong type)
//   cvar.set_float(name, value) / cvar.set_bool(name, value) - write through the convar's
//                                           resolved value pointer (same force* path C++
//                                           features use). false = not found / wrong type.
//   renderer.text(x, y, text, r, g, b, a)
//   renderer.text_size(text)              - width, height
//   renderer.line(x1, y1, x2, y2, r, g, b, a [, thickness])
//   renderer.rect(x, y, w, h, r, g, b, a [, thickness])
//   renderer.filled_rect(x, y, w, h, r, g, b, a)
//   renderer.circle(x, y, radius, r, g, b, a [, segments])
//   renderer.circle_filled(x, y, radius, r, g, b, a [, segments])
//   renderer.world_to_screen(x, y, z)     - screen px, py, or nil when behind the camera /
//                                           matrix unavailable (drawn coords, y grows down)
//   memory.module_base(module)            - load base of a loaded module ("libclient.so", ...)
//   memory.pattern_scan(module, pattern)  - IDA-style "48 8B 05 ?? ?? ?? ??" scan of .text,
//                                           returns the match address as lightuserdata or nil
//   http.get(url, callback)               - async; callback(bodyOrNil) fires on a later frame
//   entity.get_local_player()             - local CONTROLLER entity index, or nil when not
//                                           in a game (schema/entity data not ready)
//   entity.get_players()                  - table of controller entity indices (players with
//                                           an active pawn; bots included)
//   entity.get_player_pawn(controllerIdx) - pawn entity index for a controller index, or nil
//   entity.get_prop(index, class, field)  - schema-driven read; declaring class required (the
//                                           schema iterator does not walk parents). Int variant,
//   entity.get_prop_float(...)            - float variant,
//   entity.get_prop_string(...)           - string variant for fixed char arrays (printable
//                                           check; nil when the bytes are not a clean string)
//   entity.set_prop(index, class, field, value)      - int32 WRITE (game-thread callbacks only)
//   entity.set_prop_float(index, class, field, value) - float32 WRITE (game-thread callbacks only)
//   gui.checkbox(label [, default])       - menu checkbox in the script's section; 1-based id
//   gui.slider(label, min, max [, default]) - menu slider; 1-based id
//   gui.dropdown(label, options [, defaultIndex]) - menu dropdown (searchable popup when the
//                                           option list is long); gui.get returns the INDEX
//   gui.get(id)                           - current value (boolean / integer)
//   gui.set(id, value)                    - set from the script; values persist per script
//                                           (sidecar <scriptsDir>/<name>.gui, applied by label
//                                           on load, written on unload)
//   net.server()                          - fd, ip, port of the game server as observed from the
//                                           datagram hook, or nil when not connected
//   net.send_raw(data [, count])          - send `count` copies (default 1, max 256) of a
//                                           binary-safe string (max 1400 bytes) to the game
//                                           server through the original sendto - bypasses the
//                                           C++ net-lag hook entirely. Returns packets sent,
//                                           nil when not connected.
//   net.stats()                           - the hook's counters: sends/passed/dropped/duped/
//                                           flooded/connless/raw/blips/delayed/flushed/overflow/
//                                           region_blocked
//   net.set_blocked_ips(table)            - publish a block list of IPv4 strings ("1.2.3.4",
//                                           optional "/prefix" length); datagrams to these
//                                           addresses are silently swallowed in the send hook
//                                           EXCEPT datagrams to the currently-connected game
//                                           server. Capped at 512 entries. Returns entries kept.
//   net.clear_blocked_ips()               - drop the block list (traffic resumes untouched)
//
// FFI is available as a global (ffi.cast / ffi.C / ffi.load, LuaJIT GC64 build) for raw memory
// work; memory.module_base + memory.pattern_scan give scripts the same anchors our C++ uses.

// ---- helpers ----

// Every client/http binding carries its script's slot index as closure upvalue 1.
inline Script& selfScript(lua_State* L)
{
    return scripts[static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)))];
}

inline ImU32 checkColor(lua_State* L, int index)
{
    auto component = [L, index](int offset) -> int {
        const lua_Number value = luaL_checknumber(L, index + offset);
        if (value < 0.0 || value > 255.0)
            return luaL_error(L, "color components must be in range 0-255"), 0;
        return static_cast<int>(value);
    };
    return IM_COL32(component(0), component(1), component(2), component(3));
}

inline ImDrawList* requirePaintList(lua_State* L)
{
    if (!paintDrawList) {
        luaL_error(L, "renderer is only available inside the 'paint' callback"); // longjmps
        return nullptr; // unreachable
    }
    return paintDrawList;
}

// ---- callback registry (per-state registry key "__ns_callbacks": { [event] = {fn, ...} }) ----

inline void registerEventCallback(lua_State* L, const char* eventName)
{
    lua_pushliteral(L, "__ns_callbacks");
    lua_rawget(L, LUA_REGISTRYINDEX); // [callbacks?]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);                            // [callbacks]
        lua_pushliteral(L, "__ns_callbacks");       // [callbacks, key]
        lua_pushvalue(L, -2);                       // [callbacks, key, callbacks]
        lua_rawset(L, LUA_REGISTRYINDEX);           // registry[key] = callbacks
    }
    lua_pushstring(L, eventName);                   // [callbacks, event]
    lua_rawget(L, -2);                              // [callbacks, array?]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);                              // [callbacks]
        lua_newtable(L);                            // [callbacks, array]
        lua_pushstring(L, eventName);               // [callbacks, array, key]
        lua_pushvalue(L, -2);                       // [callbacks, array, key, array]
        lua_rawset(L, -4);                          // callbacks[key] = array
    }
    const int count = static_cast<int>(lua_objlen(L, -1));
    lua_pushvalue(L, 2); // the callback function - set_event_callback's 2nd argument
    lua_rawseti(L, -2, count + 1);
    lua_pop(L, 2); // array + callbacks
}

// ---- client ----

inline int l_setEventCallback(lua_State* L)
{
    const char* eventName = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    registerEventCallback(L, eventName);

    Script& script = selfScript(L);
    if (std::strcmp(eventName, "paint") == 0)
        script.hasPaint = true;
    else if (std::strcmp(eventName, "createmove") == 0)
        script.hasTick = true;
    return 0;
}

inline int l_clientLog(lua_State* L)
{
    const char* message = luaL_checkstring(L, 1);
    VerifyConsole::write(1.0f, "lua", "%s", message);
    return 0;
}

inline int l_getScreenSize(lua_State* L)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    lua_pushnumber(L, display.x);
    lua_pushnumber(L, display.y);
    return 2;
}

inline int l_isMenuOpen(lua_State* L)
{
    lua_pushboolean(L, menuOpenQuery && menuOpenQuery() ? 1 : 0);
    return 1;
}

// Steady-clock seconds since boot - the sandbox strips os.clock, so scripts time their cadences
// (chat bursts, snitch cooldowns) through this instead. Direct clock_gettime (the project-wide
// monotonicSeconds pattern): std::chrono::steady_clock::now is an out-of-line libstdc++ symbol
// and the release target links -nostdlib.
inline int l_getTime(lua_State* L)
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lua_pushnumber(L, static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9);
    return 1;
}

// ---- cvar (runtime convar reads/writes through the CvarSystem bridges) ----

inline int l_cvarGetInt(lua_State* L)
{
    int value = 0;
    if (cvarIntQuery && cvarIntQuery(luaL_checkstring(L, 1), &value))
        lua_pushinteger(L, value);
    else
        lua_pushnil(L);
    return 1;
}

inline int l_cvarGetFloat(lua_State* L)
{
    float value = 0.0f;
    if (cvarFloatQuery && cvarFloatQuery(luaL_checkstring(L, 1), &value))
        lua_pushnumber(L, value);
    else
        lua_pushnil(L);
    return 1;
}

inline int l_cvarSetFloat(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const float value = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushboolean(L, cvarFloatSetQuery && cvarFloatSetQuery(name, value) ? 1 : 0);
    return 1;
}

inline int l_cvarSetBool(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const bool value = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, cvarBoolSetQuery && cvarBoolSetQuery(name, value) ? 1 : 0);
    return 1;
}

// Runs a console command through the engine's client command buffer - the same path ChatTools /
// RadioManager use for say / chatwheel / cvars. Game-thread callbacks only: the engine command
// buffer is not thread-safe, so calling from a paint callback errors instead of racing it.
inline int l_clientExec(lua_State* L)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        return luaL_error(L, "client.exec is only available inside createmove / game event callbacks");
    const char* command = luaL_checkstring(L, 1);
    const std::size_t length = std::strlen(command);
    if (length == 0 || length >= 256)
        return luaL_error(L, "client.exec: command must be 1-255 characters");
    if (std::strchr(command, '\n') || std::strchr(command, '\r'))
        return luaL_error(L, "client.exec: newlines would inject extra commands");
    if (!engineCommandQuery)
        return luaL_error(L, "client.exec: engine bridge not installed");
    engineCommandQuery(command);
    return 0;
}

// ---- renderer (foreground draw list; only valid inside a "paint" callback) ----

inline constexpr float kScriptFontSize = 14.0f;

inline int l_renderText(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const char* text = luaL_checkstring(L, 3);
    const ImU32 color = checkColor(L, 4);
    if (ImFont* font = ImGui::GetIO().Fonts->Fonts[0])
        drawList->AddText(font, kScriptFontSize, ImVec2{x, y}, color, text);
    return 0;
}

inline int l_renderTextSize(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    if (ImFont* font = ImGui::GetIO().Fonts->Fonts[0]) {
        const ImVec2 size = font->CalcTextSizeA(kScriptFontSize, FLT_MAX, 0.0f, text);
        lua_pushnumber(L, size.x);
        lua_pushnumber(L, size.y);
    } else {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
    }
    return 2;
}

inline int l_renderLine(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x1 = static_cast<float>(luaL_checknumber(L, 1));
    const float y1 = static_cast<float>(luaL_checknumber(L, 2));
    const float x2 = static_cast<float>(luaL_checknumber(L, 3));
    const float y2 = static_cast<float>(luaL_checknumber(L, 4));
    const ImU32 color = checkColor(L, 5);
    const float thickness = lua_isnoneornil(L, 9) ? 1.0f : static_cast<float>(luaL_checknumber(L, 9));
    drawList->AddLine(ImVec2{x1, y1}, ImVec2{x2, y2}, color, thickness);
    return 0;
}

inline int l_renderRect(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float w = static_cast<float>(luaL_checknumber(L, 3));
    const float h = static_cast<float>(luaL_checknumber(L, 4));
    const ImU32 color = checkColor(L, 5);
    const float thickness = lua_isnoneornil(L, 9) ? 1.0f : static_cast<float>(luaL_checknumber(L, 9));
    drawList->AddRect(ImVec2{x, y}, ImVec2{x + w, y + h}, color, 0.0f, 0, thickness);
    return 0;
}

inline int l_renderFilledRect(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float w = static_cast<float>(luaL_checknumber(L, 3));
    const float h = static_cast<float>(luaL_checknumber(L, 4));
    const ImU32 color = checkColor(L, 5);
    drawList->AddRectFilled(ImVec2{x, y}, ImVec2{x + w, y + h}, color, 0.0f);
    return 0;
}

inline int l_renderCircle(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float radius = static_cast<float>(luaL_checknumber(L, 3));
    const ImU32 color = checkColor(L, 4);
    const int segments = lua_isnoneornil(L, 8) ? 0 : static_cast<int>(luaL_checknumber(L, 8));
    drawList->AddCircle(ImVec2{x, y}, radius, color, segments);
    return 0;
}

inline int l_renderCircleFilled(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float radius = static_cast<float>(luaL_checknumber(L, 3));
    const ImU32 color = checkColor(L, 4);
    const int segments = lua_isnoneornil(L, 8) ? 0 : static_cast<int>(luaL_checknumber(L, 8));
    drawList->AddCircleFilled(ImVec2{x, y}, radius, color, segments);
    return 0;
}

// World point -> screen pixels, or nil when the point is behind the camera or the view-projection
// matrix is not available this frame. The bridge returns NDC (-1..1, y up); this converts to
// drawn coordinates (y grows down) using the ImGui display size.
inline int l_worldToScreen(lua_State* L)
{
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    if (!worldToScreenQuery
        || !worldToScreenQuery(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
            static_cast<float>(luaL_checknumber(L, 3)), &ndcX, &ndcY))
        return 0; // nil
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    lua_pushnumber(L, (ndcX + 1.0f) * 0.5f * display.x);
    lua_pushnumber(L, (1.0f - ndcY) * 0.5f * display.y);
    return 2;
}

// ---- memory ----

inline int l_moduleBase(lua_State* L)
{
    const char* moduleName = luaL_checkstring(L, 1);
    const LinuxDynamicLibrary library{moduleName};
    const link_map* const map = library.getLinkMap();
    if (!map || !map->l_addr)
        return 0; // nil - module not loaded
    lua_pushlightuserdata(L, reinterpret_cast<void*>(map->l_addr));
    return 1;
}

int l_patternScan(lua_State* L)
{
    const char* moduleName = luaL_checkstring(L, 1);
    const char* pattern = luaL_checkstring(L, 2);

    // Parse FIRST: invalid patterns error out before any module lookup, so bad input from a
    // script never depends on the module being loaded.
    PatternByte bytes[kMaxPatternBytes];
    const int byteCount = parseIdaPattern(pattern, bytes, kMaxPatternBytes);
    if (byteCount == 0)
        return luaL_error(L, "pattern_scan: invalid or too long pattern");

    const LinuxDynamicLibrary library{moduleName};
    if (!library)
        return 0; // nil - module not loaded
    const MemorySection code = library.getCodeSection();
    if (const unsigned char* match = scanMemoryPattern(reinterpret_cast<const unsigned char*>(code.raw().data()), code.raw().size(), bytes, byteCount)) {
        lua_pushlightuserdata(L, const_cast<unsigned char*>(match));
        return 1;
    }
    return 0; // nil - not found
}

// ---- http ----

inline int l_httpGet(lua_State* L)
{
    const char* url = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // The URL lands inside a single-quoted shell string - forbid everything that could break
    // out of it (scripts receive arbitrary text from anywhere they fetch from, but the URL
    // itself is script-controlled; still, fail closed).
    for (const char* p = url; *p != '\0'; ++p) {
        if (*p == '\'' || *p == '"' || *p == '`' || *p == '\n' || *p == '\r' || *p == ';' || *p == '\\')
            return luaL_error(L, "http.get: url contains a forbidden character");
    }

    HttpSlot* slot = nullptr;
    int slotIndex = 0;
    for (int i = 0; i < kMaxHttpSlots; ++i) {
        if (!httpSlots[i].active) {
            slot = &httpSlots[i];
            slotIndex = i;
            break;
        }
    }
    if (!slot)
        return luaL_error(L, "http.get: too many concurrent requests");

    std::snprintf(slot->outPath, sizeof(slot->outPath), "/tmp/ns_lua_http_%d.txt", slotIndex);
    ::unlink(slot->outPath);
    slot->active = true;
    slot->processDone = false;
    slot->pid = 0;
    slot->scriptIndex = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));
    lua_pushvalue(L, 2);
    slot->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

    char command[1024];
    std::snprintf(command, sizeof(command), "curl -s --max-time 20 '%s' -o %s.part && mv -f %s.part %s",
        url, slot->outPath, slot->outPath, slot->outPath);
    slot->pid = spawnHostShell(command);
    if (slot->pid == 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, slot->callbackRef);
        *slot = HttpSlot{};
        return luaL_error(L, "http.get: failed to spawn curl");
    }
    return 0;
}

// ---- gui (script-owned menu items; values live in Script::guiItems) ----

inline const char* checkGuiLabel(lua_State* L, int index)
{
    const char* label = luaL_checkstring(L, index);
    const std::size_t length = std::strlen(label);
    if (length == 0 || length >= kMaxGuiLabel)
        luaL_error(L, "gui label must be 1-%d characters", static_cast<int>(kMaxGuiLabel) - 1);
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(label[i]);
        if (c < 0x20 || c == '=')
            luaL_error(L, "gui label contains a forbidden character");
    }
    return label;
}

inline GuiItem& guiItemAt(lua_State* L, int id)
{
    Script& script = selfScript(L);
    if (id < 1 || id > script.guiItemCount)
        luaL_error(L, "invalid gui item id %d", id);
    return script.guiItems[id - 1];
}

inline int l_guiCheckbox(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    const bool defaultValue = lua_toboolean(L, 2) != 0;
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Checkbox;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.boolValue = defaultValue;
    if (const auto* saved = findPendingGuiDefault(script.name, label))
        item.boolValue = saved->boolValue; // sidecar value wins over the script default
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}

inline int l_guiSlider(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    const int min = static_cast<int>(luaL_checkinteger(L, 2));
    const int max = static_cast<int>(luaL_checkinteger(L, 3));
    const int defaultValue = lua_isnoneornil(L, 4) ? min : static_cast<int>(luaL_checkinteger(L, 4));
    if (min > max)
        return luaL_error(L, "gui.slider: min must not be greater than max");
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Slider;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.minValue = min;
    item.maxValue = max;
    item.intValue = defaultValue < min ? min : (defaultValue > max ? max : defaultValue);
    if (const auto* saved = findPendingGuiDefault(script.name, label)) {
        const int value = saved->intValue;
        item.intValue = value < min ? min : (value > max ? max : value);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}

// gui.dropdown(label, options [, defaultIndex]) - a select row in the script's menu section.
// Options are copied into the GuiItem (both the char storage and a pointer table, so the popup
// layer can render across frames); gui.get returns the selected INDEX. The saved sidecar value
// is applied by label like every other gui item.
inline int l_guiDropdown(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const int optionCount = static_cast<int>(lua_objlen(L, 2));
    if (optionCount < 1)
        return luaL_error(L, "gui.dropdown: options table must not be empty");
    if (optionCount > kMaxGuiOptions)
        return luaL_error(L, "gui.dropdown: too many options (max %d)", kMaxGuiOptions);
    const int defaultValue = lua_isnoneornil(L, 3) ? 0 : static_cast<int>(luaL_checkinteger(L, 3));
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Dropdown;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.optionCount = optionCount;
    for (int i = 1; i <= optionCount; ++i) {
        lua_rawgeti(L, 2, i);
        const char* option = luaL_checkstring(L, -1);
        std::strncpy(item.optionStorage[i - 1], option, kMaxGuiLabel - 1);
        item.optionPtrs[i - 1] = item.optionStorage[i - 1];
        lua_pop(L, 1);
    }
    item.intValue = defaultValue < 0 ? 0 : (defaultValue >= optionCount ? optionCount - 1 : defaultValue);
    if (const auto* saved = findPendingGuiDefault(script.name, label)) {
        const int value = saved->intValue;
        item.intValue = value < 0 ? 0 : (value >= optionCount ? optionCount - 1 : value);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}

inline int l_guiGet(lua_State* L)
{
    GuiItem& item = guiItemAt(L, static_cast<int>(luaL_checkinteger(L, 1)));
    if (item.type == GuiItem::Type::Checkbox)
        lua_pushboolean(L, item.boolValue ? 1 : 0);
    else
        lua_pushinteger(L, item.intValue);
    return 1;
}

inline int l_guiSet(lua_State* L)
{
    GuiItem& item = guiItemAt(L, static_cast<int>(luaL_checkinteger(L, 1)));
    if (item.type == GuiItem::Type::Checkbox) {
        item.boolValue = lua_toboolean(L, 2) != 0;
    } else {
        const int value = static_cast<int>(luaL_checkinteger(L, 2));
        item.intValue = value < item.minValue ? item.minValue : (value > item.maxValue ? item.maxValue : value);
    }
    return 0;
}

// ---- entity (schema-driven reads through the EntryPoints-installed bridges) ----

inline constexpr int kMaxEntityIndex = 0x7FFE;    // cs2::kMaxValidEntityIndex
inline constexpr int kMaxSchemaFieldOffset = 0x100000; // 1 MiB sanity cap on schema offsets
inline constexpr int kMaxEntityStringBytes = 128; // m_iszPlayerName-scale fixed char arrays

inline bool entityBridgesAvailable()
{
    return localPlayerIndexQuery && entityFromIndexQuery && schemaFieldOffsetQuery;
}

inline int l_getLocalPlayer(lua_State* L)
{
    const int index = localPlayerIndexQuery ? localPlayerIndexQuery() : 0;
    if (index <= 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, index);
    return 1;
}

inline int l_getPlayers(lua_State* L)
{
    lua_newtable(L);
    if (!playerListQuery)
        return 1;
    PlayerListEntry entries[64];
    const int count = playerListQuery(entries, 64);
    int out = 0;
    for (int i = 0; i < count; ++i) {
        if (entries[i].controllerIndex <= 0)
            continue;
        lua_pushinteger(L, entries[i].controllerIndex);
        lua_rawseti(L, -2, ++out);
    }
    return 1;
}

inline int l_getPlayerPawn(lua_State* L)
{
    const int controllerIndex = static_cast<int>(luaL_checkinteger(L, 1));
    if (!playerListQuery || controllerIndex <= 0) {
        lua_pushnil(L);
        return 1;
    }
    PlayerListEntry entries[64];
    const int count = playerListQuery(entries, 64);
    for (int i = 0; i < count; ++i) {
        if (entries[i].controllerIndex == controllerIndex) {
            lua_pushinteger(L, entries[i].pawnIndex);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// Validates the common arguments and resolves the schema offset + entity pointer for
// (entityIndex, className, fieldName). Returns false with nil pushed when the entity/schema
// data is unavailable (not in a game, unknown field); errors on malformed arguments.
inline bool resolveEntityProp(lua_State* L, const std::byte** outEntity, int* outOffset)
{
    const int entityIndex = static_cast<int>(luaL_checkinteger(L, 1));
    const char* className = luaL_checkstring(L, 2);
    const char* fieldName = luaL_checkstring(L, 3);

    if (!entityBridgesAvailable()) {
        lua_pushnil(L);
        return false;
    }
    if (entityIndex < 0 || entityIndex > kMaxEntityIndex)
        luaL_error(L, "entity index out of range");
    if (className[0] == '\0' || fieldName[0] == '\0' || std::strlen(className) > 96 || std::strlen(fieldName) > 96)
        luaL_error(L, "invalid class or field name");

    const int offset = schemaFieldOffsetQuery(className, fieldName);
    const auto* entity = static_cast<const std::byte*>(entityFromIndexQuery(entityIndex));
    if (offset <= 0 || offset > kMaxSchemaFieldOffset || !entity) {
        lua_pushnil(L);
        return false;
    }
    *outEntity = entity;
    *outOffset = offset;
    return true;
}

inline int l_getProp(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    std::int32_t value = 0;
    std::memcpy(&value, entity + offset, sizeof(value));
    lua_pushinteger(L, value);
    return 1;
}

inline int l_getPropFloat(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    float value = 0.0f;
    std::memcpy(&value, entity + offset, sizeof(value));
    lua_pushnumber(L, value);
    return 1;
}

inline int l_getPropString(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    char buffer[kMaxEntityStringBytes + 1] = {};
    std::memcpy(buffer, entity + offset, kMaxEntityStringBytes); // fixed in-object char array
    std::size_t length = 0;
    while (length < kMaxEntityStringBytes && buffer[length] != '\0') {
        if (static_cast<unsigned char>(buffer[length]) < 0x20) {
            length = 0; // control byte - not a clean string, refuse rather than guess
            break;
        }
        ++length;
    }
    if (length == 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, buffer);
    return 1;
}

// The write counterparts to get_prop/get_prop_float - same 4-byte poke, same schema-verified
// offset. Writes land in live game memory, so they are enforced to the game thread only (the
// same rule client.exec follows); reads stay callable from any callback.
inline int l_setProp(lua_State* L)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        return luaL_error(L, "entity.set_prop is only available inside createmove / game event callbacks");
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    const std::int32_t value = static_cast<std::int32_t>(luaL_checkinteger(L, 4));
    std::memcpy(const_cast<std::byte*>(entity) + offset, &value, sizeof(value));
    return 0;
}

inline int l_setPropFloat(lua_State* L)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        return luaL_error(L, "entity.set_prop_float is only available inside createmove / game event callbacks");
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    const float value = static_cast<float>(luaL_checknumber(L, 4));
    std::memcpy(const_cast<std::byte*>(entity) + offset, &value, sizeof(value));
    return 0;
}

// ---- net (game-server endpoint + raw datagram send through the original sendto) ----

inline int l_netServer(lua_State* L)
{
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    if (!netlag_hook::getServerEndpoint(&fd, &addr, &addrLen))
        return 0; // nil - no endpoint captured (not connected)

    char ip[INET6_ADDRSTRLEN] = {};
    int port = 0;
    if (addr.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        port = ntohs(sin->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
        port = ntohs(sin6->sin6_port);
    } else {
        return 0;
    }
    lua_pushinteger(L, fd);
    lua_pushstring(L, ip);
    lua_pushinteger(L, port);
    return 3;
}

inline int l_netSendRaw(lua_State* L)
{
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    const int count = lua_isnoneornil(L, 2) ? 1 : static_cast<int>(luaL_checkinteger(L, 2));
    if (count < 1)
        return luaL_error(L, "net.send_raw: count must be >= 1");
    if (length == 0 || length > netlag_hook::kMaxDatagram)
        return luaL_error(L, "net.send_raw: payload must be 1-%d bytes", static_cast<int>(netlag_hook::kMaxDatagram));

    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    if (!netlag_hook::getServerEndpoint(&fd, &addr, &addrLen))
        return 0; // nil - not connected
    lua_pushinteger(L, netlag_hook::sendRawToServer(reinterpret_cast<const unsigned char*>(data), length, static_cast<std::uint32_t>(count)));
    return 1;
}

// Parses "a.b.c.d" or "a.b.c.d/prefix" into a host-byte-order IPv4 (host bits masked off).
// Returns false on anything else - malformed input just does not join the block list.
inline bool parseIpv4Cidr(const char* text, std::uint32_t* out) noexcept
{
    unsigned parts[4] = {};
    int part = 0;
    int digits = 0;
    const char* p = text;
    for (; *p != '\0' && *p != '/'; ++p) {
        if (*p >= '0' && *p <= '9') {
            parts[part] = parts[part] * 10 + static_cast<unsigned>(*p - '0');
            if (parts[part] > 255)
                return false;
            if (++digits > 3)
                return false;
        } else if (*p == '.' && part < 3 && digits > 0) {
            ++part;
            digits = 0;
        } else {
            return false;
        }
    }
    if (part != 3 || digits == 0)
        return false;
    int prefix = 32;
    if (*p == '/') {
        prefix = 0;
        for (++p; *p >= '0' && *p <= '9'; ++p)
            prefix = prefix * 10 + (*p - '0');
        if (*p != '\0' || prefix < 0 || prefix > 32)
            return false;
    }
    std::uint32_t value = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    if (prefix == 0)
        value = 0;
    else if (prefix < 32)
        value &= ~((1u << (32 - prefix)) - 1);
    *out = value;
    return true;
}

// net.set_blocked_ips({"155.133.248.36", ...}) - publish the region-filter block list. String
// table entries are parsed (deduped, sorted) into the seqlock-protected list the send hook
// consults; see NetLag.h net_region. Returns the number of entries kept.
inline int l_netSetBlockedIps(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const int count = static_cast<int>(lua_objlen(L, 1));
    if (count > static_cast<int>(net_region::kMaxBlockedIps))
        return luaL_error(L, "net.set_blocked_ips: too many entries (max %d)", static_cast<int>(net_region::kMaxBlockedIps));

    std::uint32_t ips[net_region::kMaxBlockedIps];
    int kept = 0;
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(L, 1, i);
        const char* entry = luaL_checkstring(L, -1);
        std::uint32_t value = 0;
        if (parseIpv4Cidr(entry, &value))
            ips[kept++] = value;
        lua_pop(L, 1);
    }
    net_region::publishBlockedIps(ips, static_cast<std::size_t>(kept));
    lua_pushinteger(L, kept);
    return 1;
}

inline int l_netClearBlockedIps(lua_State* L)
{
    net_region::clearBlockedIps();
    return 0;
}

inline int l_netStats(lua_State* L)
{
    lua_newtable(L);
    const auto set = [L](const char* key, const std::atomic<std::uint64_t>& counter) {
        lua_pushinteger(L, static_cast<lua_Integer>(counter.load(std::memory_order_relaxed)));
        lua_setfield(L, -2, key);
    };
    set("sends", net_lag::statSends);
    set("passed", net_lag::statPassed);
    set("dropped", net_lag::statDropped);
    set("duped", net_lag::statDuped);
    set("flooded", net_lag::statFlooded);
    set("connless", net_lag::statConnless);
    set("raw", net_lag::statRaw);
    set("blips", net_lag::statBlips);
    set("delayed", net_lag::statDelayed);
    set("flushed", net_lag::statFlushed);
    set("overflow", net_lag::statOverflow);
    set("region_blocked", net_region::statRegionBlocked);
    return 1;
}

// ---- registration ----

inline void registerApi(lua_State* L, int scriptIndex)
{
    const auto pushClosure = [L, scriptIndex](lua_CFunction fn) {
        lua_pushinteger(L, scriptIndex);
        lua_pushcclosure(L, fn, 1);
    };
    const auto pushFunction = [L](lua_CFunction fn) {
        lua_pushcfunction(L, fn);
    };

    lua_newtable(L);
    pushClosure(l_setEventCallback); lua_setfield(L, -2, "set_event_callback");
    pushClosure(l_clientLog);        lua_setfield(L, -2, "log");
    pushClosure(l_clientExec);       lua_setfield(L, -2, "exec");
    pushClosure(l_getTime);          lua_setfield(L, -2, "get_time");
    pushClosure(l_getScreenSize);    lua_setfield(L, -2, "get_screen_size");
    pushClosure(l_isMenuOpen);       lua_setfield(L, -2, "is_menu_open");
    lua_setglobal(L, "client");

    lua_newtable(L);
    pushClosure(l_cvarGetInt);    lua_setfield(L, -2, "get_int");
    pushClosure(l_cvarGetFloat);  lua_setfield(L, -2, "get_float");
    pushClosure(l_cvarSetFloat);  lua_setfield(L, -2, "set_float");
    pushClosure(l_cvarSetBool);   lua_setfield(L, -2, "set_bool");
    lua_setglobal(L, "cvar");

    lua_newtable(L);
    pushFunction(l_renderText);         lua_setfield(L, -2, "text");
    pushFunction(l_renderTextSize);     lua_setfield(L, -2, "text_size");
    pushFunction(l_renderLine);         lua_setfield(L, -2, "line");
    pushFunction(l_renderRect);         lua_setfield(L, -2, "rect");
    pushFunction(l_renderFilledRect);   lua_setfield(L, -2, "filled_rect");
    pushFunction(l_renderCircle);       lua_setfield(L, -2, "circle");
    pushFunction(l_renderCircleFilled); lua_setfield(L, -2, "circle_filled");
    pushFunction(l_worldToScreen);      lua_setfield(L, -2, "world_to_screen");
    lua_setglobal(L, "renderer");

    lua_newtable(L);
    pushFunction(l_moduleBase);  lua_setfield(L, -2, "module_base");
    pushFunction(l_patternScan); lua_setfield(L, -2, "pattern_scan");
    lua_setglobal(L, "memory");

    lua_newtable(L);
    pushClosure(l_getLocalPlayer);  lua_setfield(L, -2, "get_local_player");
    pushClosure(l_getPlayers);      lua_setfield(L, -2, "get_players");
    pushClosure(l_getPlayerPawn);   lua_setfield(L, -2, "get_player_pawn");
    pushClosure(l_getProp);         lua_setfield(L, -2, "get_prop");
    pushClosure(l_getPropFloat);    lua_setfield(L, -2, "get_prop_float");
    pushClosure(l_getPropString);   lua_setfield(L, -2, "get_prop_string");
    pushClosure(l_setProp);         lua_setfield(L, -2, "set_prop");
    pushClosure(l_setPropFloat);    lua_setfield(L, -2, "set_prop_float");
    lua_setglobal(L, "entity");

    lua_newtable(L);
    pushClosure(l_guiCheckbox); lua_setfield(L, -2, "checkbox");
    pushClosure(l_guiSlider);   lua_setfield(L, -2, "slider");
    pushClosure(l_guiDropdown); lua_setfield(L, -2, "dropdown");
    pushClosure(l_guiGet);      lua_setfield(L, -2, "get");
    pushClosure(l_guiSet);      lua_setfield(L, -2, "set");
    lua_setglobal(L, "gui");

    lua_newtable(L);
    pushClosure(l_httpGet); lua_setfield(L, -2, "get");
    lua_setglobal(L, "http");

    lua_newtable(L);
    pushClosure(l_netServer);          lua_setfield(L, -2, "server");
    pushClosure(l_netSendRaw);         lua_setfield(L, -2, "send_raw");
    pushClosure(l_netSetBlockedIps);   lua_setfield(L, -2, "set_blocked_ips");
    pushClosure(l_netClearBlockedIps); lua_setfield(L, -2, "clear_blocked_ips");
    pushClosure(l_netStats);           lua_setfield(L, -2, "stats");
    lua_setglobal(L, "net");
}
