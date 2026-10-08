#pragma once

















































































































































































































































































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
        luaL_error(L, "renderer is only available inside the 'paint' callback"); 
        return nullptr; 
    }
    return paintDrawList;
}



inline void registerEventCallback(lua_State* L, const char* eventName)
{
    lua_pushliteral(L, "__ns_callbacks");
    lua_rawget(L, LUA_REGISTRYINDEX); 
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);                            
        lua_pushliteral(L, "__ns_callbacks");       
        lua_pushvalue(L, -2);                       
        lua_rawset(L, LUA_REGISTRYINDEX);           
    }
    lua_pushstring(L, eventName);                   
    lua_rawget(L, -2);                              
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);                              
        lua_newtable(L);                            
        lua_pushstring(L, eventName);               
        lua_pushvalue(L, -2);                       
        lua_rawset(L, -4);                          
    }
    const int count = static_cast<int>(lua_objlen(L, -1));
    lua_pushvalue(L, 2); 
    lua_rawseti(L, -2, count + 1);
    lua_pop(L, 2); 
}




inline constexpr int kBindOff = 0;
inline constexpr int kBindLast = 253;
inline constexpr int kBindMaxScancode = 248;



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
    else if (std::strcmp(eventName, "menu") == 0)
        script.hasMenu = true;
    else if (std::strcmp(eventName, "unload") == 0)
        script.hasUnload = true; 
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





inline double luaNow() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

inline int l_getTime(lua_State* L)
{
    lua_pushnumber(L, luaNow());
    return 1;
}





inline int l_getMapName(lua_State* L)
{
    char name[64];
    if (copyMapName(name, sizeof(name)))
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
    return 1;
}



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


inline int l_playSound(lua_State* L)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        return luaL_error(L, "client.play_sound is only available inside createmove / game event callbacks");
    const char* path = luaL_checkstring(L, 1);
    const std::size_t length = std::strlen(path);
    if (length == 0 || length >= 200)
        return luaL_error(L, "client.play_sound: path must be 1-199 characters");
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\'' || *p == ';' || *p == '\n' || *p == '\r' || *p == '`' || *p == '\\')
            return luaL_error(L, "client.play_sound: path contains a forbidden character");
    }
    if (!engineCommandQuery)
        return luaL_error(L, "client.play_sound: engine bridge not installed");
    char command[224];
    std::snprintf(command, sizeof(command), "play %s", path);
    engineCommandQuery(command);
    return 0;
}




inline int l_delayCall(lua_State* L)
{
    const double seconds = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!(seconds >= 0.0) || seconds > 3600.0)
        return luaL_error(L, "client.delay_call: seconds must be 0-3600");
    Script& script = selfScript(L);
    if (script.pendingCallCount >= kMaxPendingCalls)
        return luaL_error(L, "client.delay_call: too many pending calls (max %d)", kMaxPendingCalls);

    PendingCall& call = script.pendingCalls[script.pendingCallCount];
    call = PendingCall{};
    call.when = luaNow() + seconds;
    lua_pushvalue(L, 2); 
    call.functionRef = luaL_ref(L, LUA_REGISTRYINDEX);

    const int argCount = lua_gettop(L) - 2;
    if (argCount > 0) {
        lua_createtable(L, 0, argCount);
        for (int i = 1; i <= argCount; ++i) {
            lua_pushvalue(L, 2 + i);
            lua_rawseti(L, -2, i);
        }
        call.argsRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    ++script.pendingCallCount;
    fprintf(stderr, "[dbg] delayCall slot=%d count=%d self=%p scripts=%p name=%s\n", static_cast<int>(lua_tointeger(L, lua_upvalueindex(1))), script.pendingCallCount, (void*)&script, (void*)scripts, script.name);
    return 0;
}




inline int l_notify(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    std::uint32_t color = IM_COL32(220, 223, 232, 255);
    if (!lua_isnoneornil(L, 2)) {
        const auto channel = [L](int index) -> int {
            const lua_Number value = luaL_checknumber(L, index);
            if (value < 0.0 || value > 255.0)
                return luaL_error(L, "notify: color components must be in range 0-255"), 0;
            return static_cast<int>(value);
        };
        color = IM_COL32(channel(2), channel(3), channel(4), 255);
    }

    Toast& toast = toasts[toastCursor];
    toastCursor = (toastCursor + 1) % kMaxToasts;
    toast.active = true;
    toast.when = luaNow();
    toast.color = color;
    std::size_t out = 0;
    for (const char* p = text; *p != '\0' && out < kMaxToastText - 1; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x20)
            toast.text[out++] = *p;
    }
    toast.text[out] = '\0';
    return 0;
}

inline int l_clipboardGet(lua_State* L)
{
    const char* text = ImGui::GetClipboardText();
    lua_pushstring(L, text ? text : "");
    return 1;
}

inline int l_clipboardSet(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    ImGui::SetClipboardText(text);
    return 0;
}

inline int l_getMousePos(lua_State* L)
{
    const ImVec2 pos = ImGui::GetIO().MousePos;
    lua_pushnumber(L, pos.x);
    lua_pushnumber(L, pos.y);
    return 2;
}


inline int l_isMouseDown(lua_State* L)
{
    const int button = lua_isnoneornil(L, 1) ? 0 : static_cast<int>(luaL_checkinteger(L, 1));
    bool down = false;
    if (button == 0)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    else if (button == 1)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    else if (button == 2)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}



inline int checkBindValue(lua_State* L, int index);





inline int l_isBindDown(lua_State* L)
{
    const int bind = checkBindValue(L, 1);
    bool down = false;
    if (bind >= 1 && bind <= kBindMaxScancode)
        down = KeyboardState::isKeyDown(bind);
    else if (bind == 249)
        down = ImGui::IsMouseDown(3); 
    else if (bind == 250)
        down = ImGui::IsMouseDown(4); 
    else if (bind == 251)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    else if (bind == 252)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    else if (bind == 253)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}




inline void* localPawnEntity()
{
    const int controller = localPlayerIndexQuery ? localPlayerIndexQuery() : 0;
    if (controller <= 0 || !playerListQuery || !entityFromIndexQuery)
        return nullptr;
    PlayerListEntry entries[64];
    const int count = playerListQuery(entries, 64);
    for (int i = 0; i < count; ++i) {
        if (entries[i].controllerIndex == controller)
            return entityFromIndexQuery(entries[i].pawnIndex);
    }
    return nullptr;
}


inline void requireGameThread(lua_State* L, const char* api)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        luaL_error(L, "%s is only available inside createmove / game event callbacks", api);
}


inline void checkTracePoints(lua_State* L, int index, cs2::Vector& start, cs2::Vector& end)
{
    start.x = static_cast<float>(luaL_checknumber(L, index));
    start.y = static_cast<float>(luaL_checknumber(L, index + 1));
    start.z = static_cast<float>(luaL_checknumber(L, index + 2));
    end.x = static_cast<float>(luaL_checknumber(L, index + 3));
    end.y = static_cast<float>(luaL_checknumber(L, index + 4));
    end.z = static_cast<float>(luaL_checknumber(L, index + 5));
}



inline int l_traceLine(lua_State* L)
{
    requireGameThread(L, "client.trace_line");
    cs2::Vector start{};
    cs2::Vector end{};
    checkTracePoints(L, 1, start, end);
    const bool skipLocal = lua_isnoneornil(L, 7) || lua_toboolean(L, 7) != 0;
    void* skipEntity = skipLocal ? localPawnEntity() : nullptr;

    const Tracing::Result result = Tracing::traceLine(start, end, skipEntity);
    lua_pushnumber(L, result.fraction);
    lua_pushnumber(L, result.endPos.x);
    lua_pushnumber(L, result.endPos.y);
    lua_pushnumber(L, result.endPos.z);
    lua_pushboolean(L, result.didHit ? 1 : 0);
    return 5;
}




inline int l_traceDamage(lua_State* L)
{
    requireGameThread(L, "client.trace_damage");
    cs2::Vector start{};
    cs2::Vector end{};
    checkTracePoints(L, 1, start, end);
    const float damageAtPoint = static_cast<float>(luaL_checknumber(L, 7));
    const float penetrationPower = static_cast<float>(luaL_checknumber(L, 8));
    const bool skipLocal = lua_isnoneornil(L, 9) || lua_toboolean(L, 9) != 0;
    void* skipEntity = skipLocal ? localPawnEntity() : nullptr;

    if (const auto damage = Autowall::penetratedDamage(start, end, skipEntity, nullptr, damageAtPoint, penetrationPower); damage.hasValue()) {
        lua_pushnumber(L, damage.value());
        return 1;
    }
    return 0; 
}



inline constexpr float kScriptFontSize = 14.0f;



inline int l_renderScale(lua_State* L)
{
    lua_pushnumber(L, static_cast<double>(neverlose::uiScale()));
    return 1;
}

inline int l_renderText(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const char* text = luaL_checkstring(L, 3);
    const ImU32 color = checkColor(L, 4);
    float size = kScriptFontSize;
    if (!lua_isnoneornil(L, 8)) {
        size = static_cast<float>(luaL_checknumber(L, 8));
        if (!(size >= 6.0f) || size > 96.0f)
            return luaL_error(L, "renderer.text: size must be 6-96");
    }
    const bool outline = lua_toboolean(L, 9) != 0;
    
    
    const bool bold = lua_toboolean(L, 10) != 0;
    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    if (bold) {
        auto& fonts = ImGui::GetIO().Fonts->Fonts;
        if (fonts.Size > 1 && fonts[1])
            font = fonts[1];
    }
    if (font) {
        if (outline) {
            const float o = size >= 20.0f ? 2.0f : 1.0f;
            const ImU32 shadow = IM_COL32(0, 0, 0, (color >> 24) & 255);
            drawList->AddText(font, size, ImVec2{x - o, y}, shadow, text);
            drawList->AddText(font, size, ImVec2{x + o, y}, shadow, text);
            drawList->AddText(font, size, ImVec2{x, y - o}, shadow, text);
            drawList->AddText(font, size, ImVec2{x, y + o}, shadow, text);
        }
        drawList->AddText(font, size, ImVec2{x, y}, color, text);
    }
    return 0;
}

inline int l_renderTextSize(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    float size = kScriptFontSize;
    if (!lua_isnoneornil(L, 2)) {
        size = static_cast<float>(luaL_checknumber(L, 2));
        if (!(size >= 6.0f) || size > 96.0f)
            return luaL_error(L, "renderer.text_size: size must be 6-96");
    }
    const bool bold = lua_toboolean(L, 3) != 0; 
    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    if (bold) {
        auto& fonts = ImGui::GetIO().Fonts->Fonts;
        if (fonts.Size > 1 && fonts[1])
            font = fonts[1];
    }
    if (font) {
        const ImVec2 dims = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        lua_pushnumber(L, dims.x);
        lua_pushnumber(L, dims.y);
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
    const float thickness = lua_isnoneornil(L, 9) ? 1.0f : static_cast<float>(luaL_checknumber(L, 9));
    if (!(thickness >= 1.0f) || thickness > 64.0f)
        return luaL_error(L, "renderer.circle: thickness must be 1-64");
    drawList->AddCircle(ImVec2{x, y}, radius, color, segments, thickness);
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




inline int l_worldToScreen(lua_State* L)
{
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    if (!worldToScreenQuery
        || !worldToScreenQuery(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
            static_cast<float>(luaL_checknumber(L, 3)), &ndcX, &ndcY))
        return 0; 
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    lua_pushnumber(L, (ndcX + 1.0f) * 0.5f * display.x);
    lua_pushnumber(L, (1.0f - ndcY) * 0.5f * display.y);
    return 2;
}






inline int l_renderPolygon(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    const ImU32 color = checkColor(L, 2);
    const bool filled = lua_toboolean(L, 6) != 0;
    const float thickness = lua_isnoneornil(L, 7) ? 1.0f : static_cast<float>(luaL_checknumber(L, 7));
    constexpr int kMaxPolygonPoints = 64;
    ImVec2 points[kMaxPolygonPoints];
    int count = 0;
    const int length = static_cast<int>(lua_objlen(L, 1));
    for (int i = 1; i <= length && count < kMaxPolygonPoints; ++i) {
        lua_rawgeti(L, 1, i);
        if (lua_istable(L, -1)) {
            lua_rawgeti(L, -1, 1);
            lua_rawgeti(L, -2, 2);
            points[count].x = static_cast<float>(luaL_checknumber(L, -2));
            points[count].y = static_cast<float>(luaL_checknumber(L, -1));
            lua_pop(L, 2);
            ++count;
        } else if (lua_isnumber(L, -1) && i + 1 <= length) {
            points[count].x = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
            lua_rawgeti(L, 1, i + 1);
            points[count].y = static_cast<float>(luaL_checknumber(L, -1));
            ++count;
            ++i; 
        }
        lua_pop(L, 1);
    }
    if (count < 3)
        return luaL_error(L, "renderer.polygon: at least 3 points required");
    if (filled)
        drawList->AddConvexPolyFilled(points, count, color);
    else
        drawList->AddPolyline(points, count, color, ImDrawFlags_Closed, thickness);
    return 0;
}



inline int l_renderArc(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float radius = static_cast<float>(luaL_checknumber(L, 3));
    const float startAngle = static_cast<float>(luaL_checknumber(L, 4)) * (3.14159265358979f / 180.0f);
    const float endAngle = static_cast<float>(luaL_checknumber(L, 5)) * (3.14159265358979f / 180.0f);
    const ImU32 color = checkColor(L, 6);
    const float thickness = lua_isnoneornil(L, 10) ? 1.0f : static_cast<float>(luaL_checknumber(L, 10));
    const int segments = lua_isnoneornil(L, 11) ? 24 : static_cast<int>(luaL_checknumber(L, 11));
    if (segments < 2)
        return luaL_error(L, "renderer.arc: segments must be >= 2");
    drawList->PathArcTo(ImVec2{x, y}, radius, startAngle, endAngle, segments);
    drawList->PathStroke(color, 0, thickness);
    return 0;
}


inline int l_renderGradientRect(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float w = static_cast<float>(luaL_checknumber(L, 3));
    const float h = static_cast<float>(luaL_checknumber(L, 4));
    const ImU32 colorLeft = checkColor(L, 5);
    const ImU32 colorRight = checkColor(L, 9);
    const bool vertical = lua_toboolean(L, 13) != 0;
    if (vertical)
        drawList->AddRectFilledMultiColor(ImVec2{x, y}, ImVec2{x + w, y + h}, colorLeft, colorLeft, colorRight, colorRight);
    else
        drawList->AddRectFilledMultiColor(ImVec2{x, y}, ImVec2{x + w, y + h}, colorLeft, colorRight, colorRight, colorLeft);
    return 0;
}


inline int l_renderRoundedRect(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float w = static_cast<float>(luaL_checknumber(L, 3));
    const float h = static_cast<float>(luaL_checknumber(L, 4));
    const float radius = static_cast<float>(luaL_checknumber(L, 5));
    const ImU32 color = checkColor(L, 6);
    const float thickness = lua_isnoneornil(L, 10) ? 1.0f : static_cast<float>(luaL_checknumber(L, 10));
    if (lua_toboolean(L, 11) != 0)
        drawList->AddRectFilled(ImVec2{x, y}, ImVec2{x + w, y + h}, color, radius);
    else
        drawList->AddRect(ImVec2{x, y}, ImVec2{x + w, y + h}, color, radius, 0, thickness);
    return 0;
}




inline int l_renderPushClip(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float w = static_cast<float>(luaL_checknumber(L, 3));
    const float h = static_cast<float>(luaL_checknumber(L, 4));
    if (w <= 0.0f || h <= 0.0f || w > 65536.0f || h > 65536.0f)
        return luaL_error(L, "renderer.push_clip: invalid size");
    drawList->PushClipRect(ImVec2{x, y}, ImVec2{x + w, y + h}, true);
    ++paintClipDepth;
    return 0;
}

inline int l_renderPopClip(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    if (paintClipDepth <= 0)
        return luaL_error(L, "renderer.pop_clip: clip stack is empty");
    drawList->PopClipRect();
    --paintClipDepth;
    return 0;
}



inline int l_moduleBase(lua_State* L)
{
    const char* moduleName = luaL_checkstring(L, 1);
    const LinuxDynamicLibrary library{moduleName};
    const link_map* const map = library.getLinkMap();
    if (!map || !map->l_addr)
        return 0; 
    lua_pushlightuserdata(L, reinterpret_cast<void*>(map->l_addr));
    return 1;
}

int l_patternScan(lua_State* L)
{
    const char* moduleName = luaL_checkstring(L, 1);
    const char* pattern = luaL_checkstring(L, 2);

    
    
    PatternByte bytes[kMaxPatternBytes];
    const int byteCount = parseIdaPattern(pattern, bytes, kMaxPatternBytes);
    if (byteCount == 0)
        return luaL_error(L, "pattern_scan: invalid or too long pattern");

    const LinuxDynamicLibrary library{moduleName};
    if (!library)
        return 0; 
    const MemorySection code = library.getCodeSection();
    if (const unsigned char* match = scanMemoryPattern(reinterpret_cast<const unsigned char*>(code.raw().data()), code.raw().size(), bytes, byteCount)) {
        lua_pushlightuserdata(L, const_cast<unsigned char*>(match));
        return 1;
    }
    return 0; 
}





inline HttpSlot* acquireHttpSlot(lua_State* L)
{
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
        return nullptr;
    std::snprintf(slot->outPath, sizeof(slot->outPath), "%s/ns_lua_http_%d.txt", ns_paths::root(), slotIndex);
    ::unlink(slot->outPath);
    slot->active = true;
    slot->processDone = false;
    slot->pid = 0;
    slot->scriptIndex = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));
    lua_pushvalue(L, -1); 
    slot->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return slot;
}


static void appendConfigValue(int fd, const char* value) noexcept
{
    for (const char* p = value; *p != '\0'; ++p) {
        char two[2] = {*p, '\0'};
        if (*p == '"' || *p == '\\')
            ::write(fd, "\\", 1);
        ::write(fd, two, 1);
    }
}




static bool writeCurlConfig(const char* configPath, const char* method, const char* url,
    const char* const* headers, int headerCount, const char* bodyPath) noexcept
{
    const int fd = ::open(configPath, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0)
        return false;
    auto line = [&](const char* key, const char* quoted) {
        ::write(fd, key, std::strlen(key));
        ::write(fd, " = \"", 4);
        appendConfigValue(fd, quoted);
        ::write(fd, "\"\n", 2);
    };
    line("url", url);
    if (method && std::strcmp(method, "GET") != 0)
        line("request", method);
    for (int i = 0; i < headerCount; ++i)
        line("header", headers[i]);
    if (bodyPath)
        line("data-binary", bodyPath);
    ::close(fd);
    return true;
}

inline int l_httpGet(lua_State* L)
{
    const char* url = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    
    
    
    for (const char* p = url; *p != '\0'; ++p) {
        if (*p == '\'' || *p == '"' || *p == '`' || *p == '\n' || *p == '\r' || *p == '\\' || static_cast<unsigned char>(*p) < 0x20)
            return luaL_error(L, "http.get: url contains a forbidden character");
    }

    
    
    
    
    
    HttpSlot* slot = acquireHttpSlot(L);
    if (!slot) {
        return luaL_error(L, "http.get: too many concurrent requests");
    }

    char configPath[ns_paths::kMaxPath];
    std::snprintf(configPath, sizeof(configPath), "%s/ns_lua_http_%d.cfg", ns_paths::root(), static_cast<int>(slot - httpSlots));
    ::unlink(configPath);
    if (!writeCurlConfig(configPath, "GET", url, nullptr, 0, nullptr)) {
        discardHttpSlot(L, slot);
        return luaL_error(L, "http.get: failed to write curl config");
    }

    char command[1024];
    std::snprintf(command, sizeof(command), "curl -s --max-time 20 -K '%s' -o '%s' && mv -f '%s' '%s'",
        configPath, slot->outPath, slot->outPath, slot->outPath);
    slot->pid = spawnHostShell(command);
    if (slot->pid == 0) {
        discardHttpSlot(L, slot);
        return luaL_error(L, "http.get: failed to spawn curl");
    }
    return 0;
}



inline int l_httpRequest(lua_State* L)
{
    const char* method = luaL_checkstring(L, 1);
    for (const char* p = method; *p != '\0'; ++p) {
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!ok)
            return luaL_error(L, "http.request: method must be letters only (GET/POST/...)");
    }
    const std::size_t methodLength = std::strlen(method);
    if (methodLength < 2 || methodLength > 10)
        return luaL_error(L, "http.request: method must be 2-10 characters");

    const char* url = luaL_checkstring(L, 2);
    for (const char* p = url; *p != '\0'; ++p) {
        if (*p == '\'' || *p == '"' || *p == '`' || *p == '\n' || *p == '\r' || *p == '\\' || static_cast<unsigned char>(*p) < 0x20)
            return luaL_error(L, "http.request: url contains a forbidden character");
    }

    int callbackIndex = 3;
    constexpr int kMaxHeaders = 16;
    const char* headers[kMaxHeaders];
    int headerCount = 0;
    const char* body = nullptr;
    std::size_t bodyLength = 0;
    if (lua_istable(L, 3)) {
        callbackIndex = 4;
        lua_getfield(L, 3, "headers");
        if (lua_istable(L, -1)) {
            const int count = static_cast<int>(lua_objlen(L, -1));
            if (count > kMaxHeaders)
                return luaL_error(L, "http.request: too many headers (max %d)", kMaxHeaders);
            for (int i = 1; i <= count; ++i) {
                lua_rawgeti(L, -1, i);
                const char* header = luaL_checkstring(L, -1);
                const std::size_t length = std::strlen(header);
                if (length == 0 || length > 512)
                    return luaL_error(L, "http.request: header must be 1-512 characters");
                for (const char* h = header; *h != '\0'; ++h) {
                    if (*h == '\n' || *h == '\r' || *h == '"')
                        return luaL_error(L, "http.request: header contains a forbidden character");
                }
                headers[headerCount++] = header;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, 3, "body");
        if (!lua_isnil(L, -1)) {
            body = luaL_checklstring(L, -1, &bodyLength);
            if (bodyLength > 64 * 1024)
                return luaL_error(L, "http.request: body must be at most 64KB");
        }
        lua_pop(L, 1);
    }
    luaL_checktype(L, callbackIndex, LUA_TFUNCTION);

    
    
    HttpSlot* slot = acquireHttpSlot(L);
    if (!slot) {
        return luaL_error(L, "http.request: too many concurrent requests");
    }
    const int slotIndex = static_cast<int>(slot - httpSlots);

    char configPath[ns_paths::kMaxPath];
    char bodyPath[ns_paths::kMaxPath] = "";
    std::snprintf(configPath, sizeof(configPath), "%s/ns_lua_http_%d.cfg", ns_paths::root(), slotIndex);
    ::unlink(configPath);
    if (body) {
        std::snprintf(bodyPath, sizeof(bodyPath), "%s/ns_lua_http_%d.body", ns_paths::root(), slotIndex);
        ::unlink(bodyPath);
        const int fd = ::open(bodyPath, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd < 0) {
            discardHttpSlot(L, slot);
            return luaL_error(L, "http.request: failed to write body file");
        }
        std::size_t written = 0;
        while (written < bodyLength) {
            const ssize_t bytes = ::write(fd, body + written, bodyLength - written);
            if (bytes <= 0) {
                ::close(fd);
                discardHttpSlot(L, slot);
                return luaL_error(L, "http.request: failed to write body file");
            }
            written += static_cast<std::size_t>(bytes);
        }
        ::close(fd);
    }
    if (!writeCurlConfig(configPath, method, url, headers, headerCount, body ? bodyPath : nullptr)) {
        discardHttpSlot(L, slot);
        return luaL_error(L, "http.request: failed to write curl config");
    }

    char command[1024];
    std::snprintf(command, sizeof(command), "curl -s --max-time 20 -K '%s' -o '%s' && mv -f '%s' '%s'",
        configPath, slot->outPath, slot->outPath, slot->outPath);
    slot->pid = spawnHostShell(command);
    if (slot->pid == 0) {
        discardHttpSlot(L, slot);
        return luaL_error(L, "http.request: failed to spawn curl");
    }
    return 0;
}



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
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.boolValue = defaultValue;
    if (const auto* saved = findPendingGuiDefault(script.name, label, 'c'))
        item.boolValue = saved->boolValue; 
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
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.minValue = min;
    item.maxValue = max;
    item.intValue = defaultValue < min ? min : (defaultValue > max ? max : defaultValue);
    if (const auto* saved = findPendingGuiDefault(script.name, label, 's')) {
        const int value = saved->intValue;
        item.intValue = value < min ? min : (value > max ? max : value);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}





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
    item.page = pendingItemPage;
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
    if (const auto* saved = findPendingGuiDefault(script.name, label, 'd')) {
        const int value = saved->intValue;
        item.intValue = value < 0 ? 0 : (value >= optionCount ? optionCount - 1 : value);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}

inline int l_guiGet(lua_State* L)
{
    GuiItem& item = guiItemAt(L, static_cast<int>(luaL_checkinteger(L, 1)));
    if (item.type == GuiItem::Type::Checkbox) {
        lua_pushboolean(L, item.boolValue ? 1 : 0);
        return 1;
    }
    if (item.type == GuiItem::Type::FloatSlider) {
        lua_pushnumber(L, item.floatValue);
        return 1;
    }
    if (item.type == GuiItem::Type::Color) {
        lua_pushinteger(L, (item.colorValue >> 24) & 255);
        lua_pushinteger(L, (item.colorValue >> 16) & 255);
        lua_pushinteger(L, (item.colorValue >> 8) & 255);
        lua_pushinteger(L, item.colorValue & 255);
        return 4;
    }
    if (item.type == GuiItem::Type::Text) {
        lua_pushstring(L, item.textValue);
        return 1;
    }
    lua_pushinteger(L, item.intValue); 
    return 1;
}

inline int l_guiSet(lua_State* L)
{
    GuiItem& item = guiItemAt(L, static_cast<int>(luaL_checkinteger(L, 1)));
    if (item.type == GuiItem::Type::Checkbox) {
        item.boolValue = lua_toboolean(L, 2) != 0;
    } else if (item.type == GuiItem::Type::Dropdown) {
        
        const int value = static_cast<int>(luaL_checkinteger(L, 2));
        item.intValue = value < 0 ? 0 : (value >= item.optionCount ? item.optionCount - 1 : value);
    } else if (item.type == GuiItem::Type::Keybind) {
        const int value = static_cast<int>(luaL_checkinteger(L, 2));
        if (value < kBindOff || value > kBindLast)
            return luaL_error(L, "gui.set: keybind value must be %d-%d (0 = off)", kBindOff, kBindLast);
        item.intValue = value;
    } else if (item.type == GuiItem::Type::FloatSlider) {
        float value = static_cast<float>(luaL_checknumber(L, 2));
        if (value < item.floatMin)
            value = item.floatMin;
        if (value > item.floatMax)
            value = item.floatMax;
        item.floatValue = value;
    } else if (item.type == GuiItem::Type::Color) {
        unsigned channels[4] = {};
        for (int i = 0; i < 4; ++i) {
            const lua_Number value = luaL_checknumber(L, 2 + i);
            if (value < 0.0 || value > 255.0)
                return luaL_error(L, "gui.set: color components must be in range 0-255");
            channels[i] = static_cast<unsigned>(value);
        }
        item.colorValue = (channels[0] << 24) | (channels[1] << 16) | (channels[2] << 8) | channels[3];
    } else if (item.type == GuiItem::Type::Text) {
        std::size_t length = 0;
        const char* value = luaL_checklstring(L, 2, &length);
        if (length >= kMaxGuiText)
            return luaL_error(L, "gui.set: text must be under %d characters", static_cast<int>(kMaxGuiText) - 1);
        std::memcpy(item.textValue, value, length);
        item.textValue[length] = '\0';
    } else {
        const int value = static_cast<int>(luaL_checkinteger(L, 2));
        item.intValue = value < item.minValue ? item.minValue : (value > item.maxValue ? item.maxValue : value);
    }
    return 0;
}

inline int checkBindValue(lua_State* L, int index)
{
    const int value = static_cast<int>(luaL_checkinteger(L, index));
    if (value < kBindOff || value > kBindLast)
        luaL_error(L, "keybind value must be %d-%d (0 = off)", kBindOff, kBindLast);
    return value;
}



inline int l_guiColor(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    unsigned channels[4] = {255, 255, 255, 255};
    for (int i = 0; i < 4 && !lua_isnoneornil(L, 2 + i); ++i) {
        const lua_Number value = luaL_checknumber(L, 2 + i);
        if (value < 0.0 || value > 255.0)
            return luaL_error(L, "gui.color: components must be in range 0-255");
        channels[i] = static_cast<unsigned>(value);
    }
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Color;
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.colorValue = (channels[0] << 24) | (channels[1] << 16) | (channels[2] << 8) | channels[3];
    if (const auto* saved = findPendingGuiDefault(script.name, label, 'k'))
        item.colorValue = saved->colorValue;
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}



inline int l_guiKeybind(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    const int defaultValue = lua_isnoneornil(L, 2) ? kBindOff : checkBindValue(L, 2);
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Keybind;
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.intValue = defaultValue;
    if (const auto* saved = findPendingGuiDefault(script.name, label, 'b')) {
        const int value = saved->intValue;
        item.intValue = value < kBindOff ? kBindOff : (value > kBindLast ? kBindLast : value);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}



inline int l_guiFloatSlider(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    const float min = static_cast<float>(luaL_checknumber(L, 2));
    const float max = static_cast<float>(luaL_checknumber(L, 3));
    float defaultValue = lua_isnoneornil(L, 4) ? min : static_cast<float>(luaL_checknumber(L, 4));
    auto sane = [](float v) { return v == v && v > -1e9f && v < 1e9f; };
    if (!sane(min) || !sane(max))
        return luaL_error(L, "gui.float_slider: min/max must be finite numbers within +-1e9");
    if (min > max)
        return luaL_error(L, "gui.float_slider: min must not be greater than max");
    if (!sane(defaultValue))
        return luaL_error(L, "gui.float_slider: default must be finite");
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::FloatSlider;
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.floatMin = min;
    item.floatMax = max;
    item.floatValue = defaultValue < min ? min : (defaultValue > max ? max : defaultValue);
    if (const auto* saved = findPendingGuiDefault(script.name, label, 'f')) {
        const float value = saved->floatValue;
        item.floatValue = value < min ? min : (value > max ? max : value);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}


inline int l_guiTextInput(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    std::size_t defaultLength = 0;
    const char* defaultValue = lua_isnoneornil(L, 2) ? "" : luaL_checklstring(L, 2, &defaultLength);
    if (defaultLength >= kMaxGuiText)
        return luaL_error(L, "gui.text_input: default must be under %d characters", static_cast<int>(kMaxGuiText) - 1);
    for (std::size_t i = 0; i < defaultLength; ++i) {
        if (static_cast<unsigned char>(defaultValue[i]) < 0x20)
            return luaL_error(L, "gui.text_input: default must not contain control characters");
    }
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Text;
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    std::memcpy(item.textValue, defaultValue, defaultLength);
    item.textValue[defaultLength] = '\0';
    if (const auto* saved = findPendingGuiDefault(script.name, label, 't')) {
        std::snprintf(item.textValue, sizeof(item.textValue), "%s", saved->text);
    }
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}



inline constexpr int kMaxEntityIndex = 0x7FFE;    
inline constexpr int kMaxSchemaFieldOffset = 0x100000; 
inline constexpr int kMaxEntityStringBytes = 128; 

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




inline bool resolveEntityPropIn(int entityIndex, const char* className, const char* fieldName,
    const std::byte** outEntity, int* outOffset, lua_State* L)
{
    if (!entityBridgesAvailable()) {
        if (L)
            lua_pushnil(L);
        return false;
    }
    if (entityIndex < 0 || entityIndex > kMaxEntityIndex)
        luaL_error(L, "entity index out of range");
    if (!className || !fieldName || className[0] == '\0' || fieldName[0] == '\0'
        || std::strlen(className) > 96 || std::strlen(fieldName) > 96)
        luaL_error(L, "invalid class or field name");

    const int offset = schemaFieldOffsetQuery(className, fieldName);
    const auto* entity = static_cast<const std::byte*>(entityFromIndexQuery(entityIndex));
    if (offset <= 0 || offset > kMaxSchemaFieldOffset || !entity) {
        if (L)
            lua_pushnil(L);
        return false;
    }
    *outEntity = entity;
    *outOffset = offset;
    return true;
}




inline bool resolveEntityProp(lua_State* L, const std::byte** outEntity, int* outOffset)
{
    const int entityIndex = static_cast<int>(luaL_checkinteger(L, 1));
    const char* className = luaL_checkstring(L, 2);
    const char* fieldName = luaL_checkstring(L, 3);
    return resolveEntityPropIn(entityIndex, className, fieldName, outEntity, outOffset, L);
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
    std::memcpy(buffer, entity + offset, kMaxEntityStringBytes); 
    std::size_t length = 0;
    while (length < kMaxEntityStringBytes && buffer[length] != '\0') {
        if (static_cast<unsigned char>(buffer[length]) < 0x20) {
            length = 0; 
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



inline int l_getPropVector(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    float values[3] = {};
    std::memcpy(values, entity + offset, sizeof(values));
    lua_pushnumber(L, values[0]);
    lua_pushnumber(L, values[1]);
    lua_pushnumber(L, values[2]);
    return 3;
}




inline int l_getEntityClass(lua_State* L)
{
    const int entityIndex = static_cast<int>(luaL_checkinteger(L, 1));
    char name[96] = {};
    if (!entityClassNameQuery || entityIndex <= 0 || entityIndex > kMaxEntityIndex
        || !entityClassNameQuery(entityIndex, name, sizeof(name)) || name[0] == '\0')
        return 0;
    lua_pushstring(L, name);
    return 1;
}





static char findPropWinnerClass[96] = {};

inline bool resolveEntityPropAny(lua_State* L, const std::byte** outEntity, int* outOffset)
{
    const int entityIndex = static_cast<int>(luaL_checkinteger(L, 1));
    const char* fieldName = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    const int candidateCount = static_cast<int>(lua_objlen(L, 3));
    if (candidateCount < 1 || candidateCount > 16)
        luaL_error(L, "entity.find_prop: candidate class list must hold 1-16 names");
    if (fieldName[0] == '\0' || std::strlen(fieldName) > 96)
        luaL_error(L, "invalid field name");
    if (!entityBridgesAvailable()) {
        lua_pushnil(L);
        return false;
    }
    for (int i = 1; i <= candidateCount; ++i) {
        lua_rawgeti(L, 3, i);
        const char* className = lua_tostring(L, -1);
        char classCopy[96] = {};
        if (className)
            std::strncpy(classCopy, className, sizeof(classCopy) - 1);
        lua_pop(L, 1);
        if (classCopy[0] == '\0')
            luaL_error(L, "entity.find_prop: candidate #%d is not a string", i);
        if (resolveEntityPropIn(entityIndex, classCopy, fieldName, outEntity, outOffset, nullptr)) {
            std::strncpy(findPropWinnerClass, classCopy, sizeof(findPropWinnerClass) - 1);
            findPropWinnerClass[sizeof(findPropWinnerClass) - 1] = '\0';
            return true;
        }
    }
    lua_pushnil(L);
    return false;
}

inline int l_findProp(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityPropAny(L, &entity, &offset))
        return 1;
    std::int32_t value = 0;
    std::memcpy(&value, entity + offset, sizeof(value));
    lua_pushinteger(L, value);
    lua_pushstring(L, findPropWinnerClass);
    return 2;
}

inline int l_findPropFloat(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityPropAny(L, &entity, &offset))
        return 1;
    float value = 0.0f;
    std::memcpy(&value, entity + offset, sizeof(value));
    lua_pushnumber(L, value);
    lua_pushstring(L, findPropWinnerClass);
    return 2;
}

inline int l_findPropString(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityPropAny(L, &entity, &offset))
        return 1;
    char buffer[kMaxEntityStringBytes + 1] = {};
    std::memcpy(buffer, entity + offset, kMaxEntityStringBytes);
    std::size_t length = 0;
    while (length < kMaxEntityStringBytes && buffer[length] != '\0') {
        if (static_cast<unsigned char>(buffer[length]) < 0x20) {
            length = 0;
            break;
        }
        ++length;
    }
    if (length == 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, buffer);
    lua_pushstring(L, findPropWinnerClass);
    return 2;
}

inline int l_findPropVector(lua_State* L)
{
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityPropAny(L, &entity, &offset))
        return 1;
    float values[3] = {};
    std::memcpy(values, entity + offset, sizeof(values));
    lua_pushnumber(L, values[0]);
    lua_pushnumber(L, values[1]);
    lua_pushnumber(L, values[2]);
    lua_pushstring(L, findPropWinnerClass);
    return 3;
}



inline int l_isKeyDown(lua_State* L)
{
    const int scancode = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, KeyboardState::isKeyDown(scancode) ? 1 : 0);
    return 1;
}




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




inline int l_setPropString(lua_State* L)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        return luaL_error(L, "entity.set_prop_string is only available inside createmove / game event callbacks");
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    std::size_t length = 0;
    const char* value = luaL_checklstring(L, 4, &length);
    if (length >= kMaxEntityStringBytes)
        return luaL_error(L, "entity.set_prop_string: value must be under %d characters", kMaxEntityStringBytes);
    auto* field = const_cast<std::byte*>(entity) + offset;
    std::memset(field, 0, kMaxEntityStringBytes);
    std::memcpy(field, value, length);
    return 0;
}



inline int l_setPropVector(lua_State* L)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        return luaL_error(L, "entity.set_prop_vector is only available inside createmove / game event callbacks");
    const std::byte* entity = nullptr;
    int offset = 0;
    if (!resolveEntityProp(L, &entity, &offset))
        return 1;
    const float values[3] = {
        static_cast<float>(luaL_checknumber(L, 4)),
        static_cast<float>(luaL_checknumber(L, 5)),
        static_cast<float>(luaL_checknumber(L, 6)),
    };
    std::memcpy(const_cast<std::byte*>(entity) + offset, values, sizeof(values));
    return 0;
}



inline int l_netServer(lua_State* L)
{
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    if (!netlag_hook::getServerEndpoint(&fd, &addr, &addrLen))
        return 0; 

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
        return 0; 
    lua_pushinteger(L, netlag_hook::sendRawToServer(reinterpret_cast<const unsigned char*>(data), length, static_cast<std::uint32_t>(count)));
    return 1;
}



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





inline int l_netSetDelay(lua_State* L)
{
    const int ms = static_cast<int>(luaL_checkinteger(L, 1));
    if (ms < 0 || ms > static_cast<int>(net_lag::kMaxLuaDelayMs))
        return luaL_error(L, "net.set_delay: ms must be 0-%d", static_cast<int>(net_lag::kMaxLuaDelayMs));
    net_lag::luaDelayMs.store(static_cast<std::uint32_t>(ms), std::memory_order_relaxed);
    return 0;
}

inline int l_netGetDelay(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(net_lag::luaDelayMs.load(std::memory_order_relaxed)));
    return 1;
}


















inline constexpr unsigned long long kSteamFriendFlagImmediate = 4; 
inline constexpr unsigned long long kSteamFriendFlagAll = 0xFFFF;  


struct FriendGameInfo {
    std::uint64_t gameId = 0;
    std::uint32_t ip = 0;
    std::uint16_t gamePort = 0;
    std::uint16_t queryPort = 0;
    std::uint64_t lobby = 0;
};

struct SteamApi {
    bool resolved = false;
    bool ok = false;
    double lastAttempt = 0.0; 
    char lastError[160] = {}; 

    void* user = nullptr;         
    void* friends = nullptr;      
    void* matchmaking = nullptr;  

    std::uint64_t (*getSteamId)(void*) = nullptr;                          
    const char* (*getPersonaName)(void*) = nullptr;                        
    int (*getFriendCount)(void*, int) = nullptr;                           
    std::uint64_t (*getFriendByIndex)(void*, int, int) = nullptr;          
    const char* (*getFriendName)(void*, std::uint64_t) = nullptr;          
    int (*getFriendState)(void*, std::uint64_t) = nullptr;                 
    bool (*getFriendGame)(void*, std::uint64_t, FriendGameInfo*) = nullptr; 
    const char* (*getRichPresence)(void*, std::uint64_t, const char*) = nullptr; 
    int (*getRichPresenceKeyCount)(void*) = nullptr;                       
    const char* (*getRichPresenceKey)(void*, int) = nullptr;               
    void (*requestRichPresence)(void*, std::uint64_t) = nullptr;           
    bool (*inviteToLobby)(void*, std::uint64_t, std::uint64_t) = nullptr;  
    int (*getLobbyMemberCount)(void*, std::uint64_t) = nullptr;            
    std::uint64_t (*getLobbyMember)(void*, std::uint64_t, int) = nullptr;  
    std::uint64_t (*getLobbyOwner)(void*, std::uint64_t) = nullptr;        

    
    
    int (*getLargeAvatar)(void*, std::uint64_t) = nullptr;           
    int (*getMediumAvatar)(void*, std::uint64_t) = nullptr;          
    bool (*requestUserInfo)(void*, std::uint64_t, bool) = nullptr;   
    void* utils = nullptr;                                           
    bool (*getImageSize)(void*, int, unsigned int*, unsigned int*) = nullptr;  
    bool (*getImageRgba)(void*, int, unsigned char*, int) = nullptr;           
};

static SteamApi steamApi;



static void luaSteamLog(const char* fmt, ...) noexcept
{
    char steamLogPath[ns_paths::kMaxPath];
    if (!ns_paths::joinLog(steamLogPath, sizeof(steamLogPath), "gamesense_gui.log"))
        return;
    const int fd = ::open(steamLogPath, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0)
        return;
    char line[256];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (length > 0)
        ::write(fd, line, static_cast<std::size_t>(length));
    ::write(fd, "\n", 1);
    ::close(fd);
}




static bool resolveSteamApi() noexcept
{
    if (steamApi.ok)
        return true;
    
    
    if (steamApi.resolved && luaNow() - steamApi.lastAttempt < 5.0)
        return false;
    steamApi.resolved = true;
    steamApi.lastAttempt = luaNow();

    char steamApiPath[512] = "";
    {
        
        const int fd = ::open("/proc/self/maps", O_RDONLY);
        if (fd >= 0) {
            constexpr std::size_t kCarry = 1024;
            char chunk[4096];
            char carry[kCarry];
            std::size_t carryLength = 0;
            off_t position = 0;
            bool found = false;
            while (!found) {
                const auto got = ::pread(fd, chunk, sizeof(chunk), position);
                if (got <= 0)
                    break;
                position += static_cast<off_t>(got);

                std::size_t lineBegin = 0;
                for (std::size_t i = 0; i < static_cast<std::size_t>(got) && !found; ++i) {
                    if (chunk[i] != '\n')
                        continue;
                    char line[kCarry + 4096];
                    std::size_t lineLength = 0;
                    if (carryLength > 0) {
                        std::memcpy(line, carry, carryLength);
                        lineLength = carryLength;
                    }
                    const auto part = (i - lineBegin) < (sizeof(line) - lineLength - 1) ? (i - lineBegin) : (sizeof(line) - lineLength - 1);
                    std::memcpy(line + lineLength, chunk + lineBegin, part);
                    lineLength += part;
                    line[lineLength] = '\0';
                    carryLength = 0;

                    
                    
                    const char* path = line;
                    for (std::size_t j = 0; j < lineLength; ++j) {
                        if (line[j] == '/') {
                            path = line + j;
                            break;
                        }
                    }
                    if (std::strstr(path, "libsteam_api.so")) {
                        const auto rest = std::strlen(path);
                        std::memcpy(steamApiPath, path, rest < sizeof(steamApiPath) - 1 ? rest : sizeof(steamApiPath) - 1);
                        steamApiPath[rest < sizeof(steamApiPath) - 1 ? rest : sizeof(steamApiPath) - 1] = '\0';
                        found = true;
                    }
                    lineBegin = i + 1;
                }

                if (found)
                    break;
                
                carryLength = 0;
                const std::size_t remaining = static_cast<std::size_t>(got) - lineBegin;
                if (remaining > 0 && remaining < kCarry) {
                    std::memcpy(carry, chunk + lineBegin, remaining);
                    carryLength = remaining;
                } else if (remaining >= kCarry) {
                    carryLength = 0; 
                }
            }
            ::close(fd);
        }
    }
    if (steamApiPath[0] == '\0') {
        std::snprintf(steamApi.lastError, sizeof(steamApi.lastError), "libsteam_api.so not in our maps");
        VerifyConsole::write(1.0f, "lua", "steam: %s", steamApi.lastError);
        luaSteamLog("lua: steam resolve failed - %s", steamApi.lastError);
        return false;
    }

    
    
    
    const LinuxDynamicLibrary steamLib{steamApiPath};
    if (!steamLib) {
        std::snprintf(steamApi.lastError, sizeof(steamApi.lastError), "could not dlopen %s (NOLOAD miss)", steamApiPath);
        VerifyConsole::write(1.0f, "lua", "steam: %s", steamApi.lastError);
        luaSteamLog("lua: steam resolve: %s", steamApi.lastError);
        return false;
    }
    const auto interfaceFn = [steamLib](const char* name) {
        return steamLib.getFunctionAddress(name).as<void* (*)()>();
    };
    const auto method = [steamLib](const char* name) {
        return steamLib.getFunctionAddress(name);
    };

    const auto userApi = interfaceFn("SteamAPI_SteamUser_v023");
    const auto friendsApi = interfaceFn("SteamAPI_SteamFriends_v018");
    const auto matchmakingApi = interfaceFn("SteamAPI_SteamMatchmaking_v009");
    steamApi.getSteamId = method("SteamAPI_ISteamUser_GetSteamID").as<std::uint64_t (*)(void*)>();
    steamApi.getPersonaName = method("SteamAPI_ISteamFriends_GetPersonaName").as<const char* (*)(void*)>();
    steamApi.getFriendCount = method("SteamAPI_ISteamFriends_GetFriendCount").as<int (*)(void*, int)>();
    steamApi.getFriendByIndex = method("SteamAPI_ISteamFriends_GetFriendByIndex").as<std::uint64_t (*)(void*, int, int)>();
    steamApi.getFriendName = method("SteamAPI_ISteamFriends_GetFriendPersonaName").as<const char* (*)(void*, std::uint64_t)>();
    steamApi.getFriendState = method("SteamAPI_ISteamFriends_GetFriendPersonaState").as<int (*)(void*, std::uint64_t)>();
    steamApi.getFriendGame = method("SteamAPI_ISteamFriends_GetFriendGamePlayed").as<bool (*)(void*, std::uint64_t, FriendGameInfo*)>();
    steamApi.getRichPresence = method("SteamAPI_ISteamFriends_GetFriendRichPresence").as<const char* (*)(void*, std::uint64_t, const char*)>();
    steamApi.getRichPresenceKeyCount = method("SteamAPI_ISteamFriends_GetFriendRichPresenceKeyCount").as<int (*)(void*)>();
    steamApi.getRichPresenceKey = method("SteamAPI_ISteamFriends_GetFriendRichPresenceKeyByIndex").as<const char* (*)(void*, int)>();
    steamApi.requestRichPresence = method("SteamAPI_ISteamFriends_RequestFriendRichPresence").as<void (*)(void*, std::uint64_t)>();
    steamApi.inviteToLobby = method("SteamAPI_ISteamMatchmaking_InviteUserToLobby").as<bool (*)(void*, std::uint64_t, std::uint64_t)>();
    steamApi.getLobbyMemberCount = method("SteamAPI_ISteamMatchmaking_GetNumLobbyMembers").as<int (*)(void*, std::uint64_t)>();
    steamApi.getLobbyMember = method("SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex").as<std::uint64_t (*)(void*, std::uint64_t, int)>();
    steamApi.getLobbyOwner = method("SteamAPI_ISteamMatchmaking_GetLobbyOwner").as<std::uint64_t (*)(void*, std::uint64_t)>();

    
    
    steamApi.getLargeAvatar = method("SteamAPI_ISteamFriends_GetLargeFriendAvatar").as<int (*)(void*, std::uint64_t)>();
    steamApi.getMediumAvatar = method("SteamAPI_ISteamFriends_GetMediumFriendAvatar").as<int (*)(void*, std::uint64_t)>();
    steamApi.requestUserInfo = method("SteamAPI_ISteamFriends_RequestUserInformation").as<bool (*)(void*, std::uint64_t, bool)>();
    steamApi.getImageSize = method("SteamAPI_ISteamUtils_GetImageSize").as<bool (*)(void*, int, unsigned int*, unsigned int*)>();
    steamApi.getImageRgba = method("SteamAPI_ISteamUtils_GetImageRGBA").as<bool (*)(void*, int, unsigned char*, int)>();
    if (const auto utilsApi = interfaceFn("SteamAPI_SteamUtils_v010"))
        steamApi.utils = utilsApi();
    else if (const auto utilsApiNewer = interfaceFn("SteamAPI_SteamUtils_v011"))
        steamApi.utils = utilsApiNewer();
    if (!userApi || !friendsApi || !matchmakingApi || !steamApi.getSteamId || !steamApi.getPersonaName
        || !steamApi.getFriendCount || !steamApi.getFriendByIndex || !steamApi.getFriendName
        || !steamApi.getFriendState || !steamApi.getFriendGame || !steamApi.getRichPresence
        || !steamApi.getRichPresenceKeyCount || !steamApi.getRichPresenceKey || !steamApi.requestRichPresence
        || !steamApi.inviteToLobby
        || !steamApi.getLobbyMemberCount || !steamApi.getLobbyMember || !steamApi.getLobbyOwner) {
        std::snprintf(steamApi.lastError, sizeof(steamApi.lastError), "libsteam_api exports missing");
        VerifyConsole::write(1.0f, "lua", "steam: %s", steamApi.lastError);
        luaSteamLog("lua: steam resolve: %s", steamApi.lastError);
        return false;
    }

    steamApi.user = userApi();
    steamApi.friends = friendsApi();
    steamApi.matchmaking = matchmakingApi();
    if (!steamApi.user || !steamApi.friends || !steamApi.matchmaking) {
        std::snprintf(steamApi.lastError, sizeof(steamApi.lastError), "interfaces unavailable (SteamAPI_Init failed?)");
        VerifyConsole::write(1.0f, "lua", "steam: %s", steamApi.lastError);
        luaSteamLog("lua: steam resolve: %s", steamApi.lastError);
        return false;
    }

    
    
    
    
    const std::uint64_t ownId = steamApi.getSteamId(steamApi.user);
    const char* persona = steamApi.getPersonaName(steamApi.friends);
    constexpr std::uint64_t kSteamId64HighBits = 0x0110000100000000ULL >> 32; 
    if (ownId == 0 || (ownId >> 32) != kSteamId64HighBits || !persona || persona[0] == '\0') {
        std::snprintf(steamApi.lastError, sizeof(steamApi.lastError), "interface sanity check failed (id %llu)", static_cast<unsigned long long>(ownId));
        VerifyConsole::write(1.0f, "lua", "steam: %s", steamApi.lastError);
        luaSteamLog("lua: steam resolve: %s", steamApi.lastError);
        return false;
    }
    steamApi.ok = true;
    steamApi.lastError[0] = '\0';
    VerifyConsole::write(1.0f, "lua", "steam: interfaces ok (SteamUser023/Friends018/Matchmaking009)");
    luaSteamLog("lua: steam interfaces ok");
    return true;
}


inline int l_steamStatus(lua_State* L)
{
    if (resolveSteamApi()) {
        lua_pushstring(L, "ok");
    } else {
        lua_pushstring(L, steamApi.lastError[0] != '\0' ? steamApi.lastError : "unknown failure");
    }
    return 1;
}





static int steamFriendCount(int* activeFlag) noexcept
{
    int count = steamApi.getFriendCount(steamApi.friends, static_cast<int>(kSteamFriendFlagImmediate));
    if (count > 0) {
        *activeFlag = static_cast<int>(kSteamFriendFlagImmediate);
        return count;
    }
    count = steamApi.getFriendCount(steamApi.friends, static_cast<int>(kSteamFriendFlagAll));
    *activeFlag = static_cast<int>(kSteamFriendFlagAll);
    if (count > 0) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            VerifyConsole::write(0.0f, "lua", "steam: friend list needs the All flag (immediate returned 0, all: %d)", count);
            luaSteamLog("lua: steam friends need All flag (immediate 0, all %d)", count);
        }
    }
    return count;
}



static std::uint64_t lobbyIfMember(std::uint64_t lobby, std::uint64_t ownId) noexcept
{
    if (lobby == 0)
        return 0;
    const int count = steamApi.getLobbyMemberCount(steamApi.matchmaking, lobby);
    for (int i = 0; i < count; ++i) {
        if (steamApi.getLobbyMember(steamApi.matchmaking, lobby, i) == ownId)
            return lobby;
    }
    return 0;
}


static std::uint64_t parseLobbyCandidate(const char* text) noexcept
{
    if (!text)
        return 0;
    std::uint64_t best = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            continue;
        int digits = 0;
        std::uint64_t value = 0;
        for (const char* q = p; *q >= '0' && *q <= '9'; ++q) {
            if (digits < 20)
                value = value * 10 + static_cast<std::uint64_t>(*q - '0');
            ++digits;
        }
        if (digits >= 15 && digits <= 20 && value > 0) {
            best = value;
            break;
        }
        
        while (*p >= '0' && *p <= '9')
            ++p;
        --p; 
    }
    return best;
}









static std::uint64_t currentLobbyId() noexcept
{
    if (!steamApi.ok || !steamApi.user || !steamApi.friends)
        return 0;
    const std::uint64_t ownId = steamApi.getSteamId(steamApi.user);

    FriendGameInfo info{};
    if (steamApi.getFriendGame(steamApi.friends, ownId, &info) && info.lobby != 0)
        return info.lobby;

    
    static const char* const kKeys[] = { "connect", "status", "game", "steam_player_group" };
    for (const char* key : kKeys) {
        const char* value = steamApi.getRichPresence(steamApi.friends, ownId, key);
        if (value && value[0] != '\0') {
            if (lobbyIfMember(parseLobbyCandidate(value), ownId) != 0) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    VerifyConsole::write(0.0f, "lua", "steam: lobby found via rich presence key '%s'", key);
                }
                return lobbyIfMember(parseLobbyCandidate(value), ownId);
            }
        }
    }

    
    int scanFlag = 0;
    const int count = steamFriendCount(&scanFlag);
    for (int i = 0; i < count; ++i) {
        const std::uint64_t friendId = steamApi.getFriendByIndex(steamApi.friends, i, scanFlag);
        FriendGameInfo friendInfo{};
        if (steamApi.getFriendGame(steamApi.friends, friendId, &friendInfo) && friendInfo.lobby != 0) {
            if (lobbyIfMember(friendInfo.lobby, ownId) != 0) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    VerifyConsole::write(0.0f, "lua", "steam: lobby found via a party member's game info");
                }
                return friendInfo.lobby;
            }
        }
    }
    return 0;
}

inline void pushSteamId(lua_State* L, std::uint64_t sid)
{
    char buffer[24];
    formatSteamId64(sid, buffer, sizeof(buffer));
    lua_pushstring(L, buffer);
}



inline bool checkSteamIdArg(lua_State* L, int index, std::uint64_t* out)
{
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER)
        *out = static_cast<std::uint64_t>(lua_tointeger(L, index));
    else if (type == LUA_TSTRING) {
        if (!parseSteamId64(lua_tostring(L, index), out))
            luaL_error(L, "steam: '%s' is not a valid SteamID64", lua_tostring(L, index));
    } else
        luaL_error(L, "steam: expected a SteamID64 string");
    return *out != 0;
}

inline int l_steamGetSteamId(lua_State* L)
{
    if (!resolveSteamApi())
        return 0;
    pushSteamId(L, steamApi.getSteamId(steamApi.user));
    return 1;
}

inline int l_steamGetLobby(lua_State* L)
{
    if (!resolveSteamApi())
        return 0;
    const std::uint64_t lobby = currentLobbyId();
    if (lobby == 0)
        return 0;
    pushSteamId(L, lobby);
    return 1;
}

inline int l_steamGetFriends(lua_State* L)
{
    lua_newtable(L);
    if (!resolveSteamApi())
        return 1;
    int flag = 0;
    const int count = steamFriendCount(&flag);
    for (int i = 0; i < count; ++i)
        pushSteamId(L, steamApi.getFriendByIndex(steamApi.friends, i, flag)), lua_rawseti(L, -2, i + 1);
    return 1;
}

inline int l_steamGetFriendName(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    const char* name = steamApi.getFriendName(steamApi.friends, sid);
    if (!name || name[0] == '\0')
        return 0;
    lua_pushstring(L, name);
    return 1;
}

inline int l_steamGetFriendState(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    lua_pushinteger(L, steamApi.getFriendState(steamApi.friends, sid));
    return 1;
}

inline int l_steamGetFriendGame(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    FriendGameInfo info{};
    if (!steamApi.getFriendGame(steamApi.friends, sid, &info))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(info.gameId & 0xFFFFFFFFULL));
    lua_setfield(L, -2, "appid");
    if (info.lobby != 0)
        pushSteamId(L, info.lobby), lua_setfield(L, -2, "lobby");
    return 1;
}




inline int l_steamGetFriendPresence(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    lua_newtable(L);
    const int keyCount = steamApi.getRichPresenceKeyCount(steamApi.friends);
    for (int i = 0; i < keyCount; ++i) {
        const char* key = steamApi.getRichPresenceKey(steamApi.friends, i);
        if (!key || key[0] == '\0')
            continue;
        const char* value = steamApi.getRichPresence(steamApi.friends, sid, key);
        lua_pushstring(L, key);
        lua_pushstring(L, value ? value : "");
        lua_rawset(L, -3);
    }
    return 1;
}



inline int l_steamRequestFriendPresence(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    steamApi.requestRichPresence(steamApi.friends, sid);
    return 0;
}



inline int l_steamInvite(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    const std::uint64_t lobby = currentLobbyId();
    if (lobby == 0)
        return 0; 
    lua_pushboolean(L, steamApi.inviteToLobby(steamApi.matchmaking, lobby, sid) ? 1 : 0);
    return 1;
}

inline int l_steamLobbyMembers(lua_State* L)
{
    lua_newtable(L);
    if (!resolveSteamApi())
        return 1;
    const std::uint64_t lobby = currentLobbyId();
    if (lobby == 0)
        return 1;
    const int count = steamApi.getLobbyMemberCount(steamApi.matchmaking, lobby);
    for (int i = 0; i < count; ++i)
        pushSteamId(L, steamApi.getLobbyMember(steamApi.matchmaking, lobby, i)), lua_rawseti(L, -2, i + 1);
    return 1;
}

inline int l_steamLobbyOwner(lua_State* L)
{
    if (!resolveSteamApi())
        return 0;
    const std::uint64_t lobby = currentLobbyId();
    if (lobby == 0)
        return 0;
    pushSteamId(L, steamApi.getLobbyOwner(steamApi.matchmaking, lobby));
    return 1;
}









inline int l_steamRequestFriendInfo(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid) || !steamApi.requestUserInfo)
        return 0;
    lua_pushboolean(L, steamApi.requestUserInfo(steamApi.friends, sid, true) ? 1 : 0);
    return 1;
}



inline int l_steamGetFriendAvatar(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid) || !steamApi.friends)
        return 0;
    int image = 0;
    if (steamApi.getLargeAvatar)
        image = steamApi.getLargeAvatar(steamApi.friends, sid);
    else if (steamApi.getMediumAvatar)
        image = steamApi.getMediumAvatar(steamApi.friends, sid);
    else
        return 0;
    if (image <= 0)
        return 0;
    lua_pushinteger(L, image);
    return 1;
}


inline int l_steamAvatarSize(lua_State* L)
{
    const int handle = static_cast<int>(luaL_checkinteger(L, 1));
    if (!resolveSteamApi() || !steamApi.getImageSize || !steamApi.utils || handle <= 0)
        return 0;
    unsigned int width = 0;
    unsigned int height = 0;
    if (!steamApi.getImageSize(steamApi.utils, handle, &width, &height) || width == 0 || height == 0)
        return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(width));
    lua_pushinteger(L, static_cast<lua_Integer>(height));
    return 2;
}


inline int l_steamAvatarRgba(lua_State* L)
{
    const int handle = static_cast<int>(luaL_checkinteger(L, 1));
    if (!resolveSteamApi() || !steamApi.getImageSize || !steamApi.getImageRgba || !steamApi.utils || handle <= 0)
        return 0;
    unsigned int width = 0;
    unsigned int height = 0;
    if (!steamApi.getImageSize(steamApi.utils, handle, &width, &height)
        || width == 0 || height == 0 || width > 512 || height > 512)
        return 0;
    const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4;
    unsigned char* pixels = static_cast<unsigned char*>(std::malloc(byteCount));
    if (!pixels)
        return 0;
    if (!steamApi.getImageRgba(steamApi.utils, handle, pixels, static_cast<int>(byteCount))) {
        std::free(pixels);
        return 0;
    }
    lua_pushlstring(L, reinterpret_cast<const char*>(pixels), byteCount);
    std::free(pixels);
    return 1;
}



inline constexpr int kMaxLuaTextures = 8; 

struct LuaTexture {
    int scriptIndex = -1; 
    int width = 0;
    int height = 0;
};
static LuaTexture luaTextures[kMaxLuaTextures];


inline void releaseScriptTextures(int scriptIndex) noexcept
{
    for (int i = 0; i < kMaxLuaTextures; ++i) {
        if (luaTextures[i].scriptIndex == scriptIndex) {
            if (luaTextureRelease)
                luaTextureRelease(i);
            luaTextures[i] = LuaTexture{};
        }
    }
    for (int i = 0; i < kMaxScriptTextures; ++i)
        scripts[scriptIndex].textureSlots[i] = 0;
}




inline int stageLuaTexture(Script& script, int scriptIndex, unsigned char* pixels, int width, int height)
{
    int scriptSlot = -1;
    for (int i = 0; i < kMaxScriptTextures; ++i) {
        if (script.textureSlots[i] == 0) {
            scriptSlot = i;
            break;
        }
    }
    if (scriptSlot < 0)
        return -1;

    int poolIndex = -1;
    for (int i = 0; i < kMaxLuaTextures; ++i) {
        if (luaTextures[i].scriptIndex < 0) {
            poolIndex = i;
            break;
        }
    }
    if (poolIndex < 0)
        return -2;

    luaTextureRequest(poolIndex, pixels, width, height);
    luaTextures[poolIndex] = LuaTexture{scriptIndex, width, height};
    script.textureSlots[scriptSlot] = poolIndex + 1; 
    return poolIndex + 1;
}



inline int l_loadImage(lua_State* L)
{
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    if (length == 0 || length > 16 * 1024 * 1024)
        return luaL_error(L, "renderer.load_image: data must be 1-16MB");

    int width = 0;
    int height = 0;
    
    unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(data),
        static_cast<int>(length), &width, &height, nullptr, 4);
    if (!pixels || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        if (pixels)
            std::free(pixels);
        return 0; 
    }
    if (!luaTextureRequest) {
        std::free(pixels);
        return luaL_error(L, "renderer.load_image: texture bridge not installed");
    }

    const int id = stageLuaTexture(selfScript(L), static_cast<int>(lua_tointeger(L, lua_upvalueindex(1))), pixels, width, height);
    if (id == -1)
        return luaL_error(L, "renderer.load_image: too many textures (max %d)", kMaxScriptTextures);
    if (id == -2)
        return luaL_error(L, "renderer.load_image: texture pool exhausted");

    lua_pushinteger(L, id);
    return 1;
}




inline int l_loadImageRgba(lua_State* L)
{
    const int width = static_cast<int>(luaL_checkinteger(L, 1));
    const int height = static_cast<int>(luaL_checkinteger(L, 2));
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 3, &length);
    if (width < 1 || width > 8192 || height < 1 || height > 8192)
        return luaL_error(L, "renderer.load_rgba: size must be 1-8192 per axis");
    if (length != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return luaL_error(L, "renderer.load_rgba: data must be width*height*4 RGBA bytes");
    if (!luaTextureRequest)
        return luaL_error(L, "renderer.load_rgba: texture bridge not installed");

    
    
    unsigned char* pixels = static_cast<unsigned char*>(std::malloc(length));
    if (!pixels)
        return 0;
    std::memcpy(pixels, data, length);
    const int id = stageLuaTexture(selfScript(L), static_cast<int>(lua_tointeger(L, lua_upvalueindex(1))), pixels, width, height);
    if (id == -1) {
        std::free(pixels);
        return luaL_error(L, "renderer.load_rgba: too many textures (max %d)", kMaxScriptTextures);
    }
    if (id == -2) {
        std::free(pixels);
        return luaL_error(L, "renderer.load_rgba: texture pool exhausted");
    }

    lua_pushinteger(L, id);
    return 1;
}

inline LuaTexture* luaTextureAt(lua_State* L, int id)
{
    const int index = static_cast<int>(luaL_checkinteger(L, 1)) - 1;
    if (index < 0 || index >= kMaxLuaTextures || luaTextures[index].scriptIndex < 0)
        luaL_error(L, "invalid texture id");
    return &luaTextures[index];
}

inline int l_imageReady(lua_State* L)
{
    const LuaTexture* texture = luaTextureAt(L, 1);
    lua_pushboolean(L, luaTextureQuery && luaTextureQuery(static_cast<int>(texture - luaTextures)) != nullptr ? 1 : 0);
    return 1;
}

inline int l_imageSize(lua_State* L)
{
    const LuaTexture* texture = luaTextureAt(L, 1);
    lua_pushinteger(L, texture->width);
    lua_pushinteger(L, texture->height);
    return 2;
}

inline int l_renderImage(lua_State* L)
{
    ImDrawList* const drawList = requirePaintList(L);
    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    if (id < 1 || id > kMaxLuaTextures || luaTextures[id - 1].scriptIndex < 0)
        return luaL_error(L, "invalid texture id");
    const void* descriptor = luaTextureQuery ? luaTextureQuery(id - 1) : nullptr;
    if (descriptor) {
        const float x = static_cast<float>(luaL_checknumber(L, 2));
        const float y = static_cast<float>(luaL_checknumber(L, 3));
        const float w = static_cast<float>(luaL_checknumber(L, 4));
        const float h = static_cast<float>(luaL_checknumber(L, 5));
        const ImU32 color = checkColor(L, 6);
        drawList->AddImage(reinterpret_cast<ImTextureID>(const_cast<void*>(descriptor)), ImVec2{x, y}, ImVec2{x + w, y + h}, ImVec2{0, 0}, ImVec2{1, 1}, color);
    }
    return 0;
}



inline constexpr std::uint64_t kInAttack = 1ull << 0;
inline constexpr std::uint64_t kInJump = 1ull << 1;
inline constexpr std::uint64_t kInDuck = 1ull << 2;
inline constexpr std::uint64_t kInForward = 1ull << 3;
inline constexpr std::uint64_t kInBack = 1ull << 4;
inline constexpr std::uint64_t kInUse = 1ull << 5;
inline constexpr std::uint64_t kInMoveLeft = 1ull << 9;
inline constexpr std::uint64_t kInMoveRight = 1ull << 10;
inline constexpr std::uint64_t kInAttack2 = 1ull << 11;
inline constexpr std::uint64_t kInReload = 1ull << 13;
inline constexpr std::uint64_t kInSpeed = 1ull << 17;
inline constexpr std::uint64_t kInWalk = 1ull << 18;
inline constexpr std::uint64_t kInBullrush = 1ull << 22;


inline UserCmd currentCmd()
{
    return UserCmd{static_cast<cs2::CUserCmd*>(tickUserCmd)};
}

inline int l_cmdSetButtons(lua_State* L)
{
    requireGameThread(L, "cmd.set_buttons");
    const std::uint64_t mask = static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    currentCmd().setButtonState(mask, lua_toboolean(L, 2) != 0);
    return 0;
}

inline int l_cmdPressButtons(lua_State* L)
{
    requireGameThread(L, "cmd.press_buttons");
    const std::uint64_t mask = static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    currentCmd().pressButtons(mask);
    return 0;
}

inline int l_cmdGetButtons(lua_State* L)
{
    requireGameThread(L, "cmd.get_buttons");
    lua_pushnumber(L, static_cast<lua_Number>(currentCmd().buttonState1()));
    return 1;
}

inline int l_cmdIsButtonDown(lua_State* L)
{
    requireGameThread(L, "cmd.is_button_down");
    const std::uint64_t mask = static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    lua_pushboolean(L, currentCmd().isButtonDown(mask) ? 1 : 0);
    return 1;
}

inline int l_cmdGetViewAngles(lua_State* L)
{
    requireGameThread(L, "cmd.get_view_angles");
    if (const auto pitch = currentCmd().viewPitch(); pitch.hasValue())
        lua_pushnumber(L, pitch.value());
    else
        lua_pushnil(L);
    if (const auto yaw = currentCmd().viewYaw(); yaw.hasValue())
        lua_pushnumber(L, yaw.value());
    else
        lua_pushnil(L);
    return 2;
}

inline int l_cmdSetViewAngles(lua_State* L)
{
    requireGameThread(L, "cmd.set_view_angles");
    const float pitch = static_cast<float>(luaL_checknumber(L, 1));
    const float yaw = static_cast<float>(luaL_checknumber(L, 2));
    currentCmd().setViewAngles(pitch, yaw);
    return 0;
}

inline int l_cmdGetForwardMove(lua_State* L)
{
    requireGameThread(L, "cmd.get_forward_move");
    if (const auto value = currentCmd().forwardMove(); value.hasValue())
        lua_pushnumber(L, value.value());
    else
        lua_pushnil(L);
    return 1;
}

inline int l_cmdGetLeftMove(lua_State* L)
{
    requireGameThread(L, "cmd.get_left_move");
    if (const auto value = currentCmd().leftMove(); value.hasValue())
        lua_pushnumber(L, value.value());
    else
        lua_pushnil(L);
    return 1;
}

inline int l_cmdSetForwardMove(lua_State* L)
{
    requireGameThread(L, "cmd.set_forward_move");
    float value = static_cast<float>(luaL_checknumber(L, 1));
    if (value > 1.0f)
        value = 1.0f;
    if (value < -1.0f)
        value = -1.0f;
    currentCmd().setForwardMove(value);
    return 0;
}

inline int l_cmdSetLeftMove(lua_State* L)
{
    requireGameThread(L, "cmd.set_left_move");
    float value = static_cast<float>(luaL_checknumber(L, 1));
    if (value > 1.0f)
        value = 1.0f;
    if (value < -1.0f)
        value = -1.0f;
    currentCmd().setLeftMove(value);
    return 0;
}






inline int l_cmdPressShot(lua_State* L)
{
    requireGameThread(L, "cmd.press_shot");
    const std::uint64_t mask = lua_isnoneornil(L, 1)
        ? kInAttack : static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    lua_pushboolean(L, currentCmd().pressButtonsBothBanks(mask) ? 1 : 0);
    return 1;
}



inline int l_cmdPressBank2(lua_State* L)
{
    requireGameThread(L, "cmd.press_bank2");
    const std::uint64_t mask = static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    lua_pushboolean(L, currentCmd().pressButtonsInBank2(mask) ? 1 : 0);
    return 1;
}




inline int l_cmdSuppressShot(lua_State* L)
{
    requireGameThread(L, "cmd.suppress_shot");
    const std::uint64_t mask = lua_isnoneornil(L, 1)
        ? kInAttack : static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    currentCmd().suppressAttack(mask);
    return 0;
}


inline int l_cmdGetMouseDx(lua_State* L)
{
    requireGameThread(L, "cmd.get_mouse_dx");
    if (const auto dx = currentCmd().mouseDx(); dx.hasValue())
        lua_pushinteger(L, dx.value());
    else
        lua_pushnil(L);
    return 1;
}



inline int l_cmdGetRandomSeed(lua_State* L)
{
    requireGameThread(L, "cmd.get_random_seed");
    if (const auto seed = currentCmd().randomSeed(); seed.hasValue())
        lua_pushinteger(L, seed.value());
    else
        lua_pushnil(L);
    return 1;
}

inline int l_cmdSetRandomSeed(lua_State* L)
{
    requireGameThread(L, "cmd.set_random_seed");
    currentCmd().setRandomSeed(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}






inline int l_cmdGetHistorySize(lua_State* L)
{
    requireGameThread(L, "cmd.get_history_size");
    if (const auto size = currentCmd().inputHistorySize(); size.hasValue())
        lua_pushinteger(L, size.value());
    else
        lua_pushnil(L);
    return 1;
}

inline int l_cmdSetAttackIndex(lua_State* L)
{
    requireGameThread(L, "cmd.set_attack_index");
    lua_pushboolean(L, currentCmd().setAttack1StartHistoryIndex(static_cast<int>(luaL_checkinteger(L, 1))) ? 1 : 0);
    return 1;
}

inline int l_cmdForceAttackIndex(lua_State* L)
{
    requireGameThread(L, "cmd.force_attack_index");
    lua_pushboolean(L, currentCmd().forceAttack1StartHistoryIndex(static_cast<int>(luaL_checkinteger(L, 1))) ? 1 : 0);
    return 1;
}



inline const char* configKindName(ConfigValueKind kind) noexcept
{
    switch (kind) {
    case ConfigValueKind::Bool: return "bool";
    case ConfigValueKind::Uint: return "uint";
    case ConfigValueKind::Float: return "float";
    case ConfigValueKind::Color: return "color";
    }
    return "unknown";
}




inline int l_configGet(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    ConfigValue value{};
    if (!configGetQuery || !configGetQuery(path, &value))
        return 0;
    switch (value.kind) {
    case ConfigValueKind::Bool:
        lua_pushboolean(L, value.boolValue ? 1 : 0);
        return 1;
    case ConfigValueKind::Uint:
        lua_pushinteger(L, static_cast<lua_Integer>(value.uintValue));
        return 1;
    case ConfigValueKind::Float:
        lua_pushnumber(L, value.floatValue);
        return 1;
    case ConfigValueKind::Color:
        lua_pushinteger(L, value.color[0]);
        lua_pushinteger(L, value.color[1]);
        lua_pushinteger(L, value.color[2]);
        lua_pushinteger(L, value.color[3]);
        return 4;
    }
    return 0;
}





inline int l_configSet(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    if (!configSetQuery || !configGetQuery) {
        lua_pushboolean(L, 0);
        return 1;
    }
    ConfigValue current{};
    if (!configGetQuery(path, &current))
        return luaL_error(L, "config.set: unknown path '%s' (see config.list())", path);
    ConfigValue next = current;
    switch (current.kind) {
    case ConfigValueKind::Bool:
        if (lua_isboolean(L, 2))
            next.boolValue = lua_toboolean(L, 2) != 0;
        else if (lua_isnumber(L, 2))
            next.boolValue = lua_tonumber(L, 2) != 0.0;
        else
            return luaL_error(L, "config.set: bool '%s' needs a boolean or number", path);
        break;
    case ConfigValueKind::Uint: {
        if (!lua_isnumber(L, 2))
            return luaL_error(L, "config.set: uint '%s' needs a number", path);
        const lua_Number value = lua_tonumber(L, 2);
        if (value < 0.0 || value > 18446744073709551615.0)
            return luaL_error(L, "config.set: uint '%s' out of range", path);
        next.uintValue = static_cast<unsigned long long>(value);
        break;
    }
    case ConfigValueKind::Float:
        if (!lua_isnumber(L, 2))
            return luaL_error(L, "config.set: float '%s' needs a number", path);
        next.floatValue = static_cast<double>(lua_tonumber(L, 2));
        break;
    case ConfigValueKind::Color: {
        for (int i = 0; i < 3; ++i) {
            const lua_Number value = luaL_checknumber(L, 2 + i);
            if (value < 0.0 || value > 255.0)
                return luaL_error(L, "config.set: color components must be in range 0-255");
            next.color[i] = static_cast<unsigned char>(value);
        }
        if (!lua_isnoneornil(L, 5)) {
            const lua_Number value = luaL_checknumber(L, 5);
            if (value < 0.0 || value > 255.0)
                return luaL_error(L, "config.set: color components must be in range 0-255");
            next.color[3] = static_cast<unsigned char>(value);
        } else {
            next.color[3] = 255;
        }
        break;
    }
    }
    lua_pushboolean(L, configSetQuery(path, &next) ? 1 : 0);
    return 1;
}



inline int l_configList(lua_State* L)
{
    lua_newtable(L);
    if (!configEntryCountQuery || !configEntryAtQuery)
        return 1;
    const int count = configEntryCountQuery();
    int out = 0;
    for (int i = 0; i < count; ++i) {
        char path[kMaxConfigPath] = {};
        int kind = 0;
        if (!configEntryAtQuery(i, path, sizeof(path), &kind) || path[0] == '\0')
            continue;
        lua_createtable(L, 0, 2);
        lua_pushstring(L, path);
        lua_setfield(L, -2, "path");
        lua_pushstring(L, configKindName(static_cast<ConfigValueKind>(kind)));
        lua_setfield(L, -2, "type");
        lua_rawseti(L, -2, ++out);
    }
    return 1;
}



inline int l_configSave(lua_State* L)
{
    (void)L;
    if (configSaveQuery)
        configSaveQuery();
    return 0;
}




inline void databasePath(char* out, std::size_t outSize, const char* scriptName) noexcept
{
    const std::size_t dirLength = std::strlen(scriptsDirPath);
    const std::size_t nameLength = std::strlen(scriptName);
    if (dirLength + 1 + nameLength + sizeof(".db") > outSize) {
        out[0] = '\0';
        return;
    }
    std::memcpy(out, scriptsDirPath, dirLength);
    out[dirLength] = '/';
    std::memcpy(out + dirLength + 1, scriptName, nameLength + 1);
    std::memcpy(out + dirLength + 1 + nameLength, ".db", sizeof(".db"));
}


inline int l_databaseWrite(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    const std::size_t keyLength = std::strlen(key);
    if (keyLength == 0 || keyLength >= 64)
        return luaL_error(L, "database.write: key must be 1-63 characters");
    for (std::size_t i = 0; i < keyLength; ++i) {
        if (static_cast<unsigned char>(key[i]) < 0x20 || key[i] == '\x7F')
            return luaL_error(L, "database.write: key contains a forbidden character");
    }

    char valueLine[600];
    if (lua_isboolean(L, 2)) {
        std::snprintf(valueLine, sizeof(valueLine), "b\t%d", lua_toboolean(L, 2) ? 1 : 0);
    } else if (lua_isnumber(L, 2)) {
        std::snprintf(valueLine, sizeof(valueLine), "n\t%.17g", lua_tonumber(L, 2));
    } else if (lua_isstring(L, 2)) {
        std::size_t length = 0;
        const char* value = lua_tolstring(L, 2, &length);
        if (length >= 500)
            return luaL_error(L, "database.write: string values must be under 500 bytes");
        std::size_t out = 0;
        valueLine[out++] = 's';
        valueLine[out++] = '\t';
        for (std::size_t i = 0; i < length && out < sizeof(valueLine) - 2; ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if (c < 0x20 || c == '\x7F')
                continue; 
            valueLine[out++] = static_cast<char>(c);
        }
        valueLine[out] = '\0';
    } else {
        return luaL_error(L, "database.write: value must be a string, number or boolean");
    }

    Script& script = selfScript(L);
    char path[700];
    databasePath(path, sizeof(path), script.name);
    if (!path[0])
        return luaL_error(L, "database.write: scripts directory unavailable");

    
    char contents[4096] = {};
    std::size_t contentsLength = 0;
    if (const int fd = ::open(path, O_RDONLY); fd >= 0) {
        ssize_t bytes;
        while (contentsLength < sizeof(contents) - 1 && (bytes = ::read(fd, contents + contentsLength, sizeof(contents) - 1 - contentsLength)) > 0)
            contentsLength += static_cast<std::size_t>(bytes);
        ::close(fd);
        contents[contentsLength] = '\0';
    }

    char kept[4096] = {};
    std::size_t keptLength = 0;
    const auto keyPrefixLength = keyLength + 1; 
    for (char* line = contents; line && *line != '\0';) {
        char* next = std::strchr(line, '\n');
        const bool matches = std::strlen(line) > keyPrefixLength
            && std::memcmp(line, key, keyLength) == 0 && line[keyLength] == '\t';
        if (!matches && next) {
            const std::size_t lineLength = static_cast<std::size_t>(next - line) + 1;
            if (keptLength + lineLength < sizeof(kept)) {
                std::memcpy(kept + keptLength, line, lineLength);
                keptLength += lineLength;
            }
        }
        line = next ? next + 1 : nullptr;
    }

    const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0)
        return luaL_error(L, "database.write: cannot write the database file");
    auto writeAll = [fd](const char* data, std::size_t length) {
        std::size_t done = 0;
        while (done < length) {
            const ssize_t bytes = ::write(fd, data + done, length - done);
            if (bytes <= 0)
                return;
            done += static_cast<std::size_t>(bytes);
        }
    };
    writeAll(kept, keptLength);
    writeAll(key, keyLength);
    writeAll("\t", 1);
    writeAll(valueLine, std::strlen(valueLine));
    writeAll("\n", 1);
    ::close(fd);
    return 0;
}

inline int l_databaseRead(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    const std::size_t keyLength = std::strlen(key);
    Script& script = selfScript(L);
    char path[700];
    databasePath(path, sizeof(path), script.name);
    if (!path[0] || keyLength == 0 || keyLength >= 64) {
        lua_pushnil(L);
        return 1;
    }

    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        lua_pushnil(L);
        return 1;
    }
    char contents[4096] = {};
    std::size_t contentsLength = 0;
    ssize_t bytes;
    while (contentsLength < sizeof(contents) - 1 && (bytes = ::read(fd, contents + contentsLength, sizeof(contents) - 1 - contentsLength)) > 0)
        contentsLength += static_cast<std::size_t>(bytes);
    ::close(fd);
    contents[contentsLength] = '\0';

    for (char* line = contents; line && *line != '\0';) {
        char* next = std::strchr(line, '\n');
        if (next)
            *next = '\0';
        if (std::strlen(line) > keyLength + 2 && std::memcmp(line, key, keyLength) == 0 && line[keyLength] == '\t') {
            const char* type = line + keyLength + 1;
            const char* value = type + 2; 
            if (type[0] == 's' && type[1] == '\t') {
                lua_pushstring(L, value);
                return 1;
            }
            if (type[0] == 'n' && type[1] == '\t') {
                lua_pushnumber(L, std::atof(value));
                return 1;
            }
            if (type[0] == 'b' && type[1] == '\t') {
                lua_pushboolean(L, value[0] == '1' ? 1 : 0);
                return 1;
            }
        }
        line = next ? next + 1 : nullptr;
    }
    lua_pushnil(L); 
    return 1;       
}



inline int l_getAllEntities(lua_State* L)
{
    const char* className = luaL_checkstring(L, 1);
    lua_newtable(L);
    if (!entityListQuery)
        return 1;
    int indices[128];
    const int count = entityListQuery(className, indices, 128);
    if (count < 0) {
        lua_pop(L, 1);
        return luaL_error(L, "entity.get_all: unknown class '%s'", className);
    }
    for (int i = 0; i < count; ++i) {
        lua_pushinteger(L, indices[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}



inline int l_getEntityOrigin(lua_State* L)
{
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    float origin[3];
    if (!entityOriginQuery || index <= 0 || index > kMaxEntityIndex || !entityOriginQuery(index, origin))
        return 0;
    lua_pushnumber(L, origin[0]);
    lua_pushnumber(L, origin[1]);
    lua_pushnumber(L, origin[2]);
    return 3;
}





inline int l_getSpectators(lua_State* L)
{
    if (!spectatorListQuery || !localPlayerIndexQuery || !playerListQuery)
        return 0;
    const int controller = localPlayerIndexQuery();
    if (controller <= 0)
        return 0;
    PlayerListEntry entries[64];
    const int count = playerListQuery(entries, 64);
    int pawnIndex = 0;
    for (int i = 0; i < count; ++i) {
        if (entries[i].controllerIndex == controller) {
            pawnIndex = entries[i].pawnIndex;
            break;
        }
    }
    if (pawnIndex <= 0)
        return 0;
    int out[32];
    const int spectators = spectatorListQuery(pawnIndex, out, 32);
    lua_createtable(L, spectators, 0);
    for (int i = 0; i < spectators; ++i) {
        lua_pushinteger(L, out[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}





inline int l_guiTab(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    std::strncpy(script.tabLabel, label, kMaxGuiLabel - 1);
    script.tabLabel[kMaxGuiLabel - 1] = '\0';
    return 0;
}

inline int l_guiDivider(lua_State* L)
{
    Script& script = selfScript(L);
    const char* label = checkGuiLabel(L, 1);
    if (script.guiItemCount >= kMaxGuiItems)
        return luaL_error(L, "too many gui items (max %d)", kMaxGuiItems);
    GuiItem& item = script.guiItems[script.guiItemCount++];
    item = GuiItem{};
    item.type = GuiItem::Type::Divider;
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    lua_pushinteger(L, script.guiItemCount);
    return 1;
}





inline int l_guiPage(lua_State* L)
{
    if (lua_isnoneornil(L, 1)) {
        pendingItemPage = static_cast<int>(ScriptPage::Subtab); 
        return 0;
    }
    const char* name = luaL_checkstring(L, 1);
    const int page = scriptPageFromName(name);
    if (page < 0)
        return luaL_error(L, "gui.page: unknown page '%s' (Rage, Legit, Movement, Player Info, "
            "Visuals, Viewmodel, Effects, Hud, Sound, Inventory, Radio, Scripts, Misc)", name);
    pendingItemPage = page;
    return 0;
}













inline void requireMenuDispatch(lua_State* L)
{
    if (!imguiMenuActive)
        luaL_error(L, "imgui is only available inside the 'menu' callback"); 
}

inline void requireImguiWindow(lua_State* L)
{
    requireMenuDispatch(L);
    if (imguiWindowDepth <= 0)
        luaL_error(L, "imgui widget called outside imgui.begin()"); 
}

inline ImguiWidgetState* nextImguiWidgetState(lua_State* L)
{
    Script& script = selfScript(L);
    if (script.imguiWidgetCount >= kMaxImguiWidgets)
        luaL_error(L, "too many imgui widgets this frame (max %d)", kMaxImguiWidgets);
    return &script.imguiWidgets[script.imguiWidgetCount++];
}




inline bool imguiLabelPersistable(const char* label) noexcept
{
    const std::size_t length = std::strlen(label);
    if (length == 0 || length >= kMaxGuiLabel)
        return false;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(label[i]);
        if (c < 0x20 || c == '=')
            return false;
    }
    return true;
}





inline bool initImguiWidgetPersistence(lua_State* L, ImguiWidgetState& widget, const char* label, char kind) noexcept
{
    const bool fresh = widget.kind != kind || std::strcmp(widget.label, label) != 0;
    if (!fresh || !imguiLabelPersistable(label))
        return false;
    std::strncpy(widget.label, label, kMaxGuiLabel - 1);
    widget.label[kMaxGuiLabel - 1] = '\0';
    widget.kind = kind;
    Script& script = selfScript(L);
    const PendingGuiValue* saved = findPendingGuiDefault(script.name, label, kind);
    if (!saved)
        return false;
    if (kind == 'i') {
        std::snprintf(widget.text, sizeof(widget.text), "%s", saved->text);
        return true;
    }
    if (kind == 'K') {
        widget.color[0] = static_cast<float>((saved->colorValue >> 24) & 255) / 255.0f;
        widget.color[1] = static_cast<float>((saved->colorValue >> 16) & 255) / 255.0f;
        widget.color[2] = static_cast<float>((saved->colorValue >> 8) & 255) / 255.0f;
        widget.color[3] = static_cast<float>(saved->colorValue & 255) / 255.0f;
        return true;
    }
    return false;
}

inline int l_imguiBegin(lua_State* L)
{
    requireMenuDispatch(L);
    const char* title = luaL_checkstring(L, 1);
    if (title[0] == '\0' || std::strlen(title) >= kMaxImguiTitle)
        luaL_error(L, "imgui.begin: title must be 1-%d characters", static_cast<int>(kMaxImguiTitle) - 1);
    bool open = lua_toboolean(L, 2) != 0; 
    const float width = lua_isnoneornil(L, 3) ? 340.0f : static_cast<float>(luaL_checknumber(L, 3));
    const float height = lua_isnoneornil(L, 4) ? 0.0f : static_cast<float>(luaL_checknumber(L, 4));
    if (!open) {
        lua_pushboolean(L, 0); 
        lua_pushboolean(L, 0); 
        return 2;
    }
    if (width > 0.0f)
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    ++imguiWindowDepth;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 14, 16, 250));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(48, 48, 56, 220));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    const bool visible = ImGui::Begin(title, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    lua_pushboolean(L, visible ? 1 : 0);
    lua_pushboolean(L, open ? 1 : 0); 
    return 2;
}

inline int l_imguiEnd(lua_State* L)
{
    requireMenuDispatch(L);
    if (imguiWindowDepth <= 0)
        return luaL_error(L, "imgui.end_window() without a matching imgui.begin()");
    ImGui::End();
    --imguiWindowDepth;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return 0;
}

inline int l_imguiText(lua_State* L)
{
    requireImguiWindow(L);
    ImGui::TextUnformatted(luaL_checkstring(L, 1));
    return 0;
}

inline int l_imguiTextColored(lua_State* L)
{
    requireImguiWindow(L);
    const ImU32 color = checkColor(L, 2);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", luaL_checkstring(L, 1));
    return 0;
}

inline int l_imguiCheckbox(lua_State* L)
{
    requireImguiWindow(L);
    bool value = lua_toboolean(L, 2) != 0;
    const bool changed = ImGui::Checkbox(luaL_checkstring(L, 1), &value);
    lua_pushboolean(L, value ? 1 : 0);
    lua_pushboolean(L, changed ? 1 : 0);
    return 2;
}

inline int l_imguiSliderFloat(lua_State* L)
{
    requireImguiWindow(L);
    float value = static_cast<float>(luaL_checknumber(L, 2));
    const float min = static_cast<float>(luaL_checknumber(L, 3));
    const float max = static_cast<float>(luaL_checknumber(L, 4));
    const bool changed = ImGui::SliderFloat(luaL_checkstring(L, 1), &value, min, max, "%.2f");
    lua_pushnumber(L, value);
    lua_pushboolean(L, ImGui::IsItemEdited() || changed);
    return 2;
}

inline int l_imguiSliderInt(lua_State* L)
{
    requireImguiWindow(L);
    int value = static_cast<int>(luaL_checkinteger(L, 2));
    const int min = static_cast<int>(luaL_checkinteger(L, 3));
    const int max = static_cast<int>(luaL_checkinteger(L, 4));
    const bool changed = ImGui::SliderInt(luaL_checkstring(L, 1), &value, min, max);
    lua_pushinteger(L, value);
    lua_pushboolean(L, changed ? 1 : 0);
    return 2;
}

inline int l_imguiInputText(lua_State* L)
{
    requireImguiWindow(L);
    const char* label = luaL_checkstring(L, 1);
    const char* text = luaL_checkstring(L, 2);
    ImguiWidgetState& widget = *nextImguiWidgetState(L);
    if (!initImguiWidgetPersistence(L, widget, label, 'i'))
        std::snprintf(widget.text, sizeof(widget.text), "%s", text);
    ImGui::InputText(label, widget.text, sizeof(widget.text));
    lua_pushstring(L, widget.text);
    return 1;
}

inline int l_imguiInputInt(lua_State* L)
{
    requireImguiWindow(L);
    int value = static_cast<int>(luaL_checkinteger(L, 2));
    ImGui::InputInt(luaL_checkstring(L, 1), &value);
    lua_pushinteger(L, value);
    return 1;
}

inline int l_imguiButton(lua_State* L)
{
    requireImguiWindow(L);
    const char* label = luaL_checkstring(L, 1);
    bool pressed = false;
    if (lua_isnoneornil(L, 2))
        pressed = ImGui::Button(label);
    else {
        const float width = static_cast<float>(luaL_checknumber(L, 2));
        const float height = lua_isnoneornil(L, 3) ? 0.0f : static_cast<float>(luaL_checknumber(L, 3));
        pressed = ImGui::Button(label, ImVec2(width, height));
    }
    lua_pushboolean(L, pressed ? 1 : 0);
    return 1;
}



inline int l_imguiCombo(lua_State* L)
{
    requireImguiWindow(L);
    const char* label = luaL_checkstring(L, 1);
    int selected = static_cast<int>(luaL_checkinteger(L, 2)) - 1; 
    constexpr int kMaxComboOptions = 64;
    const char* items[kMaxComboOptions];
    int count = 0;
    if (lua_istable(L, 3)) {
        for (int i = 1; count < kMaxComboOptions; ++i) {
            lua_rawgeti(L, 3, i);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            items[count++] = luaL_checkstring(L, -1);
            lua_pop(L, 1);
        }
    }
    else {
        for (int i = 3; i <= lua_gettop(L) && count < kMaxComboOptions; ++i)
            items[count++] = luaL_checkstring(L, i);
    }
    if (count == 0)
        return luaL_error(L, "imgui.combo: options must not be empty");
    ImGui::Combo(label, &selected, items, count);
    lua_pushinteger(L, selected + 1);
    return 1;
}

inline int l_imguiSeparator(lua_State* L)
{
    requireImguiWindow(L);
    ImGui::Separator();
    return 0;
}

inline int l_imguiSameLine(lua_State* L)
{
    requireImguiWindow(L);
    ImGui::SameLine();
    return 0;
}

inline int l_imguiColorEdit(lua_State* L)
{
    requireImguiWindow(L);
    const char* label = luaL_checkstring(L, 1);
    ImguiWidgetState& widget = *nextImguiWidgetState(L);
    const bool seeded = initImguiWidgetPersistence(L, widget, label, 'K');
    if (!seeded && lua_gettop(L) >= 5) {
        widget.color[0] = static_cast<float>(luaL_checknumber(L, 2)) / 255.0f;
        widget.color[1] = static_cast<float>(luaL_checknumber(L, 3)) / 255.0f;
        widget.color[2] = static_cast<float>(luaL_checknumber(L, 4)) / 255.0f;
        widget.color[3] = static_cast<float>(luaL_checknumber(L, 5)) / 255.0f;
    }
    ImGui::ColorEdit4(label, widget.color, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Uint8);
    lua_pushnumber(L, widget.color[0] * 255.0);
    lua_pushnumber(L, widget.color[1] * 255.0);
    lua_pushnumber(L, widget.color[2] * 255.0);
    lua_pushnumber(L, widget.color[3] * 255.0);
    return 4;
}


inline int l_imguiSetNextWindowPos(lua_State* L)
{
    requireMenuDispatch(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    ImGui::SetNextWindowPos(ImVec2{x, y}, ImGuiCond_FirstUseEver);
    return 0;
}



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
    pushClosure(l_playSound);        lua_setfield(L, -2, "play_sound");
    pushClosure(l_delayCall);        lua_setfield(L, -2, "delay_call");
    pushClosure(l_notify);           lua_setfield(L, -2, "notify");
    pushClosure(l_clipboardGet);     lua_setfield(L, -2, "clipboard_get");
    pushClosure(l_clipboardSet);     lua_setfield(L, -2, "clipboard_set");
    pushClosure(l_getTime);          lua_setfield(L, -2, "get_time");
    pushClosure(l_getMapName);       lua_setfield(L, -2, "get_map_name");
    pushClosure(l_getScreenSize);    lua_setfield(L, -2, "get_screen_size");
    pushClosure(l_getMousePos);      lua_setfield(L, -2, "get_mouse_pos");
    pushClosure(l_isMouseDown);      lua_setfield(L, -2, "is_mouse_down");
    pushClosure(l_isMenuOpen);       lua_setfield(L, -2, "is_menu_open");
    pushFunction(l_isKeyDown);       lua_setfield(L, -2, "is_key_down");
    pushFunction(l_isBindDown);      lua_setfield(L, -2, "is_bind_down");
    pushFunction(l_traceLine);       lua_setfield(L, -2, "trace_line");
    pushFunction(l_traceDamage);     lua_setfield(L, -2, "trace_damage");
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
    pushFunction(l_renderScale);        lua_setfield(L, -2, "scale");
    pushFunction(l_renderLine);         lua_setfield(L, -2, "line");
    pushFunction(l_renderRect);         lua_setfield(L, -2, "rect");
    pushFunction(l_renderFilledRect);   lua_setfield(L, -2, "filled_rect");
    pushFunction(l_renderCircle);       lua_setfield(L, -2, "circle");
    pushFunction(l_renderCircleFilled); lua_setfield(L, -2, "circle_filled");
    pushFunction(l_worldToScreen);      lua_setfield(L, -2, "world_to_screen");
    pushClosure(l_loadImage);           lua_setfield(L, -2, "load_image");
    pushClosure(l_loadImageRgba);       lua_setfield(L, -2, "load_rgba");
    pushFunction(l_imageReady);         lua_setfield(L, -2, "image_ready");
    pushFunction(l_imageSize);          lua_setfield(L, -2, "image_size");
    pushFunction(l_renderImage);        lua_setfield(L, -2, "image");
    pushFunction(l_renderPolygon);      lua_setfield(L, -2, "polygon");
    pushFunction(l_renderArc);          lua_setfield(L, -2, "arc");
    pushFunction(l_renderGradientRect); lua_setfield(L, -2, "gradient_rect");
    pushFunction(l_renderRoundedRect);  lua_setfield(L, -2, "rounded_rect");
    pushFunction(l_renderPushClip);     lua_setfield(L, -2, "push_clip");
    pushFunction(l_renderPopClip);      lua_setfield(L, -2, "pop_clip");
    lua_setglobal(L, "renderer");

    lua_newtable(L);
    pushClosure(l_cmdSetButtons);      lua_setfield(L, -2, "set_buttons");
    pushClosure(l_cmdPressButtons);    lua_setfield(L, -2, "press_buttons");
    pushClosure(l_cmdGetButtons);      lua_setfield(L, -2, "get_buttons");
    pushClosure(l_cmdIsButtonDown);    lua_setfield(L, -2, "is_button_down");
    pushClosure(l_cmdGetViewAngles);   lua_setfield(L, -2, "get_view_angles");
    pushClosure(l_cmdSetViewAngles);   lua_setfield(L, -2, "set_view_angles");
    pushClosure(l_cmdGetForwardMove);  lua_setfield(L, -2, "get_forward_move");
    pushClosure(l_cmdSetForwardMove);  lua_setfield(L, -2, "set_forward_move");
    pushClosure(l_cmdGetLeftMove);     lua_setfield(L, -2, "get_left_move");
    pushClosure(l_cmdSetLeftMove);     lua_setfield(L, -2, "set_left_move");
    pushClosure(l_cmdPressShot);       lua_setfield(L, -2, "press_shot");
    pushClosure(l_cmdPressBank2);      lua_setfield(L, -2, "press_bank2");
    pushClosure(l_cmdSuppressShot);    lua_setfield(L, -2, "suppress_shot");
    pushClosure(l_cmdGetMouseDx);      lua_setfield(L, -2, "get_mouse_dx");
    pushClosure(l_cmdGetRandomSeed);   lua_setfield(L, -2, "get_random_seed");
    pushClosure(l_cmdSetRandomSeed);   lua_setfield(L, -2, "set_random_seed");
    pushClosure(l_cmdGetHistorySize);  lua_setfield(L, -2, "get_history_size");
    pushClosure(l_cmdSetAttackIndex);  lua_setfield(L, -2, "set_attack_index");
    pushClosure(l_cmdForceAttackIndex);lua_setfield(L, -2, "force_attack_index");
    lua_setglobal(L, "cmd");

    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(kInAttack));    lua_setfield(L, -2, "attack");
    lua_pushinteger(L, static_cast<lua_Integer>(kInJump));      lua_setfield(L, -2, "jump");
    lua_pushinteger(L, static_cast<lua_Integer>(kInDuck));      lua_setfield(L, -2, "duck");
    lua_pushinteger(L, static_cast<lua_Integer>(kInForward));   lua_setfield(L, -2, "forward");
    lua_pushinteger(L, static_cast<lua_Integer>(kInBack));      lua_setfield(L, -2, "back");
    lua_pushinteger(L, static_cast<lua_Integer>(kInUse));       lua_setfield(L, -2, "use");
    lua_pushinteger(L, static_cast<lua_Integer>(kInMoveLeft));  lua_setfield(L, -2, "moveleft");
    lua_pushinteger(L, static_cast<lua_Integer>(kInMoveRight)); lua_setfield(L, -2, "moveright");
    lua_pushinteger(L, static_cast<lua_Integer>(kInAttack2));   lua_setfield(L, -2, "attack2");
    lua_pushinteger(L, static_cast<lua_Integer>(kInReload));    lua_setfield(L, -2, "reload");
    lua_pushinteger(L, static_cast<lua_Integer>(kInSpeed));     lua_setfield(L, -2, "speed");
    lua_pushinteger(L, static_cast<lua_Integer>(kInWalk));      lua_setfield(L, -2, "walk");
    lua_pushinteger(L, static_cast<lua_Integer>(kInBullrush));  lua_setfield(L, -2, "bullrush");
    lua_setglobal(L, "buttons");

    lua_newtable(L);
    pushFunction(l_moduleBase);  lua_setfield(L, -2, "module_base");
    pushFunction(l_patternScan); lua_setfield(L, -2, "pattern_scan");
    lua_setglobal(L, "memory");

    lua_newtable(L);
    pushClosure(l_getLocalPlayer);  lua_setfield(L, -2, "get_local_player");
    pushClosure(l_getPlayers);      lua_setfield(L, -2, "get_players");
    pushClosure(l_getPlayerPawn);   lua_setfield(L, -2, "get_player_pawn");
    pushClosure(l_getAllEntities);  lua_setfield(L, -2, "get_all");
    pushClosure(l_getEntityOrigin); lua_setfield(L, -2, "get_origin");
    pushClosure(l_getSpectators);   lua_setfield(L, -2, "get_spectators");
    pushClosure(l_getProp);         lua_setfield(L, -2, "get_prop");
    pushClosure(l_getPropFloat);    lua_setfield(L, -2, "get_prop_float");
    pushClosure(l_getPropString);   lua_setfield(L, -2, "get_prop_string");
    pushClosure(l_getPropVector);   lua_setfield(L, -2, "get_prop_vector");
    pushClosure(l_getEntityClass);  lua_setfield(L, -2, "get_class");
    pushClosure(l_findProp);        lua_setfield(L, -2, "find_prop");
    pushClosure(l_findPropFloat);   lua_setfield(L, -2, "find_prop_float");
    pushClosure(l_findPropString);  lua_setfield(L, -2, "find_prop_string");
    pushClosure(l_findPropVector);  lua_setfield(L, -2, "find_prop_vector");
    pushClosure(l_setProp);         lua_setfield(L, -2, "set_prop");
    pushClosure(l_setPropFloat);    lua_setfield(L, -2, "set_prop_float");
    pushClosure(l_setPropString);   lua_setfield(L, -2, "set_prop_string");
    pushClosure(l_setPropVector);   lua_setfield(L, -2, "set_prop_vector");
    lua_setglobal(L, "entity");

    lua_newtable(L);
    pushClosure(l_guiTab);      lua_setfield(L, -2, "tab");
    pushClosure(l_guiPage);     lua_setfield(L, -2, "page");
    pushClosure(l_guiCheckbox); lua_setfield(L, -2, "checkbox");
    pushClosure(l_guiSlider);   lua_setfield(L, -2, "slider");
    pushClosure(l_guiDropdown); lua_setfield(L, -2, "dropdown");
    pushClosure(l_guiColor);    lua_setfield(L, -2, "color");
    pushClosure(l_guiKeybind);  lua_setfield(L, -2, "keybind");
    pushClosure(l_guiFloatSlider); lua_setfield(L, -2, "float_slider");
    pushClosure(l_guiTextInput);lua_setfield(L, -2, "text_input");
    pushClosure(l_guiDivider);  lua_setfield(L, -2, "divider");
    pushClosure(l_guiGet);      lua_setfield(L, -2, "get");
    pushClosure(l_guiSet);      lua_setfield(L, -2, "set");
    lua_setglobal(L, "gui");

    lua_newtable(L);
    pushClosure(l_configGet);   lua_setfield(L, -2, "get");
    pushClosure(l_configSet);   lua_setfield(L, -2, "set");
    pushClosure(l_configList);  lua_setfield(L, -2, "list");
    pushClosure(l_configSave);  lua_setfield(L, -2, "save");
    lua_setglobal(L, "config");

    lua_newtable(L);
    pushClosure(l_imguiBegin);           lua_setfield(L, -2, "begin");
    pushClosure(l_imguiEnd);             lua_setfield(L, -2, "end_window");
    pushClosure(l_imguiSetNextWindowPos);lua_setfield(L, -2, "set_next_window_pos");
    pushClosure(l_imguiText);            lua_setfield(L, -2, "text");
    pushClosure(l_imguiTextColored);     lua_setfield(L, -2, "text_colored");
    pushClosure(l_imguiCheckbox);        lua_setfield(L, -2, "checkbox");
    pushClosure(l_imguiSliderFloat);     lua_setfield(L, -2, "slider_float");
    pushClosure(l_imguiSliderInt);       lua_setfield(L, -2, "slider_int");
    pushClosure(l_imguiInputText);       lua_setfield(L, -2, "input_text");
    pushClosure(l_imguiInputInt);        lua_setfield(L, -2, "input_int");
    pushClosure(l_imguiButton);          lua_setfield(L, -2, "button");
    pushClosure(l_imguiCombo);           lua_setfield(L, -2, "combo");
    pushClosure(l_imguiColorEdit);       lua_setfield(L, -2, "color_edit");
    pushClosure(l_imguiSeparator);       lua_setfield(L, -2, "separator");
    pushClosure(l_imguiSameLine);        lua_setfield(L, -2, "same_line");
    lua_setglobal(L, "imgui");

    lua_newtable(L);
    pushClosure(l_databaseRead);  lua_setfield(L, -2, "read");
    pushClosure(l_databaseWrite); lua_setfield(L, -2, "write");
    lua_setglobal(L, "database");

    lua_newtable(L);
    pushClosure(l_httpGet);      lua_setfield(L, -2, "get");
    pushClosure(l_httpRequest);  lua_setfield(L, -2, "request");
    lua_setglobal(L, "http");

    lua_newtable(L);
    pushClosure(l_netServer);          lua_setfield(L, -2, "server");
    pushClosure(l_netSendRaw);         lua_setfield(L, -2, "send_raw");
    pushClosure(l_netSetBlockedIps);   lua_setfield(L, -2, "set_blocked_ips");
    pushClosure(l_netClearBlockedIps); lua_setfield(L, -2, "clear_blocked_ips");
    pushClosure(l_netStats);           lua_setfield(L, -2, "stats");
    pushClosure(l_netSetDelay);        lua_setfield(L, -2, "set_delay");
    pushClosure(l_netGetDelay);        lua_setfield(L, -2, "get_delay");
    lua_setglobal(L, "net");

    lua_newtable(L);
    pushFunction(l_steamStatus);        lua_setfield(L, -2, "status");
    pushFunction(l_steamGetSteamId);    lua_setfield(L, -2, "get_steamid");
    pushFunction(l_steamGetLobby);      lua_setfield(L, -2, "get_lobby");
    pushFunction(l_steamGetFriends);    lua_setfield(L, -2, "get_friends");
    pushFunction(l_steamGetFriendName); lua_setfield(L, -2, "get_friend_name");
    pushFunction(l_steamGetFriendState);lua_setfield(L, -2, "get_friend_state");
    pushFunction(l_steamGetFriendGame); lua_setfield(L, -2, "get_friend_game");
    pushFunction(l_steamGetFriendPresence); lua_setfield(L, -2, "get_friend_presence");
    pushFunction(l_steamRequestFriendPresence); lua_setfield(L, -2, "request_friend_presence");
    pushFunction(l_steamInvite);        lua_setfield(L, -2, "invite");
    pushFunction(l_steamLobbyMembers);  lua_setfield(L, -2, "lobby_members");
    pushFunction(l_steamLobbyOwner);    lua_setfield(L, -2, "lobby_owner");
    pushFunction(l_steamRequestFriendInfo); lua_setfield(L, -2, "request_friend_info");
    pushFunction(l_steamGetFriendAvatar);   lua_setfield(L, -2, "get_friend_avatar");
    pushFunction(l_steamAvatarSize);        lua_setfield(L, -2, "avatar_size");
    pushFunction(l_steamAvatarRgba);        lua_setfield(L, -2, "avatar_rgba");
    lua_setglobal(L, "steam");
}
