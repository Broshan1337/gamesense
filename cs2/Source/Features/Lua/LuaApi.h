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
//                                           once per input tick - cmd.* valid inside), "menu"
//                                           (present thread, every frame while the menu is
//                                           open - imgui.* valid inside), "unload" (fired once
//                                           when the script unloads, no dispatch thread -
//                                           game-thread-only bindings refuse inside it), plus
//                                           any game event name (e.g. "player_hurt",
//                                           "weapon_fire", "bomb_planted"). Known events
//                                           pass an `event` table to the callback:
//                                           player_hurt (userid/attacker 0-based player slots,
//                                           65535 = nobody, dmg_health, health, armor,
//                                           dmg_armor, hitgroup, weapon string), player_death
//                                           (userid/attacker/assister/headshot/dominated,
//                                           weapon string), weapon_fire (userid, weapon
//                                           string), item_purchase (userid, team, weapon
//                                           string), bullet_impact + *_detonate (userid,
//                                           x/y/z floats). Other events pass no arguments.
//   client.log(message)                   - engine console (VerifyConsole, throttled)
//   client.exec(command)                  - run a console command through the engine's client
//                                           command buffer (game-thread callbacks only) - chat
//                                           ("say ..."), radio ("playerchatwheel ..."), cvars,
//                                           "pause" on sv_pausable servers, anything console-able
//   client.play_sound(path)               - play a game sound by path ("buttons/bell1.wav") -
//                                           thin wrapper over exec("play <path>")
//   client.delay_call(seconds, fn, ...)   - call fn(args) on a later dispatch (>= seconds from
//                                           now); max 32 pending per script, 1h max delay
//   client.notify(text [, r, g, b])       - on-screen toast (drawn by the framework, top center)
//   client.clipboard_get() / clipboard_set(text) - the system clipboard through the ImGui backend
//   client.get_mouse_pos()                - x, y of the cursor
//   client.is_mouse_down([button])        - 0 = left (default), 1 = right, 2 = middle
//   client.is_bind_down(bind)             - true while a gui.keybind value is physically held
//   client.get_time()                     - steady-clock seconds since module load; the only
//                                           timing source (os.clock is sandboxed away)
//   client.get_screen_size()              - width, height
//   client.is_menu_open()
//   client.trace_line(x1, y1, z1, x2, y2, z2 [, skipLocal])
//                                         - world ray (game-thread callbacks only): fraction,
//                                           endX, endY, endZ, didHit. skipLocal (default true)
//                                           excludes the local pawn. Fails closed (fraction 1,
//                                           didHit false) when the trace manager is unavailable.
//   client.trace_damage(x1..z2, damageAtPoint, penetrationPower [, skipLocal])
//                                         - autowall: the damage that survives the walls between
//                                           the two points given the weapon's raw damage at the
//                                           impact point and penetration power, or nil when the
//                                           shot cannot reach (game-thread callbacks only).
//   cvar.get_int(name) / cvar.get_float(name) - read a runtime convar (nil = absent/wrong type)
//   cvar.set_float(name, value) / cvar.set_bool(name, value) - write through the convar's
//                                           resolved value pointer (same force* path C++
//                                           features use). false = not found / wrong type.
//   renderer.text(x, y, text, r, g, b, a [, size [, outline [, bold]]]) - bold = the
//   menu semibold face
//                                         - size defaults to 14 (6-96), outline draws a dark
//                                           halo so text stays readable over the world
//   renderer.text_size(text [, size [, bold]])      - width, height at the given size
//   renderer.line(x1, y1, x2, y2, r, g, b, a [, thickness])
//   renderer.rect(x, y, w, h, r, g, b, a [, thickness])
//   renderer.filled_rect(x, y, w, h, r, g, b, a)
//   renderer.circle(x, y, radius, r, g, b, a [, segments [, thickness]])
//   renderer.circle_filled(x, y, radius, r, g, b, a [, segments])
//   renderer.image(id, x, y, w, h, r, g, b, a)
//                                         - draw a texture created by renderer.load_image (only
//                                           inside "paint"; silently skipped until the GPU
//                                           upload finishes - poll renderer.image_ready)
//   renderer.load_image(binaryData)       - decode PNG/JPEG/GIF/BMP bytes and stage a texture;
//                                           returns a texture id (1-based) or nil. 8 slots per
//                                           script; released when the script unloads.
//   renderer.load_rgba(w, h, binaryData)  - stage ALREADY-DECODED RGBA8 bytes (w*h*4) - the path
//                                           for raw pixel data (e.g. steam.avatar_rgba)
//   renderer.image_ready(id) / renderer.image_size(id)
//   renderer.world_to_screen(x, y, z)     - screen px, py, or nil when behind the camera /
//                                           matrix unavailable (drawn coords, y grows down)
//   memory.module_base(module)            - load base of a loaded module ("libclient.so", ...)
//   memory.pattern_scan(module, pattern)  - IDA-style "48 8B 05 ?? ?? ??" scan of .text,
//                                           returns the match address as lightuserdata or nil
//   http.get(url, callback)               - async GET; callback(bodyOrNil) fires on a later frame
//   http.request(method, url [, options,] callback)
//                                         - async with options = { headers = {"K: V", ...},
//                                           body = "..." }; callback(bodyOrNil). Method is one
//                                           of GET/POST/PUT/DELETE/HEAD (max 16 headers,
//                                           64KB body).
//   database.read(key) / database.write(key, value)
//                                         - persistent per-script KV store (strings/numbers/
//                                           booleans; sidecar <script>.db, survives reloads)
//   entity.get_local_player()             - local CONTROLLER entity index, or nil when not
//                                           in a game (schema/entity data not ready)
//   entity.get_players()                  - table of controller entity indices (players with
//                                           an active pawn; bots included)
//   entity.get_player_pawn(controllerIdx) - pawn entity index for a controller index, or nil
//   entity.get_all(className)             - table of entity indices of a networkable class
//                                           ("C_PlantedC4", "C_Inferno", "C_WeaponTaser",
//                                           "C_Knife", ...), or nil when the class is unknown
//   entity.get_spectators()               - table of controller entity indices of players
//                                           ACTIVELY spectating the local pawn, or nil when
//                                           unavailable / not in a game
//   entity.get_origin(index)              - world origin x, y, z of an entity (game scene node
//                                           path; C_BaseEntity has no plain origin field), nil
//                                           when unavailable
//   entity.get_prop(index, class, field)  - schema-driven read; declaring class required (the
//                                           schema iterator does not walk parents). Int variant,
//   entity.get_prop_float(...)            - float variant,
//   entity.get_prop_string(...)           - string variant for fixed char arrays (printable
//                                           check; nil when the bytes are not a clean string)
//   entity.get_prop_vector(...)           - x, y, z
//   entity.get_class(index)               - most-derived schema class name ("C_CSPlayerPawn"),
//                                           nil when invalid/unavailable
//   entity.find_prop(index, field, {classes}) - try each declaring class in order; returns
//                                           value + matched class, or nil. Float/string/vector
//   entity.find_prop_float/string/vector  - variants exist (string returns value + class too;
//                                           vector returns x, y, z + class)
//   entity.set_prop(index, class, field, value)      - int32 WRITE (game-thread callbacks only)
//   entity.set_prop_float(index, class, field, value) - float32 WRITE (game-thread callbacks only)
//   entity.set_prop_string(index, class, field, value) - fixed char-array WRITE (game-thread
//                                           callbacks only; truncated + NUL-terminated)
//   entity.set_prop_vector(index, class, field, x, y, z) - 12-byte WRITE (game-thread only)
//   gui.tab(label)                        - name this script's own subtab on the Scripts page
//                                           (default: the file name); one tab per script
//   gui.page([name])                      - render every gui item created AFTER this call in a
//                                           card at the bottom of the named menu page instead
//                                           of the script subtab: "Rage", "Legit", "Movement",
//                                           "Player Info", "Visuals"/"Glow", "Viewmodel",
//                                           "Effects", "Hud", "Sound", "Inventory", "Radio",
//                                           "Scripts", "Misc" (case-insensitive). No argument =
//                                           back to the script subtab. Values persist as usual.
//   gui.checkbox(label [, default])       - menu checkbox in the script's section; 1-based id
//   gui.slider(label, min, max [, default]) - menu slider; 1-based id
//   gui.dropdown(label, options [, defaultIndex]) - menu dropdown (searchable popup when the
//                                           option list is long); gui.get returns the INDEX
//   gui.color(label [, r, g, b [, a]])     - RGBA picker row (shared color-picker popover);
//                                           gui.get returns r, g, b, a (0-255)
//   gui.keybind(label [, default])         - key-capture row (0 = off, 1-248 SDL scancodes,
//                                           249-253 mouse); pair with client.is_bind_down
//   gui.float_slider(label, min, max [, default]) - fractional slider; gui.get returns a number
//   gui.text_input(label [, default])      - single-line text row; gui.get returns the string
//   gui.divider(label)                    - visual separator row in the script's section
//   gui.get(id)                           - current value (boolean / integer / number / string;
//                                           colors return r, g, b, a)
//   gui.set(id, value [, g, b, a])        - set from the script (colors take 4 components);
//                                           values persist per script
//                                           (sidecar <scriptsDir>/<name>.gui, applied by label
//                                           on load, written on unload)
//   imgui.* - REAL ImGui windows for scripts; only valid inside a "menu" callback
//           (client.set_event_callback("menu", fn), fires every frame while the menu is open):
//     imgui.begin(title [, open, width, height]) -> visible, open
//                                           - open a movable/resizable window (its X button
//                                           updates `open`; call end_window() unconditionally)
//     imgui.end_window()                    - close the window begun by imgui.begin
//     imgui.set_next_window_pos(x, y)       - first-use placement for the next imgui.begin
//     imgui.text(text) / imgui.text_colored(text, r, g, b, a)
//     imgui.checkbox(label, value) -> value, changed
//     imgui.slider_float(label, value, min, max) -> value, edited
//     imgui.slider_int(label, value, min, max) -> value, edited
//     imgui.input_text(label, text) -> text - input state lives in per-frame slots, matched by
//                                           call order: keep the call sequence stable per frame.
//                                           Seeded from the sidecar on first use and saved on
//                                           unload (persistable labels only - see gui labels)
//     imgui.input_int(label, value) -> value
//     imgui.button(label [, w, h]) -> pressed
//     imgui.combo(label, index, {"a","b"} | "a", "b", ...) -> newIndex (1-based)
//     imgui.color_edit(label, r, g, b, a) -> r, g, b, a (0-255 floats; omit the colors to keep;
//                                           first-use sidecar seeding like input_text)
//     imgui.separator() / imgui.same_line()
//   renderer.polygon(points, r, g, b, a [, filled [, thickness]])
//                                         - {{x,y},...} or flat {x1,y1,...}; filled=true needs
//                                           a CONVEX polygon (ImGui limitation), max 64 points
//   renderer.arc(x, y, radius, startDeg, endDeg, r, g, b, a [, thickness, segments])
//   renderer.gradient_rect(x, y, w, h, r1, g1, b1, a1, r2, g2, b2, a2 [, vertical])
//   renderer.rounded_rect(x, y, w, h, radius, r, g, b, a [, thickness [, filled]])
//   renderer.push_clip(x, y, w, h) / renderer.pop_clip()
//                                         - manual clip nesting; unbalanced pops error, the
//                                           framework unwinds leftovers at frame end
//   buttons                               - IN_ button bit constants for cmd.*:
//                                           attack, jump, duck, forward, back, use, moveleft,
//                                           moveright, attack2, bullrush, speed, walk, reload
//   cmd.set_buttons(mask, pressed) / cmd.press_buttons(mask) / cmd.get_buttons()
//                                         - button words of the current tick's command (only
//                                           inside "createmove"; press_buttons ORs, set_buttons
//                                           forces both banks on and off)
//   cmd.is_button_down(mask) / cmd.get_view_angles() / cmd.set_view_angles(pitch, yaw)
//   cmd.get_forward_move() / cmd.set_forward_move(v) - normalized -1..1 (same for left_move)
//   cmd.get_left_move() / cmd.set_left_move(v)
//   cmd.press_shot([mask])                - real-click press into BOTH button banks + buttons_pb
//                                           (the copy the server reads); false = buttons_pb
//                                           unreachable, nothing written. Default mask = attack.
//                                           The shot primitive; press_buttons is for movement.
//   cmd.press_bank2(mask)                 - OR into bank 2 (the triggerbot bank); same false
//                                           contract as press_shot
//   cmd.suppress_shot([mask])             - take a shot back off the command (both banks +
//                                           buttons_pb, attack history reset). The spread gate.
//   cmd.get_mouse_dx()                    - raw horizontal mouse delta, or nil
//   cmd.get_random_seed() / cmd.set_random_seed(seed) - per-command RNG seed (unset reads nil)
//   cmd.get_history_size()                - input-history entry count, or nil
//   cmd.set_attack_index(i) / cmd.force_attack_index(i) - point the attack marker at entry i;
//                                           set_ refuses when the game wrote it (a real click),
//                                           force_ overwrites (rage silent-aim write)
//   config.get(path)                      - live native config read ("Combat.Triggerbot.Enabled");
//                                           bool/int/number, or r, g, b, a for colors; nil when
//                                           unknown/unavailable
//   config.set(path, value [, g, b, a])   - live write (range-clamped, autosaved; same
//                                           semantics as loading a .cfg - change handlers do
//                                           NOT run). Colors take r, g, b [, a]. Unknown paths
//                                           error; returns false without the bridge.
//   config.list()                         - every addressable path as { path, type } entries
//   config.save()                         - flush the active config to disk now
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
//   net.set_delay(ms)                     - script send-side delay hold, 0-500ms (0 = off):
//                                           outgoing game datagrams wait `ms` before leaving,
//                                           which is what raises YOUR server-measured ping
//                                           (scoreboard m_iPing is server-side transport RTT -
//                                           it cannot be spoofed, only earned with real delay).
//                                           Gated on Movement > NET LAG > Enabled (turning the
//                                           card off stops script delay too); shares the ring
//                                           and delayed/flushed/overflow counters with the card.
//                                           Cleared on hook unload. Callable from any callback.
//   net.get_delay()                       - the currently published script delay ms (0 = off)
//   net.set_blocked_ips(table)            - publish a block list of IPv4 strings ("1.2.3.4",
//                                           optional "/prefix" length); datagrams to these
//                                           addresses are silently swallowed in the send hook
//                                           EXCEPT datagrams to the currently-connected game
//                                           server. Capped at 512 entries. Returns entries kept.
//   net.clear_blocked_ips()               - drop the block list (traffic resumes untouched)
//   steam.get_steamid()                   - own SteamID64 as a STRING (ids exceed 2^53, a number
//                                           round-trip corrupts them)
//   steam.get_lobby()                     - SteamID64 string of the current Steam lobby, or nil
//                                           when none is open (host a CS2 lobby from the PLAY
//                                           menu first; CS2 publishes it as rich presence)
//   steam.get_friends()                   - table of friend SteamID64 strings (immediate friends)
//   steam.get_friend_name(sid)            - current persona name, or nil
//   steam.get_friend_state(sid)           - EPersonaState int (6 = Looking To Play), or nil
//   steam.get_friend_game(sid)            - { appid = int, lobby = sid-or-nil }, or nil when the
//                                           friend is not in a game (also works on your own id)
//   steam.get_friend_presence(sid)        - friend rich presence table { key = value } (empty
//                                           until cached - pair with request_friend_presence)
//   steam.request_friend_presence(sid)    - refresh a friend's rich presence (async)
//   steam.invite(sid)                     - direct ISteamMatchmaking::InviteUserToLobby on the
//                                           CURRENT lobby - never touches the CS2 party UI or its
//                                           invite cooldown; returns false with no open lobby
//   steam.lobby_members()                 - table of SteamID64 strings in the current lobby
//   steam.lobby_owner()                   - lobby owner's SteamID64 string, or nil
//   steam.request_friend_info(sid)        - async persona+avatar request (works for non-friends,
//                                           e.g. any in-game player); poll get_friend_avatar
//   steam.get_friend_avatar(sid)          - image handle (int) once the avatar is cached, nil
//                                           until then (retry after request_friend_info)
//   steam.avatar_size(handle) / steam.avatar_rgba(handle)
//                                         - width, height / raw RGBA8 bytes of a cached avatar -
//                                           feed straight into renderer.load_rgba
//
// FFI is available as a global (ffi.cast / ffi.C / ffi.load, LuaJIT GC64 build) for raw memory
// work; memory.module_base + memory.pattern_scan give scripts the same anchors our C++ uses.
// Files in <scriptsDir>/lib/*.lua execute before every script body with their globals shared
// (json, base64, vector (vec3 + angle), easing are vendored there) - a require replacement for
// the sandbox. The lib's returned table becomes a global named by the file stem.

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

// Keybind value encoding, mirroring GameClient/Bind.h (the canonical definition - kept as
// plain constants here so the Lua core does not link the bind capture machinery): 0 = off,
// 1..248 = SDL scancodes, 249..253 = mouse buttons (MOUSE4, MOUSE5, MOUSE3, MOUSE1, MOUSE2).
inline constexpr int kBindOff = 0;
inline constexpr int kBindLast = 253;
inline constexpr int kBindMaxScancode = 248;

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
    else if (std::strcmp(eventName, "menu") == 0)
        script.hasMenu = true;
    else if (std::strcmp(eventName, "unload") == 0)
        script.hasUnload = true; // fired once from unloadScriptLocked, no dispatch thread
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

// client.get_map_name() -> "de_mirage"-style name, or nil when no game_newmap has fired since
// injection (main menu, first launch). The name is captured on the game thread by the event hook
// (see lua::setCurrentMapName / game_events::stringForKey) and copied out under its mutex, so it
// is safe to read from tick AND paint dispatches.
inline int l_getMapName(lua_State* L)
{
    char name[64];
    if (copyMapName(name, sizeof(name)))
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
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

// client.play_sound(path) - a thin, validated wrapper over exec("play <path>").
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

// client.delay_call(seconds, fn, ...) - queue fn(args...) on a later dispatch. The queue lives in
// the Script (survives until the script unloads), drained by the paint/tick dispatch loops under
// the framework mutex.
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
    lua_pushvalue(L, 2); // copy of fn on top
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

// client.notify(text [, r, g, b]) - queue a toast; dispatchPaint draws the queue after the
// script callbacks. Control characters are stripped (the text lands in ImGui draw calls only).
// The color is the 3-component text color (alpha is managed by the fade).
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

// client.is_mouse_down([button]) - button 0 = left (default), 1 = right, 2 = middle.
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

// Forward declaration: defined beside the gui.keybind row further down; declared here so the
// client.is_bind_down binding above can use it (single-header ordering, no semantics change).
inline int checkBindValue(lua_State* L, int index);

// client.is_bind_down(bind) - the gui.keybind companion: true while the bound key/button is
// physically held. Scancodes (1..248) read the menu's keyboard state; mouse binds map to the
// ImGui buttons (MOUSE4/5 = X1/X2, MOUSE3 = middle, MOUSE1/2 = left/right - the same
// identities GameClient/Bind.h captures). Safe from any callback.
inline int l_isBindDown(lua_State* L)
{
    const int bind = checkBindValue(L, 1);
    bool down = false;
    if (bind >= 1 && bind <= kBindMaxScancode)
        down = KeyboardState::isKeyDown(bind);
    else if (bind == 249)
        down = ImGui::IsMouseDown(3); // MOUSE4 = X1
    else if (bind == 250)
        down = ImGui::IsMouseDown(4); // MOUSE5 = X2
    else if (bind == 251)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    else if (bind == 252)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    else if (bind == 253)
        down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}

// ---- tracing (game-thread callbacks only: TraceShape touches the live collision world) ----

// The local pawn's C_BaseEntity*, or nullptr - used as the trace filter's skip entity.
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

// Requires the game-thread dispatch context; errors otherwise.
inline void requireGameThread(lua_State* L, const char* api)
{
    if (dispatchThreadKind.load(std::memory_order_relaxed) != 1)
        luaL_error(L, "%s is only available inside createmove / game event callbacks", api);
}

// Reads six floats (x1, y1, z1, x2, y2, z2) starting at `index` into two cs2::Vector points.
inline void checkTracePoints(lua_State* L, int index, cs2::Vector& start, cs2::Vector& end)
{
    start.x = static_cast<float>(luaL_checknumber(L, index));
    start.y = static_cast<float>(luaL_checknumber(L, index + 1));
    start.z = static_cast<float>(luaL_checknumber(L, index + 2));
    end.x = static_cast<float>(luaL_checknumber(L, index + 3));
    end.y = static_cast<float>(luaL_checknumber(L, index + 4));
    end.z = static_cast<float>(luaL_checknumber(L, index + 5));
}

// client.trace_line(x1, y1, z1, x2, y2, z2 [, skipLocal]) -> fraction, endX, endY, endZ, didHit.
// Fails closed like the C++ primitive: an unavailable trace manager reads as a clear ray.
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

// client.trace_damage(x1..z2, damageAtPoint, penetrationPower [, skipLocal]) -> damage or nil.
// The autowall primitive: how much of `damageAtPoint` (the weapon's raw, unarmored damage at the
// impact point) survives the wall layers between the two points.
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
    return 0; // nil - not reachable through walls
}

// ---- renderer (foreground draw list; only valid inside a "paint" callback) ----

inline constexpr float kScriptFontSize = 14.0f;

// The menu's UI scale - HUD scripts multiply their metrics by it so script-drawn panels match
// the native panels (the keybind list at scale 1.4 draws its caption at ~15px, not 11).
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
    // bold = arg 10: the menu's semibold face (Fonts[1]) - the same font the native HUD
    // headers use. Falls back to the regular face when the bold atlas is missing.
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
    const bool bold = lua_toboolean(L, 3) != 0; // matches renderer.text's bold flag
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

// ---- renderer v2: polygon / arc / gradient / rounded / clip ----

// renderer.polygon(points, r, g, b, a [, filled [, thickness]]) - points is a Lua table, either
// {{x,y}, ...} or a flat {x1, y1, x2, y2, ...}. filled=true uses the CONVEX fill (non-convex
// outlines must stay unfilled); default is an outline.
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
            ++i; // consumed the pair
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

// renderer.arc(x, y, radius, startDeg, endDeg, r, g, b, a [, thickness, segments]) - stroke of a
// circular arc, angles in degrees, 0 = right, counter-clockwise positive (screen coords).
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

// renderer.gradient_rect(x, y, w, h, r1, g1, b1, a1, r2, g2, b2, a2 [, vertical])
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

// renderer.rounded_rect(x, y, w, h, radius, r, g, b, a [, thickness [, filled]])
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

// renderer.push_clip(x, y, w, h) / renderer.pop_clip() - manual clip nesting for paint drawing.
// Unbalanced calls are caught: popping with an empty stack errors instead of tripping ImGui's
// clip-rect assertions, and dispatchPaint resets the depth at every frame boundary.
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

// Common acquisition of a free http slot for http.get / http.request. The callback is consumed
// from the top of the stack (luaL_ref).
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
    lua_pushvalue(L, -1); // the callback function (top of stack)
    slot->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return slot;
}

// Appends `value` to a curl config file, escaped for the -K config format (backslash and quote).
static void appendConfigValue(int fd, const char* value) noexcept
{
    for (const char* p = value; *p != '\0'; ++p) {
        char two[2] = {*p, '\0'};
        if (*p == '"' || *p == '\\')
            ::write(fd, "\\", 1);
        ::write(fd, two, 1);
    }
}

// Writes the curl -K config for a request. Everything script-controlled (url, headers, body
// reference) goes through the CONFIG FILE, never the shell command line, so the command stays a
// fixed string and injection is impossible by construction.
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

    // The URL lands inside the curl config file (single-quoted) - forbid everything that could
    // break out of it (scripts receive arbitrary text from anywhere they fetch from, but the URL
    // itself is script-controlled; still, fail closed).
    for (const char* p = url; *p != '\0'; ++p) {
        if (*p == '\'' || *p == '"' || *p == '`' || *p == '\n' || *p == '\r' || *p == '\\' || static_cast<unsigned char>(*p) < 0x20)
            return luaL_error(L, "http.get: url contains a forbidden character");
    }

    // The callback is ALREADY on top at entry ([url, callback]) - acquireHttpSlot refs it
    // directly (pushvalue(-1) + luaL_ref, net zero). NOTE: do NOT push a copy here before
    // acquireHttpSlot - the copy used to leak (never popped on the success path), and that
    // one leaked stack value under the armed count hook wedged the VM's C-call frame
    // resolution into an underflow (frozen re-dispatch loop + stack-base corruption, 2026-09-11/12).
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

// http.request(method, url [, options,] callback) - the full-featured async request.
// options = { headers = {"Key: value", ...}, body = "..." }.
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

    // Same no-leak contract as http.get: the callback is on top, acquireHttpSlot refs it
    // directly. (The old pushed copy leaked on the success path - see the http.get note.)
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
    item.page = pendingItemPage;
    std::strncpy(item.label, label, kMaxGuiLabel - 1);
    item.boolValue = defaultValue;
    if (const auto* saved = findPendingGuiDefault(script.name, label, 'c'))
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
    lua_pushinteger(L, item.intValue); // slider, dropdown, keybind
    return 1;
}

inline int l_guiSet(lua_State* L)
{
    GuiItem& item = guiItemAt(L, static_cast<int>(luaL_checkinteger(L, 1)));
    if (item.type == GuiItem::Type::Checkbox) {
        item.boolValue = lua_toboolean(L, 2) != 0;
    } else if (item.type == GuiItem::Type::Dropdown) {
        // Dropdowns index 0..optionCount-1 (NOT the slider min/max range, which stays 0/0).
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

// gui.color(label [, r, g, b [, a]]) - an RGBA picker row (the menu's shared color-picker
// popover); gui.get returns r, g, b, a. Values persist in the sidecar like every gui item.
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

// gui.keybind(label [, default]) - a key-capture row (the menu's shared keybind pill);
// gui.get returns the Bind int (0 = off). Pair with client.is_bind_down to read it.
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

// gui.float_slider(label, min, max [, default]) - a fractional slider row (the menu's shared
// slider visuals with a decimal pill); gui.get returns a number.
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

// gui.text_input(label [, default]) - a single-line text row; gui.get returns the string.
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

// Non-arg-reading core shared by the explicit-class and candidate-list (find_*) readers:
// validates and resolves (entityIndex, className, fieldName). Returns false with nil pushed
// (when L != nullptr) for unavailable data; errors through L on malformed arguments.
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

// Validates the common arguments and resolves the schema offset + entity pointer for
// (entityIndex, className, fieldName). Returns false with nil pushed when the entity/schema
// data is unavailable (not in a game, unknown field); errors on malformed arguments.
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

// Vector prop read: returns x, y, z as three numbers (or nil when the entity/schema data is
// unavailable). Same read-only rules as get_prop - callable from any callback.
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

// entity.get_class(index) - the entity's most-derived schema class name ("C_CSPlayerPawn"
// etc.) through the game's own entity-class map. Nil when invalid/unavailable. Use it to
// discover which declaring class a field lives on before calling get_prop.
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

// Candidate-list readers: entity.find_prop(index, field, {"C_CSPlayerPawn", "C_BasePlayerPawn",
// "C_BaseEntity"}) tries each declaring class in order and returns the value plus the class
// that matched (value, className), or nil when no candidate has the field. Same read-only
// rules as get_prop - callable from any callback.
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

// Raw keyboard poll: SDL scancode -> held?. Read-only (the same state the menu's bind system
// polls), safe from any callback. Scripts pair it with a dropdown of scancode names.
inline int l_isKeyDown(lua_State* L)
{
    const int scancode = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, KeyboardState::isKeyDown(scancode) ? 1 : 0);
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

// entity.set_prop_string(index, class, field, value) - fixed char-array WRITE (game-thread
// callbacks only). The value is truncated to fit and always NUL-terminated; the tail of the
// field is zeroed so no stale bytes survive a shorter write.
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

// entity.set_prop_vector(index, class, field, x, y, z) - 12-byte vector WRITE (game-thread
// callbacks only).
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

// net.set_delay(ms) - publish a script-driven send-side delay hold (0 = off, max 500ms).
// Lock-free atomic publish like the rest of the net bridge; the send hook applies
// max(menu card ms, script ms) while the NET LAG master switch is on (see NetLag.h
// isScriptDelayActive). Callable from any callback - no game-thread state is touched.
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

// ---- steam (Steamworks through the game's own libsteam_api.so) ----
//
// Script-level equivalent of the CSGO "gamesense/steamworks" require - enough to port the
// invite-cooldown-bypass luas: invites are issued with direct
// ISteamMatchmaking::InviteUserToLobby calls, so the CS2 party UI (which owns the client-side
// invite cooldown) is bypassed by construction - no Panorama ActionInviteFriend hook needed.
//
// No CS2 code is hooked: everything goes through the FLAT C-ABI accessors this libsteam_api
// exports (SteamAPI_SteamUser_v023 / SteamAPI_SteamFriends_v018 / SteamAPI_SteamMatchmaking_v009
// return the singletons; SteamAPI_ISteam*_* are the methods), so no vtable slot or interface
// version string has to be guessed. The library is located by scanning our own /proc/self/maps -
// the same proven pattern ChatTools.h uses (plain dlopen-by-soname is not reliable here).
// ChatTools resolves the same file for the persona feature; the scan is duplicated here on
// purpose so the Lua core stays self-contained.
//
// CSteamIDs cross the boundary as STRINGS (formatSteamId64/parseSteamId64 in Lua.cpp).

inline constexpr unsigned long long kSteamFriendFlagImmediate = 4; // k_EFriendFlagImmediate
inline constexpr unsigned long long kSteamFriendFlagAll = 0xFFFF;  // k_EFriendFlagAll

// FriendGameInfo_t (Steamworks SDK layout - CGameID, IP, ports, CSteamID lobby).
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
    double lastAttempt = 0.0; // failure retries are throttled
    char lastError[160] = {}; // the resolve failure reason, shown by steam.status()

    void* user = nullptr;         // ISteamUser023
    void* friends = nullptr;      // ISteamFriends018
    void* matchmaking = nullptr;  // ISteamMatchmaking009

    std::uint64_t (*getSteamId)(void*) = nullptr;                          // ISteamUser::GetSteamID
    const char* (*getPersonaName)(void*) = nullptr;                        // ISteamFriends::GetPersonaName (sanity)
    int (*getFriendCount)(void*, int) = nullptr;                           // GetFriendCount
    std::uint64_t (*getFriendByIndex)(void*, int, int) = nullptr;          // GetFriendByIndex
    const char* (*getFriendName)(void*, std::uint64_t) = nullptr;          // GetFriendPersonaName
    int (*getFriendState)(void*, std::uint64_t) = nullptr;                 // GetFriendPersonaState
    bool (*getFriendGame)(void*, std::uint64_t, FriendGameInfo*) = nullptr; // GetFriendGamePlayed
    const char* (*getRichPresence)(void*, std::uint64_t, const char*) = nullptr; // GetFriendRichPresence
    int (*getRichPresenceKeyCount)(void*) = nullptr;                       // GetFriendRichPresenceKeyCount
    const char* (*getRichPresenceKey)(void*, int) = nullptr;               // GetFriendRichPresenceKeyByIndex
    void (*requestRichPresence)(void*, std::uint64_t) = nullptr;           // RequestFriendRichPresence
    bool (*inviteToLobby)(void*, std::uint64_t, std::uint64_t) = nullptr;  // InviteUserToLobby
    int (*getLobbyMemberCount)(void*, std::uint64_t) = nullptr;            // GetNumLobbyMembers
    std::uint64_t (*getLobbyMember)(void*, std::uint64_t, int) = nullptr;  // GetLobbyMemberByIndex
    std::uint64_t (*getLobbyOwner)(void*, std::uint64_t) = nullptr;        // GetLobbyOwner

    // Avatar surface (OPTIONAL members - resolved when the exports exist; never part of the
    // hard-missing check, so one absent export cannot kill every steam.* binding for a session).
    int (*getLargeAvatar)(void*, std::uint64_t) = nullptr;           // GetLargeFriendAvatar (184x184)
    int (*getMediumAvatar)(void*, std::uint64_t) = nullptr;          // GetMediumFriendAvatar (64x64)
    bool (*requestUserInfo)(void*, std::uint64_t, bool) = nullptr;   // RequestUserInformation
    void* utils = nullptr;                                           // ISteamUtils (image accessors)
    bool (*getImageSize)(void*, int, unsigned int*, unsigned int*) = nullptr;  // GetImageSize
    bool (*getImageRgba)(void*, int, unsigned char*, int) = nullptr;           // GetImageRGBA
};

static SteamApi steamApi;

// Anomaly-log line for the steam resolve (plain posix append to /tmp/gamesense_gui.log - the
// same contract as gui_log, but without pulling the platform-API mock surface into the tests).
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

// Resolves the whole steam API on first use. The ACTUAL loaded libsteam_api.so path is found by
// scanning /proc/self/maps (chunked pread with a carry-over line buffer - the same proven pattern
// ChatTools.h uses, including the "steam library paths contain spaces" trap).
static bool resolveSteamApi() noexcept
{
    if (steamApi.ok)
        return true;
    // Failures retry (throttled): the resolve can legitimately fail during early startup before
    // SteamAPI_Init, and a permanent cache would kill steam.* for the whole session.
    if (steamApi.resolved && luaNow() - steamApi.lastAttempt < 5.0)
        return false;
    steamApi.resolved = true;
    steamApi.lastAttempt = luaNow();

    char steamApiPath[512] = "";
    {
        // Lua.cpp does its file IO with plain posix calls (see the gui sidecar path) - same here.
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

                    // path = the first '/' in the line - the numeric fields before it never
                    // contain slashes, and Steam library paths contain spaces
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
                // carry the trailing partial line into the next chunk
                carryLength = 0;
                const std::size_t remaining = static_cast<std::size_t>(got) - lineBegin;
                if (remaining > 0 && remaining < kCarry) {
                    std::memcpy(carry, chunk + lineBegin, remaining);
                    carryLength = remaining;
                } else if (remaining >= kCarry) {
                    carryLength = 0; // pathological line - skip it rather than overflow
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

    // dlopen by full path (RTLD_NOLOAD - it is already loaded and kept alive by the game's own
    // refcount, so the addresses stay valid after the balanced dlclose in the dtor, the same
    // lifetime assumption memory.pattern_scan makes).
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

    // Optional avatar surface - resolved only when these exports exist (they do in the game's
    // own libsteam_api.so; older runtimes ship the same flat names).
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

    // Sanity: the user interface must return a real SteamID64 (the universe+account-type bits
    // 0x0110000100000000 = 0x011000010 << 32 in the high half) and the friends interface the
    // live persona name - protects against an accessor/ABI surprise before any script sends an
    // invite.
    const std::uint64_t ownId = steamApi.getSteamId(steamApi.user);
    const char* persona = steamApi.getPersonaName(steamApi.friends);
    constexpr std::uint64_t kSteamId64HighBits = 0x0110000100000000ULL >> 32; // universe+type
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

// steam.status() -> diagnostic string: "ok", or why the steam API is unavailable.
inline int l_steamStatus(lua_State* L)
{
    if (resolveSteamApi()) {
        lua_pushstring(L, "ok");
    } else {
        lua_pushstring(L, steamApi.lastError[0] != '\0' ? steamApi.lastError : "unknown failure");
    }
    return 1;
}

// Friend enumeration with a flag fallback: k_EFriendFlagImmediate returned 0 in the CS2 steam
// context in-game (the persona/lobby calls on the SAME interface work), so retry with the All
// mask before giving up. `activeFlag` remembers which mask answered for the paired
// GetFriendByIndex calls.
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

// True when `ownId` is a member of `lobby` (the lobby data of lobbies we are a member of is
// local, so this is a cheap check; an invalid/foreign lobby answers with 0 members).
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

// Extracts a plausible lobby id (a 15-20 digit run) from a rich presence string.
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
        // skip the digit run (safe: the loop below re-finds a valid starting digit)
        while (*p >= '0' && *p <= '9')
            ++p;
        --p; // compensate the outer ++p
    }
    return best;
}

// Current Steam lobby id. Three sources, in order (CS2 only fills the friend-game lobby field
// once a server is joined - in the main menu the party lobby needs the fallbacks):
//   1. GetFriendGamePlayed on OUR OWN id (works in-game; the party lobby rides rich presence)
//   2. our rich presence strings ("connect"/"status"/"game") - a digit run validated by
//      checking we are actually a member of that lobby
//   3. a friends scan - a friend sitting in our party publishes the shared lobby id through
//      their own game info; membership check confirms it is ours
// The successful method is logged once for diagnosis.
static std::uint64_t currentLobbyId() noexcept
{
    if (!steamApi.ok || !steamApi.user || !steamApi.friends)
        return 0;
    const std::uint64_t ownId = steamApi.getSteamId(steamApi.user);

    FriendGameInfo info{};
    if (steamApi.getFriendGame(steamApi.friends, ownId, &info) && info.lobby != 0)
        return info.lobby;

    // Fallback 2: rich presence. CS2 puts the connect info into these keys in the menu.
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

    // Fallback 3: a friend in our own party publishes the shared lobby id in their game info.
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

// Accepts a SteamID64 as a string (the transport every other binding returns) or as a Lua
// integer (convenience for hand-built test ids).
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

// steam.get_friend_presence(sid) -> { key = value, ... } - the friend's current rich presence.
// Empty table when nothing is cached; pair with steam.request_friend_presence (the data lands
// on a later Steam callback, so the NEXT rescan sees it).
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

// steam.request_friend_presence(sid) - ask Steam to refresh a friend's rich presence (the
// response arrives asynchronously; read it on a later rescan).
inline int l_steamRequestFriendPresence(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    steamApi.requestRichPresence(steamApi.friends, sid);
    return 0;
}

// The bypass primitive: a direct Steam lobby invite. Deliberately no CS2 party/panorama code in
// the path - that is the whole point (whatever cooldown the CS2 UI enforces never runs).
inline int l_steamInvite(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid))
        return 0;
    const std::uint64_t lobby = currentLobbyId();
    if (lobby == 0)
        return 0; // nil/false-ish: no open lobby to invite into
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

// ---- steam avatars (GetLarge/MediumFriendAvatar + the ISteamUtils image accessors) ----
//
// The avatar handle is only valid after Steam cached the user's persona data; for non-friends
// (any in-game player) call steam.request_friend_info(sid) once and the data lands a few frames
// later. RGBA bytes out of GetImageRGBA are top-down RGBA8 - exactly what renderer.load_rgba
// wants, so no re-encoding step exists anywhere in the path.

// steam.request_friend_info(sid) -> true when the async persona+avatar request was issued.
inline int l_steamRequestFriendInfo(lua_State* L)
{
    std::uint64_t sid = 0;
    if (!resolveSteamApi() || !checkSteamIdArg(L, 1, &sid) || !steamApi.requestUserInfo)
        return 0;
    lua_pushboolean(L, steamApi.requestUserInfo(steamApi.friends, sid, true) ? 1 : 0);
    return 1;
}

// steam.get_friend_avatar(sid) -> image handle (int) or nil while the avatar is not cached.
// Prefers the 184x184 handle, falls back to 64x64.
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

// steam.avatar_size(imageHandle) -> width, height, or nil.
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

// steam.avatar_rgba(imageHandle) -> raw RGBA8 bytes (width*height*4), or nil.
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

// ---- images (renderer.load_image; stb decode + the generic Vulkan texture pool) ----

inline constexpr int kMaxLuaTextures = 8; // VulkanHook::lua_texture::kMaxTextures (bridge, kept in sync)

struct LuaTexture {
    int scriptIndex = -1; // owning script slot, -1 = free
    int width = 0;
    int height = 0;
};
static LuaTexture luaTextures[kMaxLuaTextures];

// Releases every texture owned by a script (unloadScript path, mutex held).
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

// Finds a free per-script slot + pool slot and stages `pixels` (RGBA8; ownership passes to the
// Vulkan uploader, which free()s it) into the Vulkan texture pool. Returns the 1-based texture
// id, -1 when the script slots are full, -2 when the pool is exhausted, -3 without a bridge.
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
    script.textureSlots[scriptSlot] = poolIndex + 1; // 1-based id
    return poolIndex + 1;
}

// renderer.load_image(data) -> texture id or nil. Decodes PNG/JPEG/GIF/BMP bytes synchronously
// (one-time cost at script load; the GPU upload is async and rideable via image_ready).
inline int l_loadImage(lua_State* L)
{
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    if (length == 0 || length > 16 * 1024 * 1024)
        return luaL_error(L, "renderer.load_image: data must be 1-16MB");

    int width = 0;
    int height = 0;
    // stb decodes with the C allocator - the Vulkan uploader takes ownership (free()s it).
    unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(data),
        static_cast<int>(length), &width, &height, nullptr, 4);
    if (!pixels || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        if (pixels)
            std::free(pixels);
        return 0; // nil - undecodable
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

// renderer.load_rgba(width, height, data) -> texture id or nil. Stages ALREADY-DECODED RGBA8
// bytes (width*height*4) - the path for images we hold in raw form, e.g. Steam avatars via
// steam.avatar_rgba (the stb decode of load_image only accepts encoded formats).
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

    // The Vulkan uploader owns (and free()s) the buffer, so hand it a private copy of the Lua
    // string bytes.
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

// ---- cmd (the tick's user command; createmove callbacks only) ----

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

// The current tick's command, or null outside the game-thread cmd context.
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

// cmd.press_shot([mask]) - press buttons the way a real click does: the mask lands in BOTH
// button banks of the raw command AND of buttons_pb (the copy the server reads). Returns
// false when buttons_pb is unreachable (nothing was written - do not count a shot).
// Default mask = attack. This is the shot primitive; press_buttons (bank 1 only) is for
// movement keys.
inline int l_cmdPressShot(lua_State* L)
{
    requireGameThread(L, "cmd.press_shot");
    const std::uint64_t mask = lua_isnoneornil(L, 1)
        ? kInAttack : static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    lua_pushboolean(L, currentCmd().pressButtonsBothBanks(mask) ? 1 : 0);
    return 1;
}

// cmd.press_bank2(mask) - OR the mask into button bank 2 (raw word + buttons_pb), the bank
// the reference triggerbot uses for attack. Returns false when buttons_pb is unreachable.
inline int l_cmdPressBank2(lua_State* L)
{
    requireGameThread(L, "cmd.press_bank2");
    const std::uint64_t mask = static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    lua_pushboolean(L, currentCmd().pressButtonsInBank2(mask) ? 1 : 0);
    return 1;
}

// cmd.suppress_shot([mask]) - take a shot AWAY from a command the game (or a real click)
// already put one on: clears the mask from both raw banks AND both buttons_pb banks and
// resets attack1_start_history_index to "no attack". The spread-gate primitive. Default = attack.
inline int l_cmdSuppressShot(lua_State* L)
{
    requireGameThread(L, "cmd.suppress_shot");
    const std::uint64_t mask = lua_isnoneornil(L, 1)
        ? kInAttack : static_cast<std::uint64_t>(luaL_checknumber(L, 1));
    currentCmd().suppressAttack(mask);
    return 0;
}

// cmd.get_mouse_dx() - the tick's raw horizontal mouse delta, or nil when unavailable.
inline int l_cmdGetMouseDx(lua_State* L)
{
    requireGameThread(L, "cmd.get_mouse_dx");
    if (const auto dx = currentCmd().mouseDx(); dx.hasValue())
        lua_pushinteger(L, dx.value());
    else
        lua_pushnil(L);
    return 1;
}

// cmd.get_random_seed() / cmd.set_random_seed(seed) - the per-command RNG seed the server
// rewinds against. Unset reads as nil (distinct from seed 0).
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

// cmd.get_history_size() - how many input-history entries this command carries (nil when
// unavailable); cmd.set_attack_index(i) - point attack1_start_history_index at entry i.
// Refuses when the game already wrote the field (a real click owns it) - use
// cmd.force_attack_index(i) to overwrite anyway (the rage silent-aim write: without it the
// server resolves the shot along the crosshair entry, ignoring redirected angles).
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

// ---- config (live read/write of the native menu config vars by dotted path) ----

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

// config.get(path) - reads "Combat.Triggerbot.Enabled"-style paths (see config.list()).
// Returns a boolean / integer / number, or r, g, b, a (0-255) for colors; nil when the path
// is unknown or the bridge is unavailable (unit tests).
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

// config.set(path, value [, g, b, a]) - writes through the same schema the .cfg save/load
// uses (range-clamped, autosaved; change handlers do NOT run - identical to loading a file).
// Colors take r, g, b [, a] (default 255); bools accept booleans or 0/1. Unknown paths error;
// an unavailable bridge returns false.
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

// config.list() - every addressable path as an array of { path = "...", type = "bool"/... }.
// Empty table without the bridge.
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

// config.save() - flush the active config to disk now (the navbar SAVE pipeline; every
// config.set already schedules an autosave, this just forces it immediately).
inline int l_configSave(lua_State* L)
{
    (void)L;
    if (configSaveQuery)
        configSaveQuery();
    return 0;
}

// ---- database (persistent per-script KV sidecar) ----

// "<scriptsDir>/<name>.db"; line format "key\t<type>\t<value>" (s = string, n = number, b = bool).
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

// Read-modify-write of the sidecar: the file is tiny, so a full rewrite keeps the format trivial.
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
                continue; // strings persist as printable text - strip the rest
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

    // Read the existing sidecar, drop the key's line, keep the rest.
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
    const auto keyPrefixLength = keyLength + 1; // key + '\t'
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
            const char* value = type + 2; // skip the type char and its tab
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
    lua_pushnil(L); // absent - MUST be a real nil: returning zero values makes
    return 1;       // tonumber(database.read(k)) a ZERO-ARG tonumber ("value expected" error)
}

// ---- entity.get_all / entity.get_origin ----

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

// entity.get_origin(index) -> x, y, z or nil - world origin through the game scene node (the
// only reliable path; C_BaseEntity's schema has no plain origin field on this build).
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

// entity.get_spectators() -> table of controller entity indices of players ACTIVELY spectating
// the local pawn (observer mode != 0 and m_hObserverTarget == local pawn), or nil when the
// bridges are unavailable / not in a game. The m_pObserverServices chain is a pointer field, so
// the walk lives behind spectatorListQuery (installed in EntryPoints finishInit).
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

// ---- gui.tab / gui.divider ----

// Names this script's own subtab on the Scripts page (one tab per script; default label = the
// file name without ".lua"). Purely cosmetic - the label is re-read every menu frame.
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

// gui.page(name) - render every gui item created AFTER this call in a card at the bottom of
// the named menu page (Rage, Legit, Movement, Player Info, Glow/Visuals, Viewmodel, Effects,
// Hud, Sound, Inventory, Radio, Scripts, Misc - case-insensitive) instead of the script's own
// Scripts-page sub-tab. Call gui.page() with no argument to go back to the sub-tab.
inline int l_guiPage(lua_State* L)
{
    if (lua_isnoneornil(L, 1)) {
        pendingItemPage = static_cast<int>(ScriptPage::Subtab); // back to the script sub-tab
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

// ---- imgui (script-owned real ImGui windows; "menu" callback only) ----
//
// The full native widget surface: a script opens a movable ImGui window in its "menu" callback
// and fills it with real ImGui widgets. Windows render (and take input) while the MENU IS OPEN
// - with the menu closed the game owns the mouse (relative mode + grab), so script windows
// simply do not render, exactly like the rest of the menu shell. Contracts:
//  * call imgui.end_window() once per imgui.begin(), even when begin returned false
//  * keep the widget call order stable across frames (input_text/color_edit state is matched
//    to slots by call order - the same contract ImGui itself uses for item ids)
//  * widget calls outside a window (before begin / after end) are errors - without this they
//    would render into the menu shell's own window at its cursor position

inline void requireMenuDispatch(lua_State* L)
{
    if (!imguiMenuActive)
        luaL_error(L, "imgui is only available inside the 'menu' callback"); // longjmps
}

inline void requireImguiWindow(lua_State* L)
{
    requireMenuDispatch(L);
    if (imguiWindowDepth <= 0)
        luaL_error(L, "imgui widget called outside imgui.begin()"); // longjmps
}

inline ImguiWidgetState* nextImguiWidgetState(lua_State* L)
{
    Script& script = selfScript(L);
    if (script.imguiWidgetCount >= kMaxImguiWidgets)
        luaL_error(L, "too many imgui widgets this frame (max %d)", kMaxImguiWidgets);
    return &script.imguiWidgets[script.imguiWidgetCount++];
}

// Sidecar persistence only round-trips labels that fit the gui label rules (no control
// characters/= that would break the tab-separated sidecar lines). Anything else still works
// in-session, it just does not persist.
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

// First-use seeding for stateful imgui widgets (input_text/color_edit). Slots are matched by
// call order, so a slot is fresh when its stored label/kind differs from this call. Fresh
// slots adopt the sidecar value (same label+kind scheme as gui.*, written back by
// saveGuiState on unload) and report true; the caller then skips its default-value seeding.
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
    bool open = lua_toboolean(L, 2) != 0; // default: open
    const float width = lua_isnoneornil(L, 3) ? 340.0f : static_cast<float>(luaL_checknumber(L, 3));
    const float height = lua_isnoneornil(L, 4) ? 0.0f : static_cast<float>(luaL_checknumber(L, 4));
    if (!open) {
        lua_pushboolean(L, 0); // not visible
        lua_pushboolean(L, 0); // not open
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
    lua_pushboolean(L, open ? 1 : 0); // the X button may have closed it
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

// imgui.combo(label, index, options...) - options as a table {"a","b"} or varargs strings.
// `index` is 1-based in Lua (0 = nothing selected); returns the new 1-based index.
inline int l_imguiCombo(lua_State* L)
{
    requireImguiWindow(L);
    const char* label = luaL_checkstring(L, 1);
    int selected = static_cast<int>(luaL_checkinteger(L, 2)) - 1; // lua 1-based -> imgui 0-based
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

// imgui.set_next_window_pos(x, y) - first-use placement for the NEXT imgui.begin().
inline int l_imguiSetNextWindowPos(lua_State* L)
{
    requireMenuDispatch(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    ImGui::SetNextWindowPos(ImVec2{x, y}, ImGuiCond_FirstUseEver);
    return 0;
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
