-- CLUTCH ORACLE - when you are the last one alive against 2+, the screen gets a banner
-- and the team gets a motivational speech. Resets every round.
-- Toggles: menu > Scripts > this script's section. No keybind.

local enabled = gui.checkbox("Enabled")
local banter  = gui.checkbox("Chat banter on clutch start")

local clutchEnemies = 0     -- set by createmove, drawn by paint
local saidThisClutch = false

local pepTalks = {
    "it is just me. that is fine.",
    "everyone go afk, i got this",
    "clutch or kick me, no in between",
    "1 versus the whole lobby, business as usual",
    "watch and learn",
}

local function countAlive(team)
    local alive, enemies = 0, 0
    for _, controller in ipairs(entity.get_players() or {}) do
        local pawn = entity.get_player_pawn(controller)
        if pawn then
            local t = entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
            local hp = entity.get_prop(pawn, "C_BaseEntity", "m_iHealth")
            if t and hp and hp > 0 then
                if t == team then
                    alive = alive + 1
                else
                    enemies = enemies + 1
                end
            end
        end
    end
    return alive, enemies
end

client.set_event_callback("round_start", function()
    saidThisClutch = false
end)

client.set_event_callback("createmove", function()
    clutchEnemies = 0
    if not gui.get(enabled) then return end
    local me = entity.get_local_player()
    if not me then return end
    local pawn = entity.get_player_pawn(me)
    if not pawn then return end
    local myTeam = entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
    local myHp = entity.get_prop(pawn, "C_BaseEntity", "m_iHealth")
    if not myTeam or not myHp or myHp <= 0 then return end

    local alive, enemies = countAlive(myTeam)
    if alive == 1 and enemies >= 2 then
        clutchEnemies = enemies
        if not saidThisClutch then
            saidThisClutch = true
            if gui.get(banter) then
                client.exec("say " .. pepTalks[math.random(#pepTalks)])
            end
        end
    end
end)

client.set_event_callback("paint", function()
    if clutchEnemies < 2 then return end
    local w, h = client.get_screen_size()
    local text = "1v" .. clutchEnemies .. " - CLOWN OR KICK"
    local tw, th = renderer.text_size(text)
    local x, y = (w - tw) * 0.5, h * 0.72
    renderer.filled_rect(x - 8, y - 6, tw + 16, th + 12, 15, 15, 20, 190)
    renderer.rect(x - 8, y - 6, tw + 16, th + 12, 150, 127, 238, 255, 1.5)
    renderer.text(x, y, text, 255, 240, 120, 255)
end)
