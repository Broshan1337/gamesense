-- K/D SHAMER - on a timer, mock the teammate with the most deaths this game.
-- Deaths are tracked from player_death events and reset every round_start.
-- Toggles: menu > Scripts > this script's section. No keybind.

local enabled  = gui.checkbox("Enabled")
local interval = gui.slider("Interval (s)", 30, 600, 120)
local minDeaths = gui.slider("Min deaths to shame", 2, 20, 4)

local deaths = {}   -- [controller entity index] = deaths this game
local lastSay = 0.0

local shames = {
    "%s, that is %d deaths. we are paying for your diff",
    "someone check on %s, %d deaths and counting",
    "%s has died %d times. character development?",
    "%s is speedrunning the respawn queue (%d)",
}

client.set_event_callback("player_death", function(event)
    if not event or not event.userid then return end
    local idx = event.userid + 1
    deaths[idx] = (deaths[idx] or 0) + 1
end)

client.set_event_callback("round_start", function()
    deaths = {}
end)

client.set_event_callback("createmove", function()
    if not gui.get(enabled) then return end
    local me = entity.get_local_player()
    if not me then return end
    local now = client.get_time()
    if now - lastSay < gui.get(interval) then return end
    lastSay = now

    local worst, worstCount = nil, 0
    for _, controller in ipairs(entity.get_players() or {}) do
        if controller ~= me then
            local count = deaths[controller] or 0
            if count > worstCount then
                worst, worstCount = controller, count
            end
        end
    end

    if worst and worstCount >= gui.get(minDeaths) then
        local name = entity.get_prop_string(worst, "CCSPlayerController", "m_iszPlayerName")
        if name then
            client.exec("say " .. string.format(shames[math.random(#shames)], name, worstCount))
        end
    end
end)
