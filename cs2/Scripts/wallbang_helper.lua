-- Wallbang Helper (ported from fatality.win "fatality wallbang helper.lua")
--
-- Pairs of waypoints mark wallbang lanes: the ODD waypoint is the spot you shoot
-- from, the following EVEN one is where you aim. When you (or an enemy) stand near
-- one end of a lane, the line to the other end lights up and the dots recolor --
-- green = you triggered it, yellow = an enemy triggered it, red/white = idle.
--
-- Controls live in Scripts > WALLBANG HELPER. Waypoints only draw on the map they
-- were recorded for; add your own with "Add Waypoint" and extend the list below.

local waypoints = {}

-- lane-end colors: [1] idle red, [2] triggered green, [3] pair-triggered yellow
local WB_COLORS = {
    { 255, 0, 0, 255 },
    { 0, 255, 0, 255 },
    { 255, 255, 0, 255 },
}
local WB_GLOW = { 255, 255, 0, 100 }

local TRIGGER_RADIUS = 50.0 -- within this distance a lane end counts as triggered
local RENDER_RADIUS = 300.0 -- lane ends beyond this distance stay hidden

-- Predefined waypoints (one entry per lane end; consecutive pairs belong together)
local predefined = {
    -- de_mirage
    { 703.59, -1603.12, -262.88, "de_mirage" }, { -1039.57, -327.51, -367.97, "de_mirage" },
    { 605.47, -1718.96, -258.09, "de_mirage" }, { -1005.98, -2480.60, -167.97, "de_mirage" },
    { 576.92, -1717.41, -259.04, "de_mirage" }, { 12.23, -2093.41, -39.97, "de_mirage" },
    { 509.55, -1665.90, -263.97, "de_mirage" }, { 459.30, -2343.78, -39.97, "de_mirage" },
    { 444.76, -1710.55, -234.35, "de_mirage" }, { 1039.96, -1909.43, -71.97, "de_mirage" },
    { 430.87, -1523.46, -227.40, "de_mirage" }, { -675.45, -780.14, -262.05, "de_mirage" },
    { 487.58, -1601.25, -255.76, "de_mirage" }, { 527.97, -534.74, -155.97, "de_mirage" },
    { -647.06, -778.04, -261.97, "de_mirage" }, { 419.38, -1522.17, -221.65, "de_mirage" },
    { -628.49, -778.79, -261.97, "de_mirage" }, { -142.97, -1418.03, -72.18, "de_mirage" },
    { -611.44, -767.45, -261.97, "de_mirage" }, { -297.20, -1529.68, -167.97, "de_mirage" },
    { -600.88, -739.22, -262.38, "de_mirage" }, { -391.37, -2031.91, -179.97, "de_mirage" },
    { -152.51, -934.70, -167.55, "de_mirage" }, { -704.82, -814.35, -263.97, "de_mirage" },
    { -710.23, -812.21, -263.97, "de_mirage" }, { -1374.63, -987.34, -167.97, "de_mirage" },
    { -999.98, -307.89, -367.97, "de_mirage" }, { 684.14, -1625.90, -262.55, "de_mirage" },
    { -1070.30, -2468.48, -167.97, "de_mirage" }, { 691.76, -1642.52, -258.56, "de_mirage" },
    { -1711.97, -1023.42, -203.92, "de_mirage" }, { 11.61, -607.98, -189.97, "de_mirage" },
    { -1671.04, 564.31, -167.97, "de_mirage" }, { -1133.83, -786.66, -167.97, "de_mirage" },
    { -1567.95, 526.26, -167.97, "de_mirage" }, { -1567.95, 526.26, -167.97, "de_mirage" },
    { -1054.21, 731.78, -79.97, "de_mirage" }, { -1571.11, 525.77, -167.97, "de_mirage" },
    { -1449.30, 252.92, -166.97, "de_mirage" }, { -1504.24, 750.58, -47.97, "de_mirage" },
    { -1633.51, 115.84, -168.39, "de_mirage" }, { -752.04, -61.73, -161.07, "de_mirage" },
    { 20.35, -2122.48, -39.97, "de_mirage" }, { 667.86, -1601.04, -263.97, "de_mirage" },
    { 151.97, -2071.96, -39.97, "de_mirage" }, { 208.15, -1437.61, -175.97, "de_mirage" },
    { 15.97, -1740.47, -167.97, "de_mirage" }, { 947.48, -2273.43, -39.97, "de_mirage" },
    { -129.66, -2412.97, -163.97, "de_mirage" }, { 468.79, -2337.59, -39.97, "de_mirage" },
    { 735.97, -2390.94, 10.63, "de_mirage" }, { -282.84, -2399.04, -163.97, "de_mirage" },
    { 1179.10, -1479.96, -167.97, "de_mirage" }, { 878.89, -2009.50, -71.97, "de_mirage" },
    { -552.23, -1310.53, -163.97, "de_mirage" }, { -453.46, -1798.52, -175.77, "de_mirage" },
    { -1504.52, -1420.02, -259.97, "de_mirage" }, { -327.19, -2037.79, -175.18, "de_mirage" },
    { -1525.03, -1474.21, -259.97, "de_mirage" }, { -494.80, -702.30, -267.72, "de_mirage" },
    { -1504.39, -1440.34, -259.97, "de_mirage" }, { -1156.04, -1248.18, -167.97, "de_mirage" },
    { -1556.84, -950.87, -191.93, "de_mirage" }, { -1041.40, -300.32, -367.97, "de_mirage" },
    { -1041.40, -300.32, -367.97, "de_mirage" }, { -1572.53, -1607.21, -263.62, "de_mirage" },
    { -1961.60, -472.47, -167.97, "de_mirage" }, { -1006.52, -321.76, -367.97, "de_mirage" },
    { -1128.40, 295.97, -159.97, "de_mirage" }, { -436.49, 662.30, -79.64, "de_mirage" },
    { -1073.82, 297.22, -159.97, "de_mirage" }, { -1012.98, 546.72, -79.97, "de_mirage" },
    { -1839.26, 241.86, -162.15, "de_mirage" }, { -982.12, 327.82, -367.97, "de_mirage" },
    { -913.93, 112.04, -170.46, "de_mirage" }, { -1011.93, -163.11, -348.30, "de_mirage" },
    { -2004.44, 682.37, -46.56, "de_mirage" }, { -1044.00, -333.05, -357.70, "de_mirage" },
    { -1038.34, 360.31, -367.97, "de_mirage" }, { -1932.83, -356.13, -167.97, "de_mirage" },
    { -969.88, -378.17, -346.88, "de_mirage" }, { 187.10, 841.37, -135.97, "de_mirage" },
    { -1017.47, -456.40, -307.77, "de_mirage" }, { -969.66, 240.66, -171.39, "de_mirage" },
    { -710.95, -821.33, -263.97, "de_mirage" }, { -1255.57, -1440.03, -158.01, "de_mirage" },
    -- de_dust2
    { 820.49, 808.03, 47.03, "de_dust2" }, { 311.44, 1786.08, 96.03, "de_dust2" },
    { 915.47, 2412.67, 127.03, "de_dust2" }, { 291.23, 2415.40, -121.09, "de_dust2" },
    { -364.10, 2145.41, -127.84, "de_dust2" }, { 334.00, 1678.27, 43.28, "de_dust2" },
    { 362.84, 1636.50, 21.39, "de_dust2" }, { -166.03, 2172.27, -126.00, "de_dust2" },
    { 1146.69, 2276.10, 9.44, "de_dust2" }, { 1356.49, 2533.70, 67.16, "de_dust2" },
    { 597.71, 457.31, 1.21, "de_dust2" }, { -541.42, 404.95, 5.82, "de_dust2" },
}

-- { x, y, z, map }
for i = 1, #predefined do
    local p = predefined[i]
    waypoints[#waypoints + 1] = { x = p[1], y = p[2], z = p[3], map = p[4] }
end

gui.tab("WALLBANG HELPER")
gui.divider("Manage")
local addWaypoint = gui.checkbox("Add Waypoint [Click]")
local clearWaypoint = gui.checkbox("Clear Last Waypoint")
gui.divider("This Session")
local counter = gui.slider("Waypoints Loaded", 0, 200, #waypoints)

local function dist3(x1, y1, z1, x2, y2, z2)
    local dx, dy, dz = x1 - x2, y1 - y2, z1 - z2
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

-- CS2 sometimes prefixes/suffixes the map name ("maps/de_mirage.vpk") - normalize
-- so the predefined table matches however the engine reports it.
local function normalizeMap(name)
    if not name then return nil end
    name = name:gsub("%.vpk$", "")
    name = name:gsub("^maps/", "")
    name = name:gsub("^workshop/[0-9]+/", "")
    return name
end

local function addWaypointAtMyPosition()
    local controller = entity.get_local_player()
    if not controller then return end
    local pawn = entity.get_player_pawn(controller)
    if not pawn then return end
    local x, y, z = entity.get_origin(pawn)
    if not x then return end
    local map = normalizeMap(client.get_map_name()) or "unknown"
    waypoints[#waypoints + 1] = { x = x, y = y, z = z, map = map }
    client.log(string.format("wallbang helper: added waypoint %d at %.2f, %.2f, %.2f on %s",
        #waypoints, x, y, z, map))
    client.notify(string.format("waypoint %d added", #waypoints), 120, 220, 120)
end

local function clearLastWaypoint()
    if #waypoints <= #predefined then
        client.notify("no custom waypoints to remove", 220, 180, 90)
        return
    end
    local removed = table.remove(waypoints)
    client.log(string.format("wallbang helper: removed waypoint at %.2f, %.2f, %.2f on %s",
        removed.x, removed.y, removed.z, removed.map))
    client.notify("last waypoint removed", 220, 180, 90)
end

-- Any enemy standing within the trigger radius of the waypoint? `players` is the
-- per-frame player list fetched once in the paint callback, `localTeam` the local
-- pawn's team number (nil = unavailable, nothing can be classified then).
local function enemyNear(x, y, z, players, localTeam)
    if not players or not localTeam then return false end
    for i = 1, #players do
        local pawn = entity.get_player_pawn(players[i])
        if pawn then
            local team = entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
            if team and team ~= localTeam then
                local px, py, pz = entity.get_origin(pawn)
                if px and dist3(px, py, pz, x, y, z) < TRIGGER_RADIUS then
                    return true
                end
            end
        end
    end
    return false
end

client.set_event_callback("paint", function()
    -- one-shot checkbox handling (fires once per click, then resets itself)
    if gui.get(addWaypoint) then
        addWaypointAtMyPosition()
        gui.set(addWaypoint, false)
    end
    if gui.get(clearWaypoint) then
        clearLastWaypoint()
        gui.set(clearWaypoint, false)
    end
    gui.set(counter, #waypoints)

    local controller = entity.get_local_player()
    if not controller then return end
    local pawn = entity.get_player_pawn(controller)
    if not pawn then return end
    local lx, ly, lz = entity.get_origin(pawn)
    if not lx then return end
    local map = normalizeMap(client.get_map_name())
    if not map then return end

    -- per-frame context fetched once: local team + the player list the enemy trigger uses
    local localTeam = entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
    local players = entity.get_players()

    local count = #waypoints
    local triggered = {}
    local visible = {}

    -- pass 1: trigger + visibility
    for i = 1, count do
        local wp = waypoints[i]
        if wp.map == map then
            local paired = (i % 2 == 1) and (i + 1) or (i - 1)
            local dist = dist3(lx, ly, lz, wp.x, wp.y, wp.z)
            if dist < TRIGGER_RADIUS or enemyNear(wp.x, wp.y, wp.z, players, localTeam) then
                triggered[i] = true
                if paired >= 1 and paired <= count then
                    visible[paired] = true -- the lane lights up, but only THIS end is triggered
                end
            end
            if dist < RENDER_RADIUS or triggered[i]
                or (paired >= 1 and paired <= count and triggered[paired]) then
                visible[i] = true
            end
        end
    end

    -- pass 2: lane lines for triggered pairs
    for i = 1, count do
        if triggered[i] and waypoints[i].map == map then
            local paired = (i % 2 == 1) and (i + 1) or (i - 1)
            if paired >= 1 and paired <= count and waypoints[paired].map == map then
                local sx, sy = renderer.world_to_screen(waypoints[i].x, waypoints[i].y, waypoints[i].z)
                local ex, ey = renderer.world_to_screen(waypoints[paired].x, waypoints[paired].y, waypoints[paired].z)
                if sx and ex then
                    renderer.line(sx, sy, ex, ey, 255, 255, 255, 200, 1)
                end
            end
        end
    end

    -- pass 3: lane end markers
    for i = 1, count do
        if visible[i] then
            local wp = waypoints[i]
            local sx, sy = renderer.world_to_screen(wp.x, wp.y, wp.z)
            if sx then
                local paired = (i % 2 == 1) and (i + 1) or (i - 1)
                local pairTriggered = paired >= 1 and paired <= count and triggered[paired] == true
                if i % 2 == 1 then
                    -- odd = the shooting spot: filled dot with a glow ring
                    local color = triggered[i] and 2 or (pairTriggered and 3 or 1)
                    renderer.circle_filled(sx, sy, 10, unpack(WB_COLORS[color]))
                    renderer.circle(sx, sy, 12, WB_GLOW[1], WB_GLOW[2], WB_GLOW[3], WB_GLOW[4])
                else
                    -- even = the target spot: green when it itself triggered, yellow when
                    -- only the paired odd end did, white outline while idle
                    local color = triggered[i] and 2 or (pairTriggered and 3 or 0)
                    if color == 0 then
                        renderer.circle(sx, sy, 10, 255, 255, 255, 255)
                    else
                        renderer.circle_filled(sx, sy, 10, unpack(WB_COLORS[color]))
                    end
                end
            end
        end
    end
end)

client.log("script loaded (wallbang helper) - controls in Scripts > WALLBANG HELPER")