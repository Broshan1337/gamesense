-- ns_movement.lua - Neversnooze movement pack (cmd.* bridge; distilled from the HVH lua corpus).
-- Own "NS MOVEMENT" subtab on the Scripts page. Game-thread only ("createmove" callbacks).
--
-- Features:
--   * Twerk (BULLRUSH button kills the duck cooldown + 8-tick duck parity)
--   * Crouch In Air (air-duck every tick while airborne)
--   * Air Strafe Assist (sidemove follows the mouse turn while airborne)
--   * Quick Plant (auto-press attack while planting-capable, in zone, on ground)
--
-- NOTE: the native Movement tab already ships jumpbug / edgejump / fast ladder - this pack only
-- adds what the C++ side does not have.

gui.tab("NS MOVEMENT")

gui.divider("MOVEMENT")

local twerk = gui.checkbox("Twerk On (duck spam)", false)
local twerkSpeed = gui.slider("Twerk Speed", 2, 16, 8)
local airDuck = gui.checkbox("Crouch In Air", false)
local strafeAssist = gui.checkbox("Air Strafe Assist", false)
local assistStrength = gui.slider("Assist Strength", 0.2, 1.0, 1.0)
local quickPlant = gui.checkbox("Quick Bomb Plant", false)

gui.divider("HUD")

local showState = gui.checkbox("Show Active State", true)

local COLORS = {
    white = { 226, 228, 235 },
    faint = { 145, 149, 159 },
    accent = { 150, 127, 238 },
    green = { 96, 210, 130 },
}

local FL_ONGROUND = 1
local tickCount = 0
local lastYaw = nil
local activeText = {}

client.set_event_callback("createmove", function()
    tickCount = tickCount + 1

    local controller = entity.get_local_player()
    local pawn = controller and entity.get_player_pawn(controller)
    if not pawn then return end

    local flags = entity.get_prop(pawn, "C_BaseEntity", "m_fFlags")
    if not flags then return end
    local onGround = (flags % 2) == 1 -- FL_ONGROUND is bit 0

    ---------------------------------------------------------------- twerk
    if gui.get(twerk) then
        -- BULLRUSH defeats the crouch-to-stand cooldown; DUCK follows a tick-parity square wave
        cmd.press_buttons(buttons.bullrush)
        local period = math.max(2, gui.get(twerkSpeed))
        local duckNow = (tickCount % (period * 2)) < period
        cmd.set_buttons(buttons.duck, duckNow)
    end

    ---------------------------------------------------------------- crouch in air
    if gui.get(airDuck) and not onGround then
        cmd.set_buttons(buttons.duck, true)
    end

    ---------------------------------------------------------------- air strafe assist
    if gui.get(strafeAssist) and not onGround then
        local pitch, yaw = cmd.get_view_angles()
        if yaw and lastYaw then
            -- normalize to -180..180 without the angle lib (inline, keeps this script standalone)
            local delta = (yaw - lastYaw) % 360.0
            if delta > 180.0 then delta = delta - 360.0 end
            if math.abs(delta) > 0.05 then
                -- turning right (yaw increasing) wants RIGHT strafe: leftMove negative
                local side = delta > 0 and -1.0 or 1.0
                cmd.set_left_move(side * math.min(1.0, math.abs(delta) * 0.35) * gui.get(assistStrength))
            end
        end
        lastYaw = yaw
    else
        lastYaw = nil
    end

    ---------------------------------------------------------------- quick plant
    if gui.get(quickPlant) then
        local inZone = (entity.get_prop(pawn, "C_CSPlayerPawn", "m_bInBombZone") or 0) % 2 == 1
        if inZone and onGround then
            -- planting = holding attack with the C4 out; the game handles the rest
            cmd.press_buttons(buttons.attack)
        end
    end

    -- remember which assists are active this tick (the HUD itself draws in paint below)
    activeText = {}
    if gui.get(twerk) then activeText[#activeText + 1] = "TWERK" end
    if gui.get(airDuck) and not onGround then activeText[#activeText + 1] = "AIRDUCK" end
    if gui.get(strafeAssist) and not onGround then activeText[#activeText + 1] = "STRAFE" end
    if gui.get(quickPlant) then activeText[#activeText + 1] = "PLANT" end
end)

client.set_event_callback("paint", function()
    -- renderer is paint-only; the createmove callback above only records the state
    local parts = activeText or {}
    local text = table.concat(parts, " ")
    if text ~= "" then
        renderer.text(12, 260, text, COLORS.accent[1], COLORS.accent[2], COLORS.accent[3], 220)
    end
end)

client.notify("ns_movement loaded - see the NS MOVEMENT tab", 150, 127, 238)
