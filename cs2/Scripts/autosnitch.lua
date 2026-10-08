-- AUTOSNITCH - a teammate damages you and the whole team hears about it, instantly.
-- Toggles: menu > Scripts > this script's section. No keybind.

local enabled   = gui.checkbox("Enabled")
local callDmg   = gui.checkbox("Include damage in the message")
local kickDrama = gui.checkbox("Fake kick-vote drama (callvote, use with judgement)")
local cooldown  = gui.slider("Cooldown (s)", 1, 30, 6)

local lastSnitch = 0.0

local lines = {
    "%s just shot me. great teammate",
    "someone tell %s this is a team game",
    "%s shot me in the back. bonding moment",
    "friendly fire: %s says hello",
    "%s, my blood is on your hands",
    "new player of the match: %s",
}

client.set_event_callback("player_hurt", function(event)
    if not gui.get(enabled) or not event then return end
    local me = entity.get_local_player()
    if not me then return end

    -- event.userid is a 0-based player slot; controller entity index = slot + 1
    if event.userid ~= me - 1 then return end          -- I must be the victim
    local attacker = event.attacker
    if not attacker or attacker >= 65535 then return end
    if attacker == event.userid then return end        -- self-damage, not snitchable

    local function teamOf(slot)
        local pawn = entity.get_player_pawn(slot + 1)
        if not pawn then return nil end
        return entity.get_prop(pawn, "C_BaseEntity", "m_iTeamNum")
    end
    local myTeam, theirTeam = teamOf(event.userid), teamOf(attacker)
    if not myTeam or not theirTeam or myTeam ~= theirTeam then return end

    local now = client.get_time()
    if now - lastSnitch < gui.get(cooldown) then return end
    lastSnitch = now

    local name = entity.get_prop_string(attacker + 1, "CCSPlayerController", "m_iszPlayerName")
        or ("player " .. attacker)
    local text = string.format(lines[math.random(#lines)], name)
    if gui.get(callDmg) then
        text = text .. " (" .. (event.dmg_health or 0) .. " dmg)"
    end
    if #text > 200 then text = string.sub(text, 1, 200) end
    client.exec("say " .. text)

    if gui.get(kickDrama) then
        client.exec("callvote kick " .. attacker)
    end
end)
