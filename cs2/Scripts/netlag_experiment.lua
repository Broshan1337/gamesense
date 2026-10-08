-- netlag_experiment.lua - connectionless rate-limiter experiment (OWN SERVERS ONLY)
--
-- Drives the C++ net library from Lua: builds 0xFFFFFFFF-prefixed datagrams, bursts them at
-- the game server via net.send_raw (which rides the datagram hook's captured endpoint and the
-- ORIGINAL sendto), and draws the hook's counters on screen. The server console should show
-- "Traffic from <ip> was blocked for exceeding rate limits" once the per-IP budget trips
-- (engine2 connectionless limiter: 0x508800 -> 0x557400 -> 0x556B40).
--
-- Watch out: a server can netban your address on top of rate-blocking. Run this only against
-- a server you control. Requires the cheat's datagram hook to be installed (it is, from init).

local enabled = gui.checkbox("Connless flood enabled", false)
local burst = gui.slider("Packets per tick", 1, 64, 8)
local rate = gui.slider("Fire every N ticks", 1, 32, 1)
local fuzz = gui.checkbox("Connless FUZZ (own server only)", false)
local fuzzN = gui.slider("Fuzz packets per tick", 1, 64, 8)

local tickCount = 0
local lastStats = nil
local fuzzSent = 0

-- Connectionless-shaped payload: magic + command byte ('q'/'k'/'!' rotate) + filler.
local commands = { "q", "k", "!" }

local function buildPacket(i)
    local cmd = commands[(i % #commands) + 1]
    return "\xFF\xFF\xFF\xFF" .. cmd .. "\0\0\0\0\0\0\0"
end

-- Connless fuzzer (crash-oracle lab, OWN SERVER ONLY). Mutates magic length, command byte
-- (half valid q/k/! to stay near the real parser, half full 0-255 incl NUL) and filler.
-- SCOPE: connectionless ONLY. Raw UDP can never reach in-band protobuf handlers
-- (CCLCMsg_*/VoiceData/etc.) - game traffic is SNS-framed+encrypted and unframed junk is
-- dropped below dispatch; only 0xFFFFFFFF traffic reaches engine2's connectionless
-- dispatcher (IConnectionlessPacketHandler; unknown bytes log "Bad connectionless packet
-- ( CL '%c')" and die there). Mutating in-band CONTENT needs FFI into the engine's real
-- send path (per-build RE, own-client crash risk) - future work, not this script.
-- ORACLE (on YOUR server console): "Bad connectionless packet" lines, "Traffic from %s
-- was blocked for exceeding rate limits" (the limiter fires BEFORE anything interesting -
-- that is expected finding #1), server fps, process liveness. Method: one variable at a
-- time, start 1/tick, raise slowly; the server may netban the address, so use your own.
local frng = 987654321
local function frand(n) -- deterministic 1..n (exact in doubles)
    frng = (frng * 9301 + 49297) % 233280
    return (frng % n) + 1
end

local fuzzMagics = { "\xFF\xFF\xFF\xFF", "\xFF\xFF\xFF\xFF", "\xFF\xFF\xFF\xFF", "\xFF\xFF\xFF", "\xFF\xFF\xFF\xFF\xFF", "" }

local function buildFuzzPacket()
    local m = fuzzMagics[frand(#fuzzMagics)]
    local c
    if frand(2) == 1 then
        c = commands[frand(#commands)]
    else
        c = string.char(frand(256) - 1)
    end
    local parts = {}
    for _ = 1, frand(60) - 1 do parts[#parts + 1] = string.char(frand(256) - 1) end
    return m .. c .. table.concat(parts)
end

client.set_event_callback("createmove", function()
    if gui.get(fuzz) then
        local n = gui.get(fuzzN)
        for _ = 1, n do
            net.send_raw(buildFuzzPacket(), 1)
            fuzzSent = fuzzSent + 1
        end
    end

    if not gui.get(enabled) then return end

    tickCount = tickCount + 1
    if (tickCount % gui.get(rate)) ~= 0 then return end

    local n = gui.get(burst)
    local i = 0
    -- net.send_raw is capped at 256 packets per call; split larger bursts.
    while n > 0 and i < 8 do
        local chunk = math.min(n, 256)
        net.send_raw(buildPacket(i), chunk)
        n = n - chunk
        i = i + 1
    end
end)

client.set_event_callback("paint", function()
    local stats = net.stats()
    lastStats = stats
    local fd, ip, port = net.server()
    if not fd then
        renderer.text(12, 40, "netlag: not connected", 255, 120, 120, 255)
        return
    end
    renderer.text(12, 40, string.format("netlag: %s:%d (fd %d)", ip, port, fd), 220, 220, 230, 255)
    renderer.text(12, 56, string.format("connless sent: %d   raw: %d", stats.connless or 0, stats.raw or 0), 180, 220, 180, 255)
    renderer.text(12, 72, string.format("hook: passed %d  dropped %d  duped %d  flooded %d",
        stats.passed or 0, stats.dropped or 0, stats.duped or 0, stats.flooded or 0), 170, 190, 220, 255)
    renderer.text(12, 88, string.format("fuzz sent: %d", fuzzSent), 235, 180, 120, 255)
end)
