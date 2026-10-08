-- VALVE ANNOUNCER - every round start, the "server" (you) broadcasts official-looking
-- console messages. Pair with a [VALVe]-badge persona in Misc > CHAT > Persona Presets
-- (e.g. "Server Restart", "Cooldown" or "Admin Msg") for the full effect.
-- Toggles: menu > Scripts > this script's section. No keybind.

local enabled = gui.checkbox("Enabled")
local delay   = gui.slider("Line delay (s)", 1, 5, 2)

local lines = {
    "[Server] matchmaking service connecting...",
    "[Server] updating server: 3% 6% 11% 47%",
    "[Server] your rank has been adjusted to Silver I",
    "[Server] round replay is being uploaded to gotv",
    "[Server] thank you for playing, enjoy your match",
}

local nextLine = 0        -- index into lines; 0 = idle
local nextSend = 0.0

client.set_event_callback("round_start", function()
    if not gui.get(enabled) then return end
    nextLine = 1
    nextSend = client.get_time() + 1.5    -- let the round banner settle first
end)

client.set_event_callback("createmove", function()
    if not gui.get(enabled) or nextLine == 0 then return end
    local now = client.get_time()
    if now < nextSend then return end
    client.exec("say " .. lines[nextLine])
    nextSend = now + gui.get(delay)
    nextLine = nextLine + 1
    if nextLine > #lines then
        nextLine = 0
    end
end)
