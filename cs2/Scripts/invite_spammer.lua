-- invite_spammer.lua - Neversnooze port of the CS:GO skeet lua
-- "ActualInviteCooldownBypass.lua" (github.com/csmit195/skeet-luas).
--
-- Original trick (CS:GO): hook Panorama FriendsListAPI.ActionInviteFriend so the client-side
-- invite cooldown never runs, then invite with ISteamMatchmaking.InviteUserToLobby directly.
-- CS2 port: no Panorama hook needed at all - steam.invite() hits the Steamworks flat accessor
-- directly, so the CS2 party UI (which owns the invite cooldown) is bypassed by construction.
--
-- Loop: invite everyone matching the filter with a small spacing, wait for the rescan
-- interval, refresh the friend list and repeat - re-inviting people the CS2 UI would
-- rate-limit ("invite somebody multiple times").
--
-- PRECONDITION: you must have an open CS2 lobby (PLAY -> host a lobby, or be in a party).
-- steam.get_lobby() reads the lobby the game publishes as rich presence; with no lobby the
-- script just reports "no lobby".

local enabled = gui.checkbox("Invite Loop", false)
local interval = gui.slider("Rescan Interval (s)", 1, 30, 5)
local spacing = gui.slider("Invite Spacing (ms)", 50, 2000, 250)
local mode = gui.dropdown("Who", {"Looking to play", "Playing CS2", "Everyone"}, 2)
local hud = gui.checkbox("Show Status HUD", true)

-- EPersonaState 6 = Looking To Play; k_EFriendFlagImmediate = 4 (internal to the C++ side)
local kLookingToPlay = 6
local kAppCS2 = 730

local queue = {}
local lobby = nil
local nextRescan = 0.0
local nextInvite = 0.0
local status = "idle"
local debugLine = ""
local debugSample = ""

-- The CS2/Steam "Looking to play" grouping is driven by RICH PRESENCE, not the persona state,
-- and game info stays empty until a server is joined - so the filter checks both the persona
-- state (6) and any presence value containing "look".
local function presenceLooksLikeToPlay(sid)
    local presence = steam.get_friend_presence(sid)
    if not presence then return false end
    for _, value in pairs(presence) do
        if value and string.find(string.lower(value), "look", 1, true) then
            return true
        end
    end
    return false
end

local function matchesFilter(sid)
    local m = gui.get(mode)
    if m == 2 or m == nil then
        return true -- Everyone (also the fallback for a stale sidecar index)
    elseif m == 1 then
        local game = steam.get_friend_game(sid)
        return game ~= nil and game.appid == kAppCS2
    end
    return steam.get_friend_state(sid) == kLookingToPlay or presenceLooksLikeToPlay(sid)
end

local function rescan()
    if steam.get_steamid() == nil then
        status = "steam unavailable - see console for the resolve log"
        queue = {}
        return
    end
    lobby = steam.get_lobby()
    if lobby == nil then
        status = "no steam lobby - open your party (PLAY > Open Party)"
        queue = {}
        return
    end
    queue = {}
    local friends = steam.get_friends() or {}
    local seen = {}
    for _, sid in ipairs(friends) do
        seen[sid] = (seen[sid] or 0) + 1
        steam.request_friend_presence(sid) -- fresh presence for the NEXT rescan (async)
        if matchesFilter(sid) then
            queue[#queue + 1] = sid
        end
    end
    -- diagnostics: friend count +, when nothing queues, a presence sample of the first friend
    local unique = 0
    for _ in pairs(seen) do unique = unique + 1 end
    debugLine = string.format("friends %d unique %d queued %d", #friends, unique, #queue)
    debugSample = ""
    if #queue == 0 and #friends > 0 then
        local sid = friends[1]
        local parts = {}
        for key, value in pairs(steam.get_friend_presence(sid) or {}) do
            parts[#parts + 1] = key .. "=" .. value
        end
        debugSample = "sample " .. (steam.get_friend_name(sid) or sid) .. ": " .. table.concat(parts, " | ")
    end
    status = "queued " .. #queue .. " friend(s)"
end

client.set_event_callback("paint", function()
    if not gui.get(enabled) then
        status = "idle"
        return
    end

    local now = client.get_time()
    if now >= nextRescan then
        rescan()
        nextRescan = now + gui.get(interval)
        nextInvite = now
    end

    if #queue > 0 and now >= nextInvite then
        local sid = table.remove(queue, 1)
        local ok = steam.invite(sid)
        local name = steam.get_friend_name(sid) or sid
        status = (ok and "invited " or "invite FAILED: ") .. name
        nextInvite = now + gui.get(spacing) / 1000.0
    end

    if gui.get(hud) then
        local text = "[invite_spammer] " .. status .. (lobby and ("  lobby " .. lobby) or "")
        renderer.filled_rect(6, 6, 520, debugLine ~= "" and 36 or 20, 0, 0, 0, 140)
        renderer.text(12, 10, text, 255, 255, 255, 220)
        if debugLine ~= "" then
            renderer.text(12, 26, debugLine, 150, 150, 160, 200)
        end
        if debugSample ~= "" then
            renderer.filled_rect(6, 6, 520, 52, 0, 0, 0, 140)
            renderer.text(12, 42, string.sub(debugSample, 1, 80), 150, 150, 160, 180)
        end
    end
end)
