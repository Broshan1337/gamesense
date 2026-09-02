#pragma once

// Script-facing Lua API.
//
// INCLUDED INSIDE namespace lua (from LuaManager.h) - do not include headers here and do not
// open a namespace: everything this file needs (lua.h, imgui.h, VerifyConsole,
// LinuxDynamicLibrary, the state structs and helpers) is already visible at the include point.
//
// API surface v1:
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
//                                           x/y/z floats). Other events pass nil.
//   client.log(message)                   - engine console (VerifyConsole, throttled)
//   client.get_screen_size()              - width, height
//   client.is_menu_open()
//   renderer.text(x, y, text, r, g, b, a)
//   renderer.text_size(text)              - width, height
//   renderer.line(x1, y1, x2, y2, r, g, b, a [, thickness])
//   renderer.rect(x, y, w, h, r, g, b, a [, thickness])
//   renderer.filled_rect(x, y, w, h, r, g, b, a)
//   renderer.circle(x, y, radius, r, g, b, a [, segments])
//   renderer.circle_filled(x, y, radius, r, g, b, a [, segments])
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
//   gui.checkbox(label [, default])       - menu checkbox in the script's section; 1-based id
//   gui.slider(label, min, max [, default]) - menu slider; 1-based id
//   gui.get(id)                           - current value (boolean / integer)
//   gui.set(id, value)                    - set from the script; values persist per script
//                                           (sidecar <scriptsDir>/<name>.gui, applied by label
//                                           on load, written on unload)
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
    pushClosure(l_getScreenSize);    lua_setfield(L, -2, "get_screen_size");
    pushClosure(l_isMenuOpen);       lua_setfield(L, -2, "is_menu_open");
    lua_setglobal(L, "client");

    lua_newtable(L);
    pushFunction(l_renderText);         lua_setfield(L, -2, "text");
    pushFunction(l_renderTextSize);     lua_setfield(L, -2, "text_size");
    pushFunction(l_renderLine);         lua_setfield(L, -2, "line");
    pushFunction(l_renderRect);         lua_setfield(L, -2, "rect");
    pushFunction(l_renderFilledRect);   lua_setfield(L, -2, "filled_rect");
    pushFunction(l_renderCircle);       lua_setfield(L, -2, "circle");
    pushFunction(l_renderCircleFilled); lua_setfield(L, -2, "circle_filled");
    lua_setglobal(L, "renderer");

    lua_newtable(L);
    pushFunction(l_moduleBase);  lua_setfield(L, -2, "module_base");
    pushFunction(l_patternScan); lua_setfield(L, -2, "pattern_scan");
    lua_setglobal(L, "memory");

    lua_newtable(L);
    pushClosure(l_getLocalPlayer); lua_setfield(L, -2, "get_local_player");
    pushClosure(l_getPlayers);     lua_setfield(L, -2, "get_players");
    pushClosure(l_getPlayerPawn);  lua_setfield(L, -2, "get_player_pawn");
    pushClosure(l_getProp);        lua_setfield(L, -2, "get_prop");
    pushClosure(l_getPropFloat);   lua_setfield(L, -2, "get_prop_float");
    pushClosure(l_getPropString);  lua_setfield(L, -2, "get_prop_string");
    lua_setglobal(L, "entity");

    lua_newtable(L);
    pushClosure(l_guiCheckbox); lua_setfield(L, -2, "checkbox");
    pushClosure(l_guiSlider);   lua_setfield(L, -2, "slider");
    pushClosure(l_guiGet);      lua_setfield(L, -2, "get");
    pushClosure(l_guiSet);      lua_setfield(L, -2, "set");
    lua_setglobal(L, "gui");

    lua_newtable(L);
    pushClosure(l_httpGet); lua_setfield(L, -2, "get");
    lua_setglobal(L, "http");
}
