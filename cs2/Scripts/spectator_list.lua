-- Spectator list HUD (ported from the gamesense spectator list to the Neversnooze framework).
-- Window with everyone currently spectating you: accent bar (solid / fade / dynamic-fade
-- palette), header, name rows with Steam avatars. Fades in/out, draggable while the menu is
-- open, position persists across sessions.

gui.tab("SPECTATORS")

local show = gui.checkbox("Spectators", false)

local palettes = { "Solid", "Fade", "Dynamic fade" }
local palette = gui.dropdown("Palette", palettes, 0)

local color_r = gui.slider("Color red", 0, 255, 142)
local color_g = gui.slider("Color green", 0, 255, 165)
local color_b = gui.slider("Color blue", 0, 255, 229)
local color_a = gui.slider("Color alpha", 0, 255, 85)
local fade_offset = gui.slider("Fade offset", 1, 1000, 825)
local fade_freq = gui.slider("Fade frequency", 1, 100, 10)
local fade_split = gui.slider("Fade split ratio", 0, 100, 100)

local RES = 10000
-- database.read returns NOTHING (not nil) for a missing key on older builds, and tonumber()
-- with zero arguments raises "value expected" - assign first, then convert explicitly.
local saved_x = database.read("pos_x")
local saved_y = database.read("pos_y")
local pos_x = (saved_x ~= nil and tonumber(saved_x)) or 7220
local pos_y = (saved_y ~= nil and tonumber(saved_y)) or 5000

local m_alpha = 0
local tex_cache = {}
local tex_count = 0
local drag = { active = false, ox = 0, oy = 0 }
local last_now = nil

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function hsv_to_rgb(h, s, v)
    local r, g, b
    local i = math.floor(h * 6)
    local f = h * 6 - i
    local p = v * (1 - s)
    local q = v * (1 - f * s)
    local t = v * (1 - (1 - f) * s)
    i = i % 6
    if i == 0 then r, g, b = v, t, p
    elseif i == 1 then r, g, b = q, v, p
    elseif i == 2 then r, g, b = p, v, t
    elseif i == 3 then r, g, b = p, q, v
    elseif i == 4 then r, g, b = t, p, v
    elseif i == 5 then r, g, b = v, p, q
    end
    return r * 255, g * 255, b * 255
end

local function get_bar_color()
    local r, g, b, a = gui.get(color_r), gui.get(color_g), gui.get(color_b), gui.get(color_a)
    local pal = gui.get(palette)
    if pal ~= 1 then
        local split = gui.get(fade_split) / 100
        local h = 0
        if pal == 3 then
            h = client.get_time() * (gui.get(fade_freq) / 100)
        else
            h = gui.get(fade_offset) / 1000
        end
        r, g, b = hsv_to_rgb(h, 1, 1)
        r, g, b = r * split, g * split, b * split
    end
    return r, g, b, a
end

-- Horizontal gradient across w in 2px slices (renderer has no gradient primitive).
local function gradient(x, y, w, h, r1, g1, b1, a1, r2, g2, b2, a2)
    local steps = math.max(1, math.floor(w / 2))
    for i = 0, steps - 1 do
        local t0 = i / steps
        local t1 = (i + 1) / steps
        renderer.filled_rect(x + w * t0, y, w * (t1 - t0) + 1, h,
            r1 + (r2 - r1) * t0, g1 + (g2 - g1) * t0, b1 + (b2 - b1) * t0, a1 + (a2 - a1) * t0)
    end
end

-- SteamID64 as a STRING: ids exceed 2^53, so the account id (low 32 bits of m_steamID) is
-- rebuilt through uint64 cdata and formatted with string.format - tostring prints a "ULL"
-- suffix and a double round-trip would corrupt the low digits.
local function to_sid64(account_id)
    if not account_id or account_id <= 0 then
        return nil
    end
    return string.format("%d", ffi.cast("uint64_t", 0x0110000100000000ULL) + ffi.cast("uint64_t", account_id))
end

local function get_avatar_texture(steamid, now)
    if not steamid then
        return nil
    end
    local cached = tex_cache[steamid]
    if cached and cached.tex and renderer.image_ready(cached.tex) then
        return cached.tex
    end
    if cached and cached.done then
        return nil
    end
    -- throttle: persona data lands a few frames after the request
    if cached and now - (cached.lastTry or 0) < 1.0 then
        return nil
    end
    if not cached then
        cached = { lastTry = now }
        tex_cache[steamid] = cached
    end
    cached.lastTry = now
    cached.tries = (cached.tries or 0) + 1
    if cached.tries > 15 then
        cached.done = true -- avatar never arrived; stop retrying
        return nil
    end
    if tex_count >= 8 then
        return nil -- renderer texture slots exhausted (8 per script)
    end
    steam.request_friend_info(steamid)
    local handle = steam.get_friend_avatar(steamid)
    if not handle then
        return nil
    end
    local w, h = steam.avatar_size(handle)
    if not w or not h or w <= 0 or h <= 0 then
        return nil
    end
    local data = steam.avatar_rgba(handle)
    if not data then
        return nil
    end
    local ok, tex = pcall(renderer.load_rgba, w, h, data)
    if not ok or not tex then
        return nil
    end
    cached.tex = tex
    tex_count = tex_count + 1
    return tex
end

client.set_event_callback("paint", function()
    local enabled = gui.get(show)
    local menu_open = client.is_menu_open()
    local now = client.get_time()
    local dt = last_now and clamp(now - last_now, 0, 0.1) or (1 / 60)
    last_now = now

    -- Layout metrics
    local ROW_H = 17
    local HEADER_H = 20
    local BAR_H = 2
    local DRAG_H = 22
    local HEIGHT_OFFSET = 26

    -- Collect spectators
    local specs = {}
    local indices = entity.get_spectators()
    if indices then
        for _, i in ipairs(indices) do
            if i ~= entity.get_local_player() then
                local name = entity.get_prop_string(i, "CBasePlayerController", "m_iszPlayerName")
                if name and name ~= "" and name ~= "Unknown" then
                    specs[#specs + 1] = {
                        name = name,
                        steamid = to_sid64(entity.get_prop(i, "CBasePlayerController", "m_steamID")),
                    }
                end
            end
        end
    end

    local has_items = #specs > 0 or menu_open
    if enabled and has_items then
        m_alpha = math.min(1, m_alpha + 8 * dt)
    else
        m_alpha = math.max(0, m_alpha - 8 * dt)
    end
    if m_alpha <= 0 then
        drag.active = false
        return
    end

    local sw, sh = client.get_screen_size()
    if not sw or sw <= 0 then return end

    -- Layout width calculation
    local maxw = 85
    for _, s in ipairs(specs) do
        local nw = renderer.text_size(s.name)
        if nw > maxw then
            maxw = nw
        end
    end
    local w = 55 + maxw
    local total_h = HEIGHT_OFFSET + (#specs * ROW_H)

    local x = pos_x / RES * sw
    local y = pos_y / RES * sh

    -- Mouse dragging when menu is open
    if menu_open then
        local mx, my = client.get_mouse_pos()
        local pressed = client.is_mouse_down(0)
        if pressed and not drag.active then
            if mx >= x and my >= y and mx <= x + w and my <= y + DRAG_H then
                drag.active = true
                drag.ox = mx - x
                drag.oy = my - y
            end
        elseif not pressed and drag.active then
            drag.active = false
            database.write("pos_x", pos_x)
            database.write("pos_y", pos_y)
        end
        if drag.active then
            pos_x = clamp(mx - drag.ox, 0, sw - w) / sw * RES
            pos_y = clamp(my - drag.oy, 0, sh - total_h) / sh * RES
            x = pos_x / RES * sw
            y = pos_y / RES * sh
        end
    else
        drag.active = false
    end

    local r, g, b, a = get_bar_color()
    local render_alpha = m_alpha * 255
    local bg_alpha = m_alpha * a

    -- Top accent bar
    local pal = gui.get(palette)
    if pal == 1 then
        renderer.filled_rect(x, y, w, BAR_H, r, g, b, render_alpha)
    else
        local halfW = math.floor(w * 0.5)
        local bar_ext = 1
        gradient(x, y, halfW + bar_ext, BAR_H, g, b, r, render_alpha, r, g, b, render_alpha)
        gradient(x + halfW, y, w - halfW, BAR_H, r, g, b, render_alpha, b, r, g, render_alpha)
    end

    -- Header background + title
    renderer.filled_rect(x, y + BAR_H, w, HEADER_H, 17, 17, 17, bg_alpha)
    local head_w, head_h = renderer.text_size("spectators")
    local head_x = math.floor(x + (w - head_w) * 0.5)
    local head_y = math.floor(y + BAR_H + (HEADER_H - head_h) * 0.5)
    renderer.text(head_x, head_y, "spectators", 255, 255, 255, render_alpha)

    -- Spectator entries
    local height_offset = HEIGHT_OFFSET
    local right_offset = (x + w * 0.5) > (sw * 0.5)

    for _, s in ipairs(specs) do
        local text_w, text_h = renderer.text_size(s.name)
        local tex = get_avatar_texture(s.steamid, now)
        local item_y = y + height_offset

        local name_x = x + 5
        if tex then
            if right_offset then
                renderer.image(tex, x + w - text_h - 5, item_y, text_h, text_h, 255, 255, 255, render_alpha)
            else
                renderer.image(tex, x + 5, item_y, text_h, text_h, 255, 255, 255, render_alpha)
                name_x = x + text_h + 10
            end
        end

        renderer.text(math.floor(name_x), math.floor(item_y), s.name, 255, 255, 255, render_alpha)
        height_offset = height_offset + ROW_H
    end
end)