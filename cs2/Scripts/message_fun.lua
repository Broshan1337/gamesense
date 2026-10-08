-- message_fun.lua - console-message playground (client.exec bridge demo)
--
-- client.exec(command) runs anything the console can run: "say", "playerchatwheel",
-- cvar sets, and on sv_pausable servers even "pause". It is LOCAL command entry - the
-- server sees exactly what it would see if you typed it, which is the whole point:
-- scriptable chat/radio/cvar experiments with zero new cheat code.
--
-- Only callable inside createmove / game event callbacks (the binding enforces this -
-- the engine command buffer is not thread-safe, so paint callbacks error out).

local enabled = gui.checkbox("Chat playlist", false)
local interval = gui.slider("Every N ticks", 32, 512, 128)

local messages = {
    'say gg wp from a lua script',
    'say {rt}this line uses a radio icon code',
    'playerchatwheel cmd "thanks" 0',
    'say cl_interp is a lifestyle',
}
local index = 0
local tickCount = 0

client.set_event_callback("createmove", function()
    if not gui.get(enabled) then return end
    tickCount = tickCount + 1
    if tickCount % gui.get(interval) ~= 0 then return end

    index = (index + 1) % #messages
    -- Strings here are SCRIPT-OWNED, so client.exec's no-untrusted-text rule is satisfied.
    -- Never build a command from player names or networked strings (newlines = separators).
    client.exec(messages[index + 1])
end)

-- Snapshot the net-lag hook counters while we are here (networks fun is one #include away):
client.set_event_callback("paint", function()
    if gui.get(enabled) then
        local stats = net.stats()
        renderer.text(12, 92, string.format("passed %d | dropped %d | connless %d",
            stats.passed or 0, stats.dropped or 0, stats.connless or 0), 160, 200, 240, 255)
    end
end)

-- Other console one-liners worth knowing (all via client.exec, from createmove only):
--   client.exec("pause")                     - pauses the server IF sv_pausable 1 (clc_RequestPause)
--   client.exec("cl_hud_color 7")            - HUD color without touching config
--   client.exec("playerchatwheel cmd ...")   - radio line, see ChatTools' experiments
