-- GASLIGHT - when our own datagram hook actually drops packets (net-lag card enabled or
-- genuine loss), tell the team "is anyone else lagging" backed by REAL packet statistics.
-- Nothing beats complaining about lag with evidence.

local enabled   = gui.checkbox("Enabled")
local threshold = gui.slider("Lost packets to trigger", 1, 60, 8)
local interval  = gui.slider("Min interval (s)", 10, 120, 25)

local lastCheck  = 0.0
local lastDropped = 0
local lastSay    = 0.0

client.set_event_callback("createmove", function()
    if not gui.get(enabled) then return end
    local now = client.get_time()
    if now - lastCheck < 2 then return end
    lastCheck = now

    local stats = net.stats()
    if not stats then return end
    local dropped = stats.dropped or 0
    local delta = dropped - lastDropped
    lastDropped = dropped

    if delta >= gui.get(threshold) and now - lastSay >= gui.get(interval) then
        lastSay = now
        client.exec("say anyone else lagging? i just lost " .. delta .. " packets")
    end
end)
