-- BLAME BOT - you die, the team gets an explanation. It is never your fault.
-- Toggles: menu > Scripts > this script's section. No keybind.

local enabled  = gui.checkbox("Enabled")
local callOut  = gui.checkbox("Name the killer")
local cooldown = gui.slider("Cooldown (s)", 0, 30, 1)

local lastBlame = 0.0

local excuses = {
    "my ping just spiked to 400",
    "audio bug, i literally heard nothing",
    "he was behind the wall, ask gotv",
    "my mouse double clicked, rip",
    "that was a luck shot, not going to lie",
    "smoke cleared 1 frame before i peeked, unlucky",
    "i lagged, no cap",
    "my team forgot to trade. classic",
    "server reg, everyone saw it",
    "was checking radar, this game is rigged",
}

local killerCallouts = {
    "%s is on 400 hours of radar",
    "%s, that gun is not for beginners",
    "watch %s, he is scripting for sure",
    "thanks for the free kill %s",
    "%s diff, unfortnate",
}

client.set_event_callback("player_death", function(event)
    if not gui.get(enabled) or not event then return end
    local me = entity.get_local_player()
    if not me or event.userid ~= me - 1 then return end

    local now = client.get_time()
    if now - lastBlame < gui.get(cooldown) then return end
    lastBlame = now

    local line = nil
    local attacker = event.attacker
    if gui.get(callOut) and attacker and attacker < 65535 and attacker ~= event.userid then
        local killer = entity.get_prop_string(attacker + 1, "CCSPlayerController", "m_iszPlayerName")
        if killer then
            line = string.format(killerCallouts[math.random(#killerCallouts)], killer)
        end
    end
    if not line then
        line = excuses[math.random(#excuses)]
    end
    client.exec("say " .. line)
end)
