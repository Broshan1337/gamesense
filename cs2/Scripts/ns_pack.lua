-- ns_pack.lua - Neversnooze visual + QOL pack (distilled from the HVH lua corpus survey).
-- Lives in its own "NS PACK" subtab on the Scripts page (gui.tab). One script, many features,
-- no resolver/AA/rage logic - those stay native.
--
-- Uses: entity.get_all/get_prop (schema), world_to_screen, numeric events, toasts, play_sound,
-- database persistence.

gui.tab("NS PACK")

--------------------------------------------------------------------- helpers
local COLORS = {
    white  = { 226, 228, 235 },
    faint  = { 145, 149, 159 },
    red    = { 232, 96, 96 },
    green  = { 96, 210, 130 },
    yellow = { 230, 200, 90 },
    accent = { 150, 127, 238 },
}

-- 0-based event player slots (userid/attacker, 65535 = nobody) -> controller entity index
local function controllerOfSlot(slot)
    if not slot or slot >= 65535 or slot < 0 then return nil end
    return slot + 1
end

-- CHandle prop value (int32) -> entity index
local function handleIndex(handle)
    if not handle or handle <= 0 then return nil end
    local index = handle % 32768
    if index <= 0 then return nil end
    return index
end

local function pawnOf(controllerIndex)
    return entity.get_player_pawn(controllerIndex)
end

local function myPawn()
    local c = entity.get_local_player()
    if not c then return nil end
    return entity.get_player_pawn(c)
end

local function originOf(index)
    -- m_vecAbsOrigin lives on CGameSceneNode behind a pointer - the framework bridge is the
    -- only reliable path (entity.get_prop on C_BaseEntity has no plain origin field).
    return entity.get_origin(index)
end

local function teamOf(index)
    local team = entity.get_prop(index, "C_BaseEntity", "m_iTeamNum")
    if not team then return -1 end
    return team % 256
end

local function nameOf(controllerIndex)
    if not controllerIndex then return "?" end
    return entity.get_prop_string(controllerIndex, "CBasePlayerController", "m_iszPlayerName") or "?"
end

local function hsv(h, s, v)
    h = h % 360.0
    local c = v * s
    local x = c * (1.0 - math.abs((h / 60.0) % 2.0 - 1.0))
    local m = v - c
    local r, g, b
    if h < 60 then r, g, b = c, x, 0
    elseif h < 120 then r, g, b = x, c, 0
    elseif h < 180 then r, g, b = 0, c, x
    elseif h < 240 then r, g, b = 0, x, c
    elseif h < 300 then r, g, b = x, 0, c
    else r, g, b = c, 0, x end
    return math.floor((r + m) * 255), math.floor((g + m) * 255), math.floor((b + m) * 255)
end

--------------------------------------------------------------------- gui
gui.divider("INDICATORS")

local hitrateEnabled = gui.checkbox("Hit + Kill Counter", true)
local hudStrip = gui.checkbox("FPS + Speed Strip", true)
local velocityGraph = gui.checkbox("Velocity Graph", true)
local damageFloats = gui.checkbox("Damage Numbers", true)
local shotMarkers = gui.checkbox("Shot Markers", false)

gui.divider("BOMB")

local bombEsp = gui.checkbox("Bomb Timer ESP", true)
local defuseBar = gui.checkbox("Defuse Bar + Site", true)

gui.divider("WARNINGS")

local zeusWarning = gui.checkbox("Zeus Warning", true)
local lowAmmo = gui.checkbox("Low Ammo Warning", true)
local knifeAlarm = gui.checkbox("Anti-Knife Beep", false)

gui.divider("QOL")

local autoRs = gui.checkbox("Auto !rs On Death", false)
local autoBackup = gui.checkbox("Team Radio On Hurt", false)
local buybot = gui.dropdown("Buybot", { "Off", "AWP", "Rifle", "Scout", "Eco" }, 0)
local hitSound = gui.checkbox("Hit Sound", false)

gui.divider("FUN")

local chinaHat = gui.checkbox("China Hat", false)
local hatEveryone = gui.checkbox("Hat On Everyone", true)
local hatRadius = gui.slider("Hat Size", 10, 80, 34)
local hatHeight = gui.slider("Hat Height", 0, 120, 52)
local hatSpeed = gui.slider("Hat Pulse", 1, 10, 4)
local trails = gui.checkbox("Player Trails", false)
local matrixRain = gui.checkbox("Matrix Rain", false)
local rainAlpha = gui.slider("Rain Alpha", 50, 255, 180)

-- persistent counters
local hits = database.read("pack_hits") or 0
local kills = database.read("pack_kills") or 0

--------------------------------------------------------------------- state
local lastPaintTime = nil
local fpsEma = 144.0
local speed = 0.0
local velocityHistory = {}
local trailHistory = {}
local shotImpacts = {}
local damageEvents = {}
local lastBeep = 0.0
local lastHitSound = 0.0
local rainDrops = nil
local rainNeedsInit = true
local roundPhaseToastAt = 0.0

-- draggable velocity graph (position persists across sessions)
local graphX = database.read("pack_graph_x")
local graphY = database.read("pack_graph_y")
graphX = tonumber(graphX) or nil
graphY = tonumber(graphY) or nil
local graphDragging = false
local graphDragOffX = 0.0
local graphDragOffY = 0.0

local function pushShotMarker(x, y, z)
    shotImpacts[#shotImpacts + 1] = { x = x, y = y, z = z, when = client.get_time(), hit = false }
    if #shotImpacts > 24 then table.remove(shotImpacts, 1) end
end

--------------------------------------------------------------------- events
client.set_event_callback("player_hurt", function(e)
    if not e then return end
    local attacker = controllerOfSlot(e.attacker)
    local me = entity.get_local_player()
    if not me then return end
    if attacker == me then
        hits = hits + 1
        database.write("pack_hits", hits)
        -- hit on our latest shot marker?
        local now = client.get_time()
        for i = #shotImpacts, 1, -1 do
            if now - shotImpacts[i].when < 0.3 then
                shotImpacts[i].hit = true
                break
            end
        end
        if gui.get(hitSound) and now - lastHitSound > 0.15 then
            lastHitSound = now
            client.play_sound("buttons/bell1.wav")
        end
        -- floating damage number on the victim
        local victim = controllerOfSlot(e.userid)
        if victim and e.dmg_health and e.dmg_health > 0 then
            local pawn = entity.get_player_pawn(victim)
            if pawn then
                damageEvents[#damageEvents + 1] = {
                    pawn = pawn, dmg = e.dmg_health, when = now,
                    headshot = (e.hitgroup or 0) == 1,
                }
                if #damageEvents > 16 then table.remove(damageEvents, 1) end
            end
        end
    end
end)

client.set_event_callback("player_death", function(e)
    if not e then return end
    local me = entity.get_local_player()
    if not me then return end
    local attacker = controllerOfSlot(e.attacker)
    if attacker == me then
        kills = kills + 1
        database.write("pack_kills", kills)
    end
    local victim = controllerOfSlot(e.userid)
    if victim == me and gui.get(autoRs) then
        client.exec("say !rs")
    end
end)

client.set_event_callback("bullet_impact", function(e)
    if not e or not e.x then return end
    local me = entity.get_local_player()
    if me and controllerOfSlot(e.userid) == me and gui.get(shotMarkers) then
        pushShotMarker(e.x, e.y, e.z)
    end
end)

client.set_event_callback("bomb_planted", function()
    client.notify("Bomb planted", 230, 200, 90)
end)

client.set_event_callback("bomb_defused", function()
    client.notify("Bomb defused", 96, 210, 130)
end)

client.set_event_callback("round_start", function()
    local mode = gui.get(buybot)
    if mode == 0 then return end
    local buys = {
        [1] = "buy awp;buy vesthelm;buy flashbang;buy smokegrenade;buy molotov",
        [2] = "buy ak47;buy m4a1;buy vesthelm;buy flashbang;buy smokegrenade;buy molotov;buy hegrenade",
        [3] = "buy ssg08;buy vest;buy flashbang",
        [4] = "buy vest;buy flashbang",
    }
    if buys[mode] then client.exec(buys[mode]) end
end)

--------------------------------------------------------------------- paint
client.set_event_callback("paint", function()
    local now = client.get_time()
    local w, h = client.get_screen_size()

    -- fps EMA from paint deltas
    if lastPaintTime then
        local dt = now - lastPaintTime
        if dt > 0.0005 then
            local instant = 1.0 / dt
            fpsEma = fpsEma * 0.95 + instant * 0.05
        end
    end
    lastPaintTime = now

    ---------------------------------------------------------------- HUD strip
    if gui.get(hudStrip) then
        speed = 0.0
        local pawn = myPawn()
        if pawn then
            local vx, vy = entity.get_prop_vector(pawn, "C_BaseEntity", "m_vecAbsVelocity")
            if vx then speed = math.sqrt(vx * vx + vy * vy) end
        end
        local text = string.format("fps %d   speed %d", math.floor(fpsEma + 0.5), math.floor(speed + 0.5))
        renderer.text(w - 160, h - 32, text, COLORS.faint[1], COLORS.faint[2], COLORS.faint[3], 220)
    end

    ---------------------------------------------------------------- velocity graph (draggable)
    if gui.get(velocityGraph) then
        local pawn = myPawn()
        if pawn then
            local vx, vy = entity.get_prop_vector(pawn, "C_BaseEntity", "m_vecAbsVelocity")
            if vx then
                velocityHistory[#velocityHistory + 1] = math.sqrt(vx * vx + vy * vy)
                if #velocityHistory > 64 then table.remove(velocityHistory, 1) end
            end
        end
        local gw, gh = 220, 44
        if not graphX then graphX, graphY = w * 0.5 - gw * 0.5, h - 116 end
        local gx, gy = graphX, graphY
        -- drag: hold anywhere on the graph body, position follows the cursor, saved on release
        local mx, my = client.get_mouse_pos()
        local overBody = mx >= gx - 6 and mx <= gx + gw + 6 and my >= gy - 24 and my <= gy + gh + 6
        local mouseDown = client.is_mouse_down()
        if mouseDown and overBody and not graphDragging then
            graphDragging = true
            graphDragOffX = gx - mx
            graphDragOffY = gy - my
        elseif not mouseDown and graphDragging then
            graphDragging = false
            database.write("pack_graph_x", math.floor(graphX + 0.5))
            database.write("pack_graph_y", math.floor(graphY + 0.5))
        end
        if graphDragging then
            graphX = mx + (graphDragOffX or 0)
            graphY = my + (graphDragOffY or 0)
        end
        local gx, gy = graphX, graphY
        renderer.filled_rect(gx - 6, gy - 6, gw + 12, gh + 12, 16, 16, 18, 150)
        local count = #velocityHistory
        for i = 2, count do
            local x0 = gx + ((i - 2) / 63.0) * gw
            local x1 = gx + ((i - 1) / 63.0) * gw
            local y0 = gy + gh - math.min(gh, velocityHistory[i - 1] / 350.0 * gh)
            local y1 = gy + gh - math.min(gh, velocityHistory[i] / 350.0 * gh)
            renderer.line(x0, y0, x1, y1, COLORS.accent[1], COLORS.accent[2], COLORS.accent[3], 200)
        end
        if count > 0 then
            renderer.text(gx + 4, gy - 22, string.format("%d u/s", math.floor(velocityHistory[count] + 0.5)), COLORS.white[1], COLORS.white[2], COLORS.white[3], 220)
        end
    end

    ---------------------------------------------------------------- hit + kill counters
    if gui.get(hitrateEnabled) then
        renderer.text(12, h - 48, string.format("hits %d   kills %d", hits, kills), COLORS.faint[1], COLORS.faint[2], COLORS.faint[3], 220)
    end

    ---------------------------------------------------------------- shot markers
    if gui.get(shotMarkers) then
        for i = #shotImpacts, 1, -1 do
            local marker = shotImpacts[i]
            local age = now - marker.when
            if age > 2.0 then
                table.remove(shotImpacts, i)
            else
                local sx, sy = renderer.world_to_screen(marker.x, marker.y, marker.z)
                if sx then
                    local alpha = math.floor(255 * (1.0 - age / 2.0))
                    local color = marker.hit and COLORS.red or COLORS.yellow
                    local d = 5.0
                    renderer.line(sx - d, sy - d, sx + d, sy + d, color[1], color[2], color[3], alpha)
                    renderer.line(sx - d, sy + d, sx + d, sy - d, color[1], color[2], color[3], alpha)
                end
            end
        end
    end

    ---------------------------------------------------------------- damage floats
    if gui.get(damageFloats) then
        for i = #damageEvents, 1, -1 do
            local ev = damageEvents[i]
            local age = now - ev.when
            if age > 1.0 then
                table.remove(damageEvents, i)
            else
                local ox, oy, oz = originOf(ev.pawn)
                if ox then
                    local sx, sy = renderer.world_to_screen(ox, oy, oz + 72.0 + age * 46.0)
                    if sx then
                        local alpha = math.floor(255 * (1.0 - age))
                        local color = ev.headshot and COLORS.red or COLORS.white
                        renderer.text(sx, sy, tostring(ev.dmg), color[1], color[2], color[3], alpha)
                    end
                end
            end
        end
    end

    ---------------------------------------------------------------- bomb ESP
    local bombIndex = nil
    local bombList = entity.get_all("C_PlantedC4")
    if bombList then bombIndex = bombList[1] end

    if bombIndex and gui.get(bombEsp) then
        local blow = entity.get_prop_float(bombIndex, "C_PlantedC4", "m_flC4Blow")
        local ox, oy, oz = originOf(bombIndex)
        if blow and ox then
            local remaining = blow - now
            if remaining > 0 then
                local sx, sy = renderer.world_to_screen(ox, oy, oz + 14.0)
                if sx then
                    local color = remaining < 10 and COLORS.red or (remaining < 20 and COLORS.yellow or COLORS.green)
                    -- countdown ring: an arc-ish fan of dots, proportional to remaining time
                    local segments = 26
                    local lit = math.ceil((math.min(1.0, remaining / 40.0)) * segments)
                    for s = 0, segments - 1 do
                        local a = (s / segments) * math.pi * 2.0 - math.pi * 0.5
                        local px = sx + math.cos(a) * 30.0
                        local py = sy + math.sin(a) * 30.0
                        if s < lit then
                            renderer.circle_filled(px, py, 2.0, color[1], color[2], color[3], 230)
                        else
                            renderer.circle_filled(px, py, 1.2, 70, 74, 84, 140)
                        end
                    end
                    renderer.circle_filled(sx, sy, 21.0, 16, 16, 18, 170)
                    renderer.text(sx - 6, sy - 8, tostring(math.ceil(remaining)), 236, 238, 245, 235)
                end
            end
        end
    end

    ---------------------------------------------------------------- defuse bar + site
    if bombIndex and gui.get(defuseBar) then
        local countdown = entity.get_prop_float(bombIndex, "C_PlantedC4", "m_flDefuseCountDown")
        local length = entity.get_prop_float(bombIndex, "C_PlantedC4", "m_flDefuseLength")
        if countdown and length and countdown > 0 then
            local frac = 1.0 - countdown / length
            local barW, barH = 240, 10
            local bx, by = w * 0.5 - barW * 0.5, h - 168
            local color = countdown > 5 and COLORS.green or COLORS.red
            renderer.filled_rect(bx - 2, by - 2, barW + 4, barH + 4, 16, 16, 18, 190)
            renderer.filled_rect(bx, by, barW * math.min(1.0, frac), barH, color[1], color[2], color[3], 230)
            local defuserHandle = entity.get_prop(bombIndex, "C_PlantedC4", "m_hBombDefuser")
            local defuserPawn = handleIndex(defuserHandle)
            if defuserPawn then
                local controllerHandle = entity.get_prop(defuserPawn, "C_BasePlayerPawn", "m_hController")
                local controller = handleIndex(controllerHandle)
                renderer.text(bx, by - 20, nameOf(controller) .. " is defusing!", 236, 238, 245, 230)
            end
        end
        -- bombsite letter from the site centers on the player resource
        local resList = entity.get_all("C_CSPlayerResource")
        local res = resList and resList[1]
        local bx, by, bz = originOf(bombIndex)
        if res and bx then
            local ax, ay, az = entity.get_prop_vector(res, "C_CSPlayerResource", "m_bombsiteCenterA")
            local bxx, byy, bzz = entity.get_prop_vector(res, "C_CSPlayerResource", "m_bombsiteCenterB")
            if ax or bxx then
                local dA = ax and math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2) or 1e9
                local dB = bxx and math.sqrt((bxx - bx) ^ 2 + (byy - by) ^ 2) or 1e9
                renderer.text(w * 0.5 + 78, h - 168, dA < dB and "SITE A" or "SITE B", COLORS.faint[1], COLORS.faint[2], COLORS.faint[3], 220)
            end
        end
    end

    ---------------------------------------------------------------- zeus warning
    if gui.get(zeusWarning) then
        local list = entity.get_all("C_WeaponTaser")
        if list then
            local me = entity.get_local_player()
            local myPawnIndex = me and entity.get_player_pawn(me)
            if myPawnIndex then
                local mx, myy, mz = originOf(myPawnIndex)
                if mx then
                    for _, taserIndex in ipairs(list) do
                        local owner = handleIndex(entity.get_prop(taserIndex, "C_BaseEntity", "m_hOwnerEntity"))
                        if owner and teamOf(owner) ~= teamOf(myPawnIndex) then
                            local ox, oy, oz = originOf(taserIndex)
                            if ox then
                                local dist = math.sqrt((ox - mx) ^ 2 + (oy - myy) ^ 2 + (oz - mz) ^ 2)
                                if dist < 300.0 then
                                    local alpha = dist < 160.0 and 255 or 160
                                    local feet = math.floor(dist * 0.0328084 + 0.5)
                                    renderer.text(w * 0.5 - 46, h * 0.5 - 96, "ZEUS " .. feet .. "ft", COLORS.red[1], COLORS.red[2], COLORS.red[3], alpha)
                                    break
                                end
                            end
                        end
                    end
                end
            end
        end
    end

    ---------------------------------------------------------------- low ammo
    if gui.get(lowAmmo) then
        local pawn = myPawn()
        if pawn then
            local active = handleIndex(entity.get_prop(pawn, "C_BasePlayerPawn", "m_hActiveWeapon"))
            if active then
                local clip = entity.get_prop(active, "C_BasePlayerWeapon", "m_iClip1")
                if clip and clip <= 3 then
                    local color = clip == 0 and COLORS.red or COLORS.yellow
                    local text = clip == 0 and "RELOAD" or ("LOW AMMO (" .. clip .. ")")
                    local _, tw = renderer.text_size(text)
                    renderer.text(w * 0.5 - tw * 0.5, h * 0.5 + 62, text, color[1], color[2], color[3], 230)
                end
            end
        end
    end

    ---------------------------------------------------------------- knife alarm
    if gui.get(knifeAlarm) then
        local list = entity.get_all("C_Knife")
        if list then
            local me = entity.get_local_player()
            local myPawnIndex = me and entity.get_player_pawn(me)
            local mx, myy, mz = myPawnIndex and originOf(myPawnIndex) or nil
            if mx then
                for _, knifeIndex in ipairs(list) do
                    local owner = handleIndex(entity.get_prop(knifeIndex, "C_BaseEntity", "m_hOwnerEntity"))
                    if owner and teamOf(owner) ~= teamOf(myPawnIndex) then
                        local ox, oy, oz = originOf(knifeIndex)
                        if ox then
                            local dist = math.sqrt((ox - mx) ^ 2 + (oy - myy) ^ 2 + (oz - mz) ^ 2)
                            if dist < 400.0 and now - lastBeep > 0.6 then
                                lastBeep = now
                                client.play_sound("ui/beepclear.wav")
                            end
                            break
                        end
                    end
                end
            end
        end
    end

    ---------------------------------------------------------------- china hat (on the head)
    if gui.get(chinaHat) then
        local targets = {}
        if gui.get(hatEveryone) then
            local players = entity.get_players()
            if players then
                for _, controllerIndex in ipairs(players) do
                    local pawn = entity.get_player_pawn(controllerIndex)
                    if pawn then
                        local health = entity.get_prop(pawn, "C_BaseEntity", "m_iHealth")
                        if health and health > 0 then
                            targets[#targets + 1] = pawn
                        end
                    end
                end
            end
        else
            local pawn = myPawn()
            if pawn then targets[#targets + 1] = pawn end
        end
        for _, pawnIndex in ipairs(targets) do
            local ox, oy, oz = originOf(pawnIndex)
            if ox then
                -- head sits roughly 64u above the origin (standing); the cone sits ON the head
                local headZ = oz + 64.0
                local radius = gui.get(hatRadius)
                local height = gui.get(hatHeight)
                local segments = 20
                local pulse = 0.6 + 0.4 * math.sin(now * gui.get(hatSpeed))
                local alpha = math.floor(200 * pulse)
                local apexX, apexY = renderer.world_to_screen(ox, oy, headZ + height)
                if apexX then
                    for i = 0, segments - 1 do
                        local a0 = i / segments * math.pi * 2.0
                        local a1 = (i + 1) / segments * math.pi * 2.0
                        local x0, y0 = renderer.world_to_screen(ox + math.cos(a0) * radius, oy + math.sin(a0) * radius, headZ)
                        local x1, y1 = renderer.world_to_screen(ox + math.cos(a1) * radius, oy + math.sin(a1) * radius, headZ)
                        if x0 and x1 then
                            renderer.line(x0, y0, apexX, apexY, COLORS.accent[1], COLORS.accent[2], COLORS.accent[3], alpha)
                            renderer.line(x0, y0, x1, y1, COLORS.accent[1], COLORS.accent[2], COLORS.accent[3], alpha)
                        end
                    end
                end
            end
        end
    end

    ---------------------------------------------------------------- trails
    if gui.get(trails) then
        local pawn = myPawn()
        if pawn then
            local ox, oy, oz = originOf(pawn)
            if ox then
                local last = trailHistory[#trailHistory]
                if not last or math.sqrt((ox - last.x) ^ 2 + (oy - last.y) ^ 2) > 24.0 then
                    trailHistory[#trailHistory + 1] = { x = ox, y = oy, z = oz, when = now }
                    if #trailHistory > 32 then table.remove(trailHistory, 1) end
                end
                for i = 2, #trailHistory do
                    local a, b = trailHistory[i - 1], trailHistory[i]
                    local age = now - b.when
                    if age < 3.0 then
                        local x0, y0 = renderer.world_to_screen(a.x, a.y, a.z)
                        local x1, y1 = renderer.world_to_screen(b.x, b.y, b.z)
                        if x0 and x1 then
                            local r, g, bl = hsv((now * 60.0 + i * 18.0) % 360.0, 0.8, 1.0)
                            renderer.line(x0, y0, x1, y1, r, g, bl, math.floor(220 * (1.0 - age / 3.0)))
                        end
                    end
                end
            end
        end
    end

    ---------------------------------------------------------------- matrix rain
    if gui.get(matrixRain) then
        if rainNeedsInit then
            rainNeedsInit = false
            rainDrops = {}
            math.randomseed(math.floor(now * 1000) % 100000)
            for i = 1, 42 do
                rainDrops[i] = { x = math.random(0, w), y = math.random(-h, 0), speed = 120 + math.random(0, 260) }
            end
        end
        local rainA = gui.get(rainAlpha)
        for _, drop in ipairs(rainDrops) do
            renderer.text(drop.x, drop.y, "\xEF\x81\x8A", 150, 127, 238, rainA)
            drop.y = drop.y + drop.speed * 0.016
            if drop.y > h + 40 then
                drop.y = -40
                drop.x = math.random(0, w)
            end
        end
    else
        rainNeedsInit = true
    end
end)

client.notify("ns_pack loaded - see the NS PACK tab", 150, 127, 238)
