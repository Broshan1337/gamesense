-- F BOT - a teammate dies and you pay your respects. Loudly. Several times.
-- Toggles: menu > Scripts > this script's section. No keybind.

local enabled = gui.checkbox("Enabled")
local burst   = gui.slider("Burst count", 1, 10, 5)
local gap     = gui.slider("Gap (x0.1s)", 2, 20, 4) -- sliders are integer-only

local pending = 0
local nextSend = 0.0

local function teamOf(controller)
    local pawn = entity.get_player_pawn(controller)
    if not pawn then return nil end
    return entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
end

client.set_event_callback("player_death", function(event)
    if not gui.get(enabled) or not event then return end
    local me = entity.get_local_player()
    if not me then return end
    if event.userid == me - 1 then return end          -- my own death: other scripts cover that

    local victimTeam, myTeam = teamOf(event.userid + 1), teamOf(me)
    if not victimTeam or not myTeam or victimTeam ~= myTeam then return end

    pending = gui.get(burst)
    nextSend = client.get_time() + 0.4                 -- small delay so it reads like a reaction
end)

client.set_event_callback("createmove", function()
    if not gui.get(enabled) or pending <= 0 then return end
    local now = client.get_time()
    if now < nextSend then return end
    client.exec("say F")
    pending = pending - 1
    nextSend = now + gui.get(gap) * 0.1                -- pacing keeps the server flood limiter calm
end)
