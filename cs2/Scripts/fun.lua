-- fun.lua - lagcomp breaker + engine network toys. Own "FUN" subtab; more items land here
-- over time (this file is the drawer for client-safe desync/network fun).
--
-- RULE: nothing here may need sv_cheats or a hosted server. Every item works in a live
-- match from the client side only. Gated engine commands (net_fake*) fail soft to the
-- console - the script never depends on their result.
--
-- Contents:
--   1. LAGCOMP BREAKER - yaw flick patterns + move bias + duck flick + attack spike.
--      Enemies' lagcompensation resolves OUR position history; rapid record changes make
--      their resolved hitbox stale. Costs own aim while active (honest tradeoff).
--      "Choke-sync" mode reads net.stats().dropped and only flicks while YOUR hand-driven
--      C++ choke (Movement > NET LAG > ChokeKeyBind) is withholding - no Lua choke API needed.
--   2. ENGINE FAKELAG/LOSS - client.exec("net_fakelag N") (libnetworksystem: value split
--      into send/recv halves, cap 10000ms) and net_fakeloss. "Not allowed to change
--      net_fakelag" unless permitted - result is console-only, harmless if denied.
--   2b. FAKE PING - scoreboard ping is server-measured RTT: earned with real send-side delay
--      via net.set_delay (sustained offset + 450ms/3s spike). Needs NET LAG master Enabled.
--   2c. FAKE JITTER - net_fakejitter send|recv AVG (libnetworksystem syntax from strings;
--      same gate as fakelag), net_fakeclear reset, net_fakestatus to view (console).
--   3. INTERP LAB - cvar.set_* writes through the value pointer (flag checks bypassed):
--      cl_interp (float ms), cl_interpolate (bool), cl_ignorepackets freeze (bool, engine2,
--      auto-restores via client.delay_call before cl_timeout can kick you).
--   4. VOICE + SEARCH + DEMO - voice_loopback toggle (hear own mic),
--      mm_dedicated_search_maxping (client-side, pairs with the region picker),
--      demo record toggle (client-side engine command).
--   5. MIC FUN - PTT hold/blip via +voicerecord (the exact path RadioManager's mic
--      broadcast uses), mic-live HUD from voice_vox_current_peak, local soundboard via
--      client.play_sound (poor-man's transmit: open PTT and let the mic re-capture it).
--   6. MATCH THEATER (display only, zero packets): floating damage numbers (bullet_impact
--      + player_hurt, slot-matched like ns_pack), first-blood horn, quad/ace callouts,
--      bomb bell. All acoustic via the two proven paths (buttons/bell1.wav, ui/beepclear.wav).
--   7. SPAM (independent buckets): radio rotator over the 21 server-validated CW.* phrases
--      (user-confirmed `playerchatwheel CW.X CW.X` shape), kill-triggered world pings
--      (player_ping = arg-less, separate 5-token bucket), spray momentary (impulse 201).
--      WHY: radio dies after ~3 sends because libserver runs a per-player TOKEN BUCKET
--      (CCSPlayer_RadioServices.m_flRadioTokenSlots) - no client knob exists, token writes
--      are server-authoritative, so there is NO mechanism bypass; rotation wins ONLY if
--      buckets are per-message (unverified - mash and watch), while pings provably stack
--      (own token field m_flPlayerPingTokens). WARNING: teammates can ignore radio and the
--      server tracks m_bHasCommunicationAbuseMute - automated spam risks a comms ban.
--
-- BEWARE sv_auto_cstrafe_logging/kick on strict servers: automated angle/move patterns are
-- exactly what it watches ("input automation detected"). Breaker = casual default.
--
-- NOTE (LuaJIT): one function may use at most 60 upvalues, so ALL controls live in `ui`
-- and ALL mutable state in `st` - never add bare file-scope locals used by callbacks.

gui.tab("FUN")

local ui = {}

gui.divider("LAGCOMP BREAKER")

ui.brkEnable = gui.checkbox("Breaker Enabled", false)
ui.brkMode = gui.dropdown("Breaker Mode", { "Subtle", "Jitter", "Spaz", "Choke-sync" }, 1)
ui.brkStrength = gui.slider("Flick Strength (deg)", 1, 30, 6)
ui.brkMove = gui.checkbox("Move Bias (zigzag)", true)
ui.brkDuck = gui.checkbox("Duck Flick (hitbox teleport)", false)
ui.brkAttack = gui.checkbox("Extra Spike On Attack", true)

gui.divider("ENGINE FAKELAG (console-gated)")

ui.flEnable = gui.checkbox("net_fakelag Enabled", false)
ui.flMs = gui.slider("Fakelag ms (send+recv)", 0, 1000, 60)
ui.lossEnable = gui.checkbox("net_fakeloss Enabled", false)
ui.lossPct = gui.slider("Fakeloss %", 0, 100, 8)

gui.divider("FAKE PING (real delay)")

-- Scoreboard ping (CCSPlayerController.m_iPing) is SERVER-measured transport RTT: it cannot
-- be spoofed, only earned with real send-side delay. This section drives the datagram hook's
-- delay ring from Lua (net.set_delay, 0-500ms) - sustained offset for a constant high ping,
-- momentary spike for the lag-theater. REQUIREMENT: Movement > NET LAG > Enabled must be ON
-- (the card is the master switch; turning it off stops script delay too). Watch it work via
-- the delayed/flushed hook counters and the scoreboard (m_flSmoothedPing lags a few seconds).
ui.pingEnable = gui.checkbox("Fake Ping Enabled", false)
ui.pingMs = gui.slider("Added Ping ms", 0, 500, 120)
ui.pingSpike = gui.checkbox("Ping Spike 450ms/3s (momentary)", false)

gui.divider("FAKE JITTER (console-gated)")

-- net_fakejitter syntax recovered from libnetworksystem strings: "net_fakejitter [send|recv]
-- AVG [PCT] [MAX]". Same permission gate as fakelag (watch the console). net_fakestatus
-- prints the live simulated conditions; net_fakeclear wipes them.
ui.jitEnable = gui.checkbox("Fake Jitter Enabled", false)
ui.jitTarget = gui.dropdown("Jitter Direction", { "send", "recv" }, 1)
ui.jitAvg = gui.slider("Jitter AVG ms", 0, 200, 30)
ui.jitClear = gui.checkbox("Clear Sim Conditions (momentary)", false)

gui.divider("INTERP LAB")

ui.interpEnable = gui.checkbox("cl_interp Override", false)
ui.interpMs = gui.slider("Interp ms", 0, 500, 100)
ui.interpolateOff = gui.checkbox("Disable Interpolation", false)
ui.freezeBtn = gui.checkbox("Freeze World (auto-restore)", false)
ui.freezeSecs = gui.slider("Freeze Seconds", 1, 10, 3)

gui.divider("VOICE + SEARCH + DEMO")

ui.loopback = gui.checkbox("Hear Own Mic (loopback)", false)
ui.maxPingEnable = gui.checkbox("Cap Search Ping", false)
ui.maxPingValue = gui.slider("Max Ping", 30, 200, 80)
ui.labRec = gui.checkbox("Record Demo", false)

gui.divider("MIC FUN")

-- PTT is a latched console button (+voicerecord holds it down like a physical key, exactly
-- how the Radio mic broadcast transmits hands-free). Hot mic = room noise goes live; the
-- talking icon may stick on (funny by itself). No cheats, works in live matches.
ui.pttHold = gui.checkbox("Hold Mic Open (hot mic)", false)
ui.pttBlip = gui.checkbox("Mic Blip 0.6s (momentary)", false)
-- Local soundboard: play_sound is LOCAL-only (you hear it). Poor-man's transmit: open the
-- mic (above) and let it re-capture your speakers - crude, echoey, hilarious. Type any
-- `play`-resolvable game path (buttons/bell1.wav pattern); a dud path just prints to console.
ui.sndPath = gui.text_input("Sound path", "buttons/bell1.wav")
ui.sndPlay = gui.checkbox("Play Sound (momentary)", false)

gui.divider("MATCH THEATER (display only)")

-- Purely local cosmetics: no packets, no cvars, nothing the server can see. Damage numbers
-- match impacts to hurts the ns_pack way (fresh impact from the same shooter); firstblood /
-- ace reset every round_start; bomb bell defaults OFF (ns_pack already toasts the plant).
ui.dmgNums = gui.checkbox("Damage Numbers", true)
ui.firstBlood = gui.checkbox("First Blood Horn", true)
ui.aceCall = gui.checkbox("Quad / Ace Callouts", true)
ui.bombBell = gui.checkbox("Bomb Bell", false)

gui.divider("SPAM (separate buckets)")

-- CW.* phrase table (mirrors ChatTools' server-validated list): `playerchatwheel CW.X CW.X`
-- is the user-confirmed shape (chat line + VOICE for everyone).
ui.radioNext = gui.checkbox("Next Radio (momentary)", false)
ui.radioAuto = gui.checkbox("Radio Auto-Spam", false)
ui.radioTicks = gui.slider("Auto Every N Ticks", 64, 640, 256)
local RADIO_CW = {
    "CW.EcoRound", "CW.SpendRound", "CW.NeedDrop", "CW.NeedPlan", "CW.NeedLeader",
    "CW.GoGoGo", "CW.OMW", "CW.FollowMe", "CW.FollowingYou", "CW.GoA", "CW.GoB",
    "CW.GoToLocMid", "CW.Regroup", "CW.StickTogether", "CW.SpreadOut", "CW.TeamFallBack",
    "CW.HoldPosition", "CW.NeedQuiet", "CW.ImAttacking", "CW.HeardNoise", "CW.SeesEnemy",
}
ui.pingOnKill = gui.checkbox("Ping Where I Kill", false)
ui.sprayNow = gui.checkbox("Spray (momentary)", false)

local COLORS = {
    accent = { 150, 127, 238 },
    green = { 96, 210, 130 },
    red = { 235, 120, 120 },
    faint = { 145, 149, 159 },
}

local MODES = { "Subtle", "Jitter", "Spaz", "Choke-sync" }

-- ---- state (createmove writes, paint only reads) ----
local st = {
    tick = 0, rng = 12345, lastDropped = 0,
    flOn = false, flMs = -1, lossOn = false, lossPct = -1,
    pingPublished = 0, spikeToken = 0, spikeRestore = false,
    jitOn = false, jitAvg = -1, jitTarget = -1,
    interpOn = false, interpMs = -1, origInterp = nil, origKnown = false,
    noInterp = false, freezeToken = 0, freezeRestore = false,
    loopback = false, maxPingOn = false, maxPing = -1, rec = false,
    pttHold = false, blipToken = 0, blipRestore = false,
    impacts = {}, dmgs = {}, fbDone = true, ace = {},
    radioIdx = 0,
}
local hud = { dropsDelta = 0, specs = 0 }

-- small-int LCG (exact in doubles): rand() in [0,1)
local function rand()
    st.rng = (st.rng * 9301 + 49297) % 233280
    return st.rng / 233280
end

local function modeIndex()
    local m = gui.get(ui.brkMode) or 1
    if m < 1 or m > #MODES then m = 1 end
    return m
end

-- 0-based event player slots (userid/attacker, 65535 = nobody) -> controller index.
local function controllerOfSlot(slot)
    if not slot or slot >= 65535 or slot < 0 then return nil end
    return slot + 1
end

-- Momentary checkbox: fires fn once, resets itself. All one-shot console toys use this.
local function momentary(id, fn)
    if gui.get(id) then
        gui.set(id, false)
        fn()
    end
end

-- Bool cvar with console fallback: force-write first (bypasses flag checks for the
-- in-process client/engine/networksystem cvars); when absent, the real console path runs.
local function setEngineBool(name, on)
    if not cvar.set_bool(name, on) then
        client.exec(name .. " " .. (on and "1" or "0"))
    end
end

client.set_event_callback("createmove", function()
    st.tick = st.tick + 1
    local tickCount = st.tick

    -- lag-o-meter + spectator count for the paint HUD (cheap, runs even with breaker off)
    local stats = net.stats()
    local dropped = (stats and stats.dropped) or 0
    hud.dropsDelta = dropped - st.lastDropped
    st.lastDropped = dropped
    local specs = entity.get_spectators()
    hud.specs = specs and #specs or 0

    ------------------------------------------------------------------ 1. breaker
    local brkActive, brkLabel = false, "off"
    if gui.get(ui.brkEnable) then
        local mode = modeIndex()
        local s = gui.get(ui.brkStrength) or 6
        brkLabel = MODES[mode] .. " " .. s
        local mag, doFlick = 0, false
        if mode == 1 then
            mag, doFlick = s * 0.5, (tickCount % 2 == 0)
        elseif mode == 2 then
            mag, doFlick = s, true
        elseif mode == 3 then
            mag, doFlick = s * 3, true
        else -- Choke-sync: flick only while the C++ choke is withholding
            if hud.dropsDelta > 0 then mag, doFlick = s * 3, true end
        end
        if doFlick and mag > 0 then
            local pitch, yaw = cmd.get_view_angles()
            if pitch and yaw then
                local sign = (rand() < 0.5) and 1 or -1
                local nyaw = yaw + sign * mag
                local npitch = pitch
                if mode == 3 then
                    npitch = pitch + ((rand() < 0.5) and 1 or -1) * s * 0.5
                end
                if gui.get(ui.brkAttack) and cmd.is_button_down(buttons.attack) then
                    nyaw = nyaw + ((rand() < 0.5) and 1 or -1) * s
                end
                cmd.set_view_angles(npitch, nyaw)
                brkActive = true
            end
        elseif mode == 4 then
            brkLabel = brkLabel .. " (holding)"
        end
        if gui.get(ui.brkMove) then
            local f = cmd.get_forward_move() or 0
            local l = cmd.get_left_move() or 0
            local bias = ((tickCount % 2) == 0) and 0.45 or -0.45
            if (tickCount % 4) < 2 then
                f = math.max(-1, math.min(1, f + bias))
            else
                l = math.max(-1, math.min(1, l + bias))
            end
            cmd.set_forward_move(f)
            cmd.set_left_move(l)
            brkActive = true
        end
        if gui.get(ui.brkDuck) then
            cmd.press_buttons(buttons.bullrush) -- kills the crouch-to-stand cooldown
            cmd.set_buttons(buttons.duck, (tickCount % 8) < 4)
            brkActive = true
        end
    end
    hud.breaker = brkActive and ("BREAKER: " .. brkLabel) or "breaker off"

    ------------------------------------------------------------------ 2. fakelag
    local flOn = gui.get(ui.flEnable)
    local ms = math.floor(gui.get(ui.flMs) or 0)
    if flOn then
        if (not st.flOn) or ms ~= st.flMs then
            client.exec("net_fakelag " .. ms) -- result lands in the console only
        end
    elseif st.flOn then
        client.exec("net_fakelag 0")
    end
    st.flOn, st.flMs = flOn, ms
    hud.fakelag = flOn and ("fakelag " .. ms .. "ms (console-gated)") or nil

    local lOn = gui.get(ui.lossEnable)
    local pct = math.floor(gui.get(ui.lossPct) or 0)
    if lOn then
        if (not st.lossOn) or pct ~= st.lossPct then
            client.exec("net_fakeloss " .. pct)
        end
    elseif st.lossOn then
        client.exec("net_fakeloss 0")
    end
    st.lossOn, st.lossPct = lOn, pct
    hud.fakeloss = lOn and ("fakeloss " .. pct .. "% (console-gated)") or nil

    ------------------------------------------------------------------ 2b. fake ping
    -- Spike arming takes precedence: while armed the published hold stays 450ms; the
    -- delayed restore only drops a flag (consumed below on the game thread).
    if gui.get(ui.pingSpike) then
        gui.set(ui.pingSpike, false)
        if st.spikeToken == 0 then
            st.spikeToken = tickCount
            local tok = st.spikeToken
            client.delay_call(3, function(t) if t == st.spikeToken then st.spikeRestore = true end end, tok)
        end
    end
    if st.spikeRestore then
        st.spikeRestore = false
        st.spikeToken = 0
    end
    local pingWant = (st.spikeToken ~= 0) and 450
        or ((gui.get(ui.pingEnable) and math.floor(gui.get(ui.pingMs) or 0)) or 0)
    if pingWant ~= st.pingPublished then
        net.set_delay(pingWant) -- 0-500 enforced binding-side; needs NET LAG master on
        st.pingPublished = pingWant
    end
    hud.ping = (st.pingPublished > 0)
        and ("ping +" .. st.pingPublished .. "ms (NET LAG card must be on)") or nil

    ------------------------------------------------------------------ 2c. fake jitter
    local jOn = gui.get(ui.jitEnable)
    local jAvg = math.floor(gui.get(ui.jitAvg) or 0)
    local jTgt = gui.get(ui.jitTarget) or 1
    if jTgt < 1 or jTgt > 2 then jTgt = 1 end
    if jOn then
        if (not st.jitOn) or jAvg ~= st.jitAvg or jTgt ~= st.jitTarget then
            client.exec("net_fakejitter " .. (jTgt == 1 and "send " or "recv ") .. jAvg)
        end
    elseif st.jitOn then
        client.exec("net_fakeclear") -- disabling clears ALL simulated conditions
    end
    st.jitOn, st.jitAvg, st.jitTarget = jOn, jAvg, jTgt
    hud.jitter = jOn and ("jitter " .. (jTgt == 1 and "send " or "recv ") .. jAvg .. "ms") or nil
    momentary(ui.jitClear, function() client.exec("net_fakeclear") end)

    ------------------------------------------------------------- 3. interp lab
    local iOn = gui.get(ui.interpEnable)
    local ims = math.floor(gui.get(ui.interpMs) or 100)
    if iOn then
        if not st.origKnown then
            st.origInterp = cvar.get_float("cl_interp") -- may be nil; restore skips then
            st.origKnown = true
        end
        if (not st.interpOn) or ims ~= st.interpMs then
            cvar.set_float("cl_interp", ims / 1000)
        end
    elseif st.interpOn and st.origInterp ~= nil then
        cvar.set_float("cl_interp", st.origInterp)
    end
    st.interpOn, st.interpMs = iOn, ims
    hud.interp = iOn and ("interp " .. ims .. "ms") or nil

    local noI = gui.get(ui.interpolateOff)
    if noI ~= st.noInterp then
        -- set_bool reports found/wrong-type; doubles as our bool probe
        cvar.set_bool("cl_interpolate", not noI)
        st.noInterp = noI
    end
    if noI then hud.nointerp = "NO INTERP" else hud.nointerp = nil end

    -- freeze: delayed restore only arms a flag here; the write happens below (game thread)
    if gui.get(ui.freezeBtn) then
        if st.freezeToken == 0 then
            if cvar.set_bool("cl_ignorepackets", true) then
                st.freezeToken = tickCount -- nonzero arm token
                local tok = st.freezeToken
                local secs = gui.get(ui.freezeSecs) or 3
                client.delay_call(secs, function(t) if t == st.freezeToken then st.freezeRestore = true end end, tok)
            else
                gui.set(ui.freezeBtn, false) -- unsupported: uncheck, don't stick
            end
        end
    elseif st.freezeToken ~= 0 then
        cvar.set_bool("cl_ignorepackets", false)
        st.freezeToken = 0
    end
    if st.freezeRestore then
        st.freezeRestore = false
        if st.freezeToken ~= 0 then
            cvar.set_bool("cl_ignorepackets", false)
            st.freezeToken = 0
            gui.set(ui.freezeBtn, false)
        end
    end
    hud.frozen = (st.freezeToken ~= 0) and "WORLD FROZEN" or nil

    ------------------------------------------------- 4. voice + search + demo
    local lb = gui.get(ui.loopback)
    if lb ~= st.loopback then setEngineBool("voice_loopback", lb); st.loopback = lb end

    local mpOn = gui.get(ui.maxPingEnable)
    local mpVal = math.floor(gui.get(ui.maxPingValue) or 80)
    if mpOn then
        if (not st.maxPingOn) or mpVal ~= st.maxPing then
            client.exec("mm_dedicated_search_maxping " .. mpVal)
        end
    end
    st.maxPingOn, st.maxPing = mpOn, mpVal
    hud.maxping = mpOn and ("search ping <= " .. mpVal) or nil

    local rec = gui.get(ui.labRec)
    if rec ~= st.rec then
        client.exec(rec and "record fun_session" or "stop")
        st.rec = rec
    end

    ------------------------------------------------------------------ 5. mic fun
    -- PTT hold: same latched +voicerecord the radio broadcast uses. Blip: momentary
    -- 0.6s open via the delay-flag pattern (write happens here, game thread).
    local hold = gui.get(ui.pttHold)
    if hold ~= st.pttHold then
        client.exec(hold and "+voicerecord" or "-voicerecord")
        st.pttHold = hold
    end
    if gui.get(ui.pttBlip) then
        gui.set(ui.pttBlip, false)
        if st.blipToken == 0 and not hold then
            client.exec("+voicerecord")
            st.blipToken = tickCount
            local tok = st.blipToken
            client.delay_call(0.6, function(t) if t == st.blipToken then st.blipRestore = true end end, tok)
        end
    end
    if st.blipRestore then
        st.blipRestore = false
        st.blipToken = 0
        if not gui.get(ui.pttHold) then client.exec("-voicerecord") end
    end
    momentary(ui.sndPlay, function() client.play_sound(gui.get(ui.sndPath) or "") end)
    -- mic-live readout: our own PTT state + the engine's capture peak (same cvar the
    -- radio broadcast health-probe reads). Peak > 0 while open = signal going out.
    local peak = cvar.get_float("voice_vox_current_peak")
    local micOpen = hold or st.blipToken ~= 0
    if micOpen then
        hud.mic = string.format("MIC OPEN (peak %.2f)", peak or 0)
    else
        hud.mic = nil
    end

    ------------------------------------------------------------------ 7. spam
    local function fireRadio()
        st.radioIdx = (st.radioIdx % #RADIO_CW) + 1
        local cw = RADIO_CW[st.radioIdx]
        client.exec("playerchatwheel " .. cw .. " " .. cw)
        hud.radio = "radio: " .. cw
    end
    momentary(ui.radioNext, fireRadio)
    if gui.get(ui.radioAuto) then
        local every = math.floor(gui.get(ui.radioTicks) or 256)
        if every < 1 then every = 1 end
        if tickCount % every == 0 then fireRadio() end
    end
    momentary(ui.sprayNow, function() client.exec("impulse 201") end)
end)

------------------------------------------------------------------ events (game thread)
client.set_event_callback("round_start", function()
    st.fbDone = false
    st.ace = {}
    st.impacts = {}
end)

client.set_event_callback("bullet_impact", function(e)
    if not e or not e.x then return end
    st.impacts[#st.impacts + 1] = { x = e.x, y = e.y, z = e.z, when = client.get_time(), shooter = e.userid }
    if #st.impacts > 24 then table.remove(st.impacts, 1) end
end)

client.set_event_callback("player_hurt", function(e)
    if not e then return end
    if not gui.get(ui.dmgNums) then return end
    local me = entity.get_local_player()
    if not me then return end
    local attacker = controllerOfSlot(e.attacker)
    local victim = controllerOfSlot(e.userid)
    local mine, ours = (attacker == me), (victim == me)
    if not (mine or ours) then return end -- clutter filter: only our fights
    -- place at the shooter's freshest impact (ns_pack's 0.3s match window)
    local now = client.get_time()
    local spot = nil
    for i = #st.impacts, 1, -1 do
        local im = st.impacts[i]
        if now - im.when < 0.3 and im.shooter == e.attacker then spot = im break end
    end
    if not spot then return end
    st.dmgs[#st.dmgs + 1] = {
        x = spot.x, y = spot.y, z = spot.z, amount = e.dmg_health or 0,
        friendly = mine, untilT = now + 1.2,
    }
    if #st.dmgs > 16 then table.remove(st.dmgs, 1) end
end)

client.set_event_callback("player_death", function(e)
    if not e then return end
    local slot = e.attacker
    if slot and slot < 65535 then
        -- ace board (suicides don't count)
        if slot ~= e.userid then
            st.ace[slot] = (st.ace[slot] or 0) + 1
            if gui.get(ui.aceCall) then
                if st.ace[slot] == 4 then
                    client.notify("QUAD KILL", 235, 180, 120)
                    client.play_sound("ui/beepclear.wav")
                elseif st.ace[slot] >= 5 then
                    client.notify("ACE!", 150, 127, 238)
                    client.play_sound("buttons/bell1.wav")
                end
            end
        end
        -- first blood horn (any first death of the round)
        if not st.fbDone and gui.get(ui.firstBlood) then
            st.fbDone = true
            client.notify(e.headshot == 1 and "FIRST BLOOD (headshot)" or "FIRST BLOOD", 235, 120, 120)
            client.play_sound("buttons/bell1.wav")
        end
        -- kill ping: separate 5-token bucket, throttled by kill rate itself
        if gui.get(ui.pingOnKill) and slot ~= e.userid then
            local me = entity.get_local_player()
            if me and controllerOfSlot(slot) == me then
                client.exec("player_ping")
            end
        end
    end
end)

client.set_event_callback("bomb_planted", function()
    if gui.get(ui.bombBell) then client.play_sound("buttons/bell1.wav") end
end)

client.set_event_callback("bomb_defused", function()
    if gui.get(ui.bombBell) then client.play_sound("ui/beepclear.wav") end
end)

client.set_event_callback("paint", function()
    local y = 100
    local function line(text, r, g, b)
        renderer.text(12, y, text, r, g, b, 255)
        local _, th = renderer.text_size(text)
        y = y + th + 2
    end
    local a = COLORS.accent
    line(hud.breaker or "breaker off", a[1], a[2], a[3])
    if hud.dropsDelta > 0 then
        local r = COLORS.red
        line("choking: +" .. hud.dropsDelta .. " withheld", r[1], r[2], r[3])
    end
    if hud.fakelag then line(hud.fakelag, COLORS.faint[1], COLORS.faint[2], COLORS.faint[3]) end
    if hud.fakeloss then line(hud.fakeloss, COLORS.faint[1], COLORS.faint[2], COLORS.faint[3]) end
    if hud.ping then line(hud.ping, COLORS.green[1], COLORS.green[2], COLORS.green[3]) end
    if hud.jitter then line(hud.jitter, COLORS.faint[1], COLORS.faint[2], COLORS.faint[3]) end
    if hud.interp then line(hud.interp, COLORS.faint[1], COLORS.faint[2], COLORS.faint[3]) end
    if hud.nointerp then line(hud.nointerp, COLORS.red[1], COLORS.red[2], COLORS.red[3]) end
    if hud.frozen then line(hud.frozen, COLORS.red[1], COLORS.red[2], COLORS.red[3]) end
    if hud.maxping then line(hud.maxping, COLORS.faint[1], COLORS.faint[2], COLORS.faint[3]) end
    if hud.mic then line(hud.mic, COLORS.red[1], COLORS.red[2], COLORS.red[3]) end
    if hud.radio then line(hud.radio, COLORS.accent[1], COLORS.accent[2], COLORS.accent[3]) end
    if hud.specs > 0 then
        local g = COLORS.green
        line(hud.specs .. " watching", g[1], g[2], g[3])
    end
    -- floating damage numbers: project, float upward with age, fade out
    if gui.get(ui.dmgNums) and #st.dmgs > 0 then
        local now = client.get_time()
        local keep = {}
        for _, d in ipairs(st.dmgs) do
            local age = d.untilT - now
            if age > 0 then
                keep[#keep + 1] = d
                local sx, sy = renderer.world_to_screen(d.x, d.y, d.z)
                if sx then
                    local c = d.friendly and COLORS.green or COLORS.red
                    renderer.text(sx - 10, sy - (1.2 - age) * 40, tostring(d.amount),
                        c[1], c[2], c[3], math.floor(255 * math.min(1, age * 2)))
                end
            end
        end
        st.dmgs = keep
    end
end)

client.notify("fun loaded - see the FUN tab", 150, 127, 238)
