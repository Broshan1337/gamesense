-- FAKE HUD - fake medical meters for discord screenshare. Nothing here does anything to the
-- game; it just draws a pill-cluster overlay that LOOKS like a suspicious cheat HUD.
--
-- Style: stadium pills (ref: HITS/MISS/ACC + AIM/TRIG/BLOCK) drawn with the native
-- renderer.rounded_rect (filled) + centered renderer.text. No imgui.* here on purpose:
-- imgui.* only exists inside the "menu" callback (menu open), while a HUD must draw
-- every frame from "paint" - renderer.* IS the game's ImGui draw list underneath.
--
-- Four meters, all starting GREEN:
--   CHOLESTEROL      climbs with every kill you land ("eating kills"), slow decay
--   BLOOD PRESSURE   spikes when you take damage / drop below 50 HP, fast decay
--   INSULIN          rises the closer a living enemy gets (adrenaline sugar spike)
--   DDOS ON PEEK     red ONLY while the master button is armed
--
-- MASTER KEY: press it and EVERYTHING goes red for N seconds (the "committed" mode), then all
-- meters ease back to their automatic behavior. Toggles: menu > Scripts > this section.

local enabled       = gui.checkbox("Enabled")
local keys          = { "Insert", "Home", "End", "Delete", "Page Up", "Page Down",
                        "M", "N", "B", "H", "K", "L", "P", "Up", "Down", "Left", "Right" }
local keyCodes      = { Insert = 73, Home = 74, End = 77, Delete = 76,
                        ["Page Up"] = 75, ["Page Down"] = 78,
                        M = 16, N = 17, B = 5, H = 11, K = 14, L = 15, P = 19,
                        Up = 82, Down = 81, Left = 80, Right = 79 }
local masterKey     = gui.dropdown("Master Key", keys, 1)
local masterSeconds = gui.slider("Master Duration (s)", 3, 15, 8)
local insulinRange  = gui.slider("Insulin Range (units)", 200, 1500, 700)
local posX          = gui.slider("Box X", 10, 3000, 14)
local posY          = gui.slider("Box Y", 10, 3000, 60)
local boxOpacity    = gui.slider("Box Opacity", 0, 255, 220)

local meters = { pressure = 0.0, cholesterol = 0.0, insulin = 0.0 }
local masterUntil = 0.0
local lastTime = nil

-- native HUD panel language — EXACT tokens from the keybind-list chrome (kSidebarBg,
-- kHairline, kPillBg...): dark translucent box + top-light wash + pulsing accent dot +
-- caption header + opaque hairline, value pills inside.
local BOX_BG     = { 16, 16, 18 }        -- kSidebarBg (alpha = Box Opacity slider)
local BOX_BORDER = { 52, 52, 58 }        -- window border (alpha 220)
local HAIR       = { 26, 26, 30 }        -- kHairline (opaque)
local CHIP_BG    = { 24, 24, 26 }        -- kPillBg
local CHIP_EDGE  = { 32, 32, 36 }        -- kPillBgHover (pill outline)
local TITLE_COL  = { 196, 199, 208 }
local ACCENT     = { 150, 127, 238 }
local GREEN = { 110, 215, 135 }
local RED   = { 238, 90, 75 }
local DIM   = { 100, 112, 102 }  -- DDOS idle: readable but clearly "off"

local TOP_LABELS   = { "CHOLESTEROL", "BLOOD PRESSURE", "INSULIN" }
local HDR_H        = 38
local CHIP_H       = 24
local CHIP_SIZE    = 13
local CHIP_PAD     = 12
local CHIP_GAP     = 6
local BOX_PAD      = 12
local ROW_GAP      = 8
local BOX_RADIUS   = 10

local function textDims(text, size)
    local tw, th = renderer.text_size(text, size)
    return tw or 0, th or 0
end

-- stadium chip with centered label — keybind-list value-pill tokens (kPillBg fill,
-- kPillBgHover outline), slightly brighter than the box so they read as controls
local function pill(x, y, w, h, label, size, cr, cg, cb, ca, bgAlpha)
    local ba = math.min(255, (bgAlpha or 220) + 40)
    local r = h * 0.5
    renderer.rounded_rect(x, y, w, h, r, CHIP_BG[1], CHIP_BG[2], CHIP_BG[3], ba, 1, true)
    renderer.rounded_rect(x + 0.5, y + 0.5, w - 1, h - 1, r - 0.5,
                          CHIP_EDGE[1], CHIP_EDGE[2], CHIP_EDGE[3], 255, 1.0)
    local tw, th = textDims(label, size)
    renderer.text(x + (w - tw) * 0.5, y + (h - th) * 0.5 - 0.5,
                  label, cr, cg, cb, ca, size)
end

-- the native boxed-panel chrome, keybind-list exact: translucent dark box (alpha = opacity
-- slider), top-light wash (white 18 -> 0 over the header band), pulsing accent dot, caption,
-- opaque kHairline divider, then chips.
local function panel(x, y, w, h, title, bgAlpha, now)
    local sc = renderer.scale()
    local hdrH, radius, pad = HDR_H * sc, BOX_RADIUS * sc, BOX_PAD * sc
    renderer.rounded_rect(x, y, w, h, radius, BOX_BG[1], BOX_BG[2], BOX_BG[3], bgAlpha, 1, true)
    renderer.rounded_rect(x, y, w, h, radius, BOX_BORDER[1], BOX_BORDER[2], BOX_BORDER[3], 220, 1.4)
    -- top-light: the keybind list's white-18 wash fading down the header band
    renderer.gradient_rect(x + pad, y + 1, w - pad * 2, 40 * sc,
                           255, 255, 255, 18, 255, 255, 255, 0, true)
    -- 1px inner highlight along the top edge
    renderer.rounded_rect(x + pad, y + 1, w - pad * 2, 1, 0.5, 255, 255, 255, 22, 1.4)
    local pulse = 0.55 + 0.45 * math.abs(math.sin(now * 2.5))
    renderer.circle_filled(x + 15 * sc, y + 17 * sc, 2.4 * sc,
                           ACCENT[1], ACCENT[2], ACCENT[3], math.floor(255 * pulse))
    renderer.text(x + 23 * sc, y + 10 * sc, title, 214, 217, 226, 255, 12 * sc, false, true)
    renderer.rounded_rect(x + pad, y + hdrH - 2 * sc, w - pad * 2, 1, 0.5,
                          HAIR[1], HAIR[2], HAIR[3], 255, 1.4)
end

-- 2D distance to the nearest LIVING enemy (controller index -> pawn -> origin), nil = unknown
local function nearestEnemyDistance()
    local me = entity.get_local_player()
    if not me then return nil end
    local myPawn = entity.get_player_pawn(me)
    if not myPawn then return nil end
    local mx, my = entity.get_prop_vector(myPawn, "C_BaseEntity", "m_vecAbsOrigin")
    if not mx then return nil end
    local myTeam = entity.get_prop(myPawn, "C_BaseEntity", "m_iTeamNum")
    if not myTeam then return nil end
    local best = math.huge
    for _, idx in ipairs(entity.get_players()) do
        if idx ~= me then
            local pawn = entity.get_player_pawn(idx)
            if pawn then
                local team = entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
                local hp = entity.get_prop(pawn, "C_BaseEntity", "m_iHealth")
                if team and hp and hp > 0 and team ~= myTeam then
                    local x, y = entity.get_prop_vector(pawn, "C_BaseEntity", "m_vecAbsOrigin")
                    if x then
                        local dx, dy = x - mx, y - my
                        local d = math.sqrt(dx * dx + dy * dy)
                        if d < best then best = d end
                    end
                end
            end
        end
    end
    if best == math.huge then return nil end
    return best
end

client.set_event_callback("player_hurt", function(event)
    if not gui.get(enabled) or not event then return end
    local me = entity.get_local_player()
    if not me then return end
    -- event.userid is a 0-based player slot; controller entity index = slot + 1
    if event.userid == me - 1 then
        meters.pressure = 1.0   -- we got hit: pressure spike
    end
end)

client.set_event_callback("player_death", function(event)
    if not gui.get(enabled) or not event then return end
    local me = entity.get_local_player()
    if not me then return end
    if event.attacker and event.attacker < 65535 and event.attacker == me - 1 then
        meters.cholesterol = math.min(1.0, meters.cholesterol + 0.45)   -- eating kills
    end
end)

client.set_event_callback("paint", function()
    if not gui.get(enabled) then return end
    local now = client.get_time()
    local dt = lastTime and math.min(0.1, now - lastTime) or 0.016
    lastTime = now

    -- decay: pressure recovers fast, cholesterol lingers (it is a lifestyle, not a phase)
    meters.pressure = math.max(0.0, meters.pressure - 0.30 * dt)
    meters.cholesterol = math.max(0.0, meters.cholesterol - 0.10 * dt)

    -- master button: everything red while armed
    local keyName = keys[gui.get(masterKey)]
    if keyName and client.is_key_down(keyCodes[keyName] or 73) then
        masterUntil = now + gui.get(masterSeconds)
    end
    local masterActive = now < masterUntil

    -- insulin: adrenaline sugar spike, driven by the nearest living enemy
    local dist = nearestEnemyDistance()
    if dist and dist < gui.get(insulinRange) then
        meters.insulin = math.max(meters.insulin, math.min(1.0, 1.0 - dist / gui.get(insulinRange)))
    else
        meters.insulin = math.max(0.0, meters.insulin - 0.25 * dt)
    end

    -- low health keeps the pressure up
    local me = entity.get_local_player()
    if me then
        local pawn = entity.get_player_pawn(me)
        local hp = pawn and entity.get_prop(pawn, "C_BaseEntity", "m_iHealth") or 100
        if hp > 0 and hp < 50 then
            meters.pressure = math.max(meters.pressure, 0.8)
        end
    end

    -- the whole cluster now lives inside ONE native-style boxed panel: VITALS header,
    -- meter chips row, DDOS chip row (same chrome as the C++ COMBAT/STATUS boxes).
    local values = {
        math.max(meters.cholesterol, masterActive and 1.0 or 0.0),
        math.max(meters.pressure, masterActive and 1.0 or 0.0),
        math.max(meters.insulin, masterActive and 1.0 or 0.0),
    }

    local x, y = gui.get(posX) or 14, gui.get(posY) or 60
    local bgAlpha = gui.get(boxOpacity) or 220 -- gui.get can hand back nil for untouched sliders

    local widths = {}
    local totalChips = 0
    for i, label in ipairs(TOP_LABELS) do
        local tw = textDims(label, CHIP_SIZE)
        widths[i] = tw + CHIP_PAD * 2
        totalChips = totalChips + widths[i]
    end
    totalChips = totalChips + CHIP_GAP * (#TOP_LABELS - 1)


    local boxW = totalChips + BOX_PAD * 2
    local boxH = HDR_H + BOX_PAD + CHIP_H + ROW_GAP + CHIP_H + BOX_PAD

    panel(x, y, boxW, boxH, "VITAL O METER", bgAlpha, now)

    local cx = x + BOX_PAD
    local chipY = y + HDR_H + BOX_PAD
    for i, label in ipairs(TOP_LABELS) do
        local hot = values[i] >= 0.6
        local c = hot and RED or GREEN
        pill(cx, chipY, widths[i], CHIP_H, label, CHIP_SIZE, c[1], c[2], c[3], 255, bgAlpha)
        cx = cx + widths[i] + CHIP_GAP
    end

    local dc = masterActive and RED or DIM
    local da = masterActive and 255 or 170
    pill(x + BOX_PAD, chipY + CHIP_H + ROW_GAP, boxW - BOX_PAD * 2, CHIP_H, "DDOS ON PEEK", CHIP_SIZE,
         dc[1], dc[2], dc[3], da, bgAlpha)
end)
