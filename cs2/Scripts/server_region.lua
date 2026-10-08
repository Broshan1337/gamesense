-- SERVER REGION SELECTOR - pin CS2 matchmaking to one SDR region.
--
-- How it works: CS2 has no region cvar. Matchmaking picks a data center from the SDR relay
-- pings your client reports, so this script blocks every relay IP EXCEPT the selected
-- region's (fetched live from Valve's own ISteamApps/GetSDRConfig API). Dead relays are
-- excluded from matchmaking, so only your region gets matched - the same trick the firewall
-- server-picker tools use, but done inside the process via our datagram hook: no root, no
-- firewall rules, instantly reversible.
--
-- The filter never touches the match you are already in (the currently-connected game server
-- always passes). SELECT THE REGION AND WAIT FOR THE HUD LINE TO SAY THE RELAY LIST IS
-- BLOCKED BEFORE QUEUEING - pings reported before the filter was live are what matchmaking
-- acts on.
--
-- Toggles: menu > Scripts > this script's section. Purple status line lives in the top-left
-- corner.

local enabled = gui.checkbox("Enabled")
local options = {
    "Off (no filter)",
    "Alibaba Cloud Beijing - Mobile (China)",
    "Alibaba Cloud Beijing - Telecom (China)",
    "Alibaba Cloud Beijing - Unicom (China)",
    "Alibaba Cloud Chengdu - Mobile (China)",
    "Alibaba Cloud Chengdu - Telecom (China)",
    "Alibaba Cloud Chengdu - Unicom (China)",
    "Alibaba Cloud Shanghai - Mobile (China)",
    "Alibaba Cloud Shanghai - Telecom (China)",
    "Alibaba Cloud Shanghai - Unicom (China)",
    "Amsterdam (Netherlands)",
    "Atlanta (Georgia)",
    "Buenos Aires (Argentina)",
    "Chennai - Ambattur (India)",
    "Chicago (Illinois)",
    "Dallas (Texas)",
    "Dubai (United Arab Emirates)",
    "Frankfurt (Germany)",
    "Guam",
    "Hong Kong",
    "Johannesburg (South Africa)",
    "Lima (Peru)",
    "London (England)",
    "Los Angeles (California)",
    "Madrid (Spain)",
    "Mumbai (India)",
    "Paris (France)",
    "Santiago (Chile)",
    "Sao Paulo (Brazil)",
    "Seattle (Washington)",
    "Seoul (South Korea)",
    "Singapore",
    "Sterling (Virginia)",
    "Stockholm - Bromma (Sweden)",
    "Stockholm - Kista (Sweden)",
    "Sydney (Australia)",
    "Tencent Guangzhou - Mobile (China)",
    "Tencent Guangzhou - Telecom (China)",
    "Tencent Guangzhou - Unicom (China)",
    "Tokyo Koto City (Japan)",
    "Vienna (Austria)",
    "Warsaw (Poland)",
}
local region = gui.dropdown("Region", options, 0)

-- dropdown index -> SDR pop key (index 1 is the Off option). Auto-generated from the Valve API;
-- if Valve adds or renames regions, regenerate this table from the same endpoint.
local keyForIndex = {
    [2] = "pekm", [3] = "pekt", [4] = "peku", [5] = "ctum", [6] = "ctut", [7] = "ctuu",
    [8] = "pvgm", [9] = "pvgt", [10] = "pvgu", [11] = "ams", [12] = "atl", [13] = "eze",
    [14] = "maa2", [15] = "ord", [16] = "dfw", [17] = "dxb", [18] = "fra", [19] = "gum",
    [20] = "hkg", [21] = "jnb", [22] = "lim", [23] = "lhr", [24] = "lax", [25] = "mad",
    [26] = "bom2", [27] = "par", [28] = "scl", [29] = "gru", [30] = "sea", [31] = "seo",
    [32] = "sgp", [33] = "iad", [34] = "sto2", [35] = "sto", [36] = "syd", [37] = "tgdm",
    [38] = "tgdt", [39] = "tgdu", [40] = "tyo", [41] = "vie", [42] = "waw",
}

-- ---- state ----
local pops = nil            -- parsed SDR config: pops[key] = {desc, ips, aliases}
local fetchFailed = false
local lastFetch = 0.0
local appliedIndex = 0      -- dropdown index the current block list was built for
local appliedCount = 0      -- entries in the active block list (0 = filter idle)

-- ---- SDR config fetching + parsing (http callback runs on the paint thread: parse here, the
-- block list itself is a thread-safe publish into the datagram hook) ----

-- Extract every pop object from '"pops":{...}' with a brace-depth scan - no JSON lib in the
-- sandbox, but the machine-generated shape is stable (no '{' inside string values).
local function parseSdrConfig(body)
    local start = body:find('"pops"', 1, true)
    if not start then return nil end
    local parsed = {}
    local i = start + 6
    while true do
        local _, close, key = body:find('"([%w]+)"%s*:%s*{', i)
        if not key then break end
        local depth, j = 1, close + 1
        while depth > 0 and j <= #body do
            local c = body:sub(j, j)
            if c == '{' then depth = depth + 1
            elseif c == '}' then depth = depth - 1 end
            j = j + 1
        end
        local obj = body:sub(close + 1, j - 1)
        local desc = obj:match('"desc"%s*:%s*"([^"]*)"')
        if desc and obj:find('"relays"', 1, true) then
            local ips = {}
            for ip in obj:gmatch('"ipv4"%s*:%s*"([%d%.]+)"') do
                ips[#ips + 1] = ip
            end
            local aliases = {}
            local aliasArray = obj:match('"aliases"%s*:%s*(%b[])')
            for alias in (aliasArray or ""):gmatch('"([%w]+)"') do
                aliases[alias] = true
            end
            parsed[key] = { desc = desc, ips = ips, aliases = aliases }
        end
        i = j
    end
    return next(parsed) and parsed or nil
end

local function fetchSdrConfig()
    lastFetch = client.get_time()
    fetchFailed = false -- optimistic; the callback re-sets it if this attempt fails too
    http.get("https://api.steampowered.com/ISteamApps/GetSDRConfig/v1/?appid=730", function(body)
        if not body then
            fetchFailed = true
            return
        end
        pops = parseSdrConfig(body)
        if not pops then
            fetchFailed = true
        else
            appliedIndex = -1 -- fresh data; force re-apply on the next tick
        end
    end)
end

-- Allowed set for a selection: the pop itself + its aliases + any pop aliasing to it.
local function allowedSet(key)
    local allowed = { [key] = true }
    local self = pops[key]
    if self then
        for alias in pairs(self.aliases) do
            allowed[alias] = true
        end
    end
    for k, pop in pairs(pops) do
        if pop.aliases[key] then
            allowed[k] = true
        end
    end
    return allowed
end

-- Publish the block list: every relay IP that is NOT in the allowed set.
local function applyRegion(index)
    appliedIndex = index
    local key = keyForIndex[index]
    local selected = key and pops[key]
    if not selected then
        appliedCount = 0
        net.clear_blocked_ips()
        return
    end
    local allowed = allowedSet(key)
    local blocked = {}
    for k, pop in pairs(pops) do
        if not allowed[k] then
            for _, ip in ipairs(pop.ips) do
                blocked[#blocked + 1] = ip
            end
        end
    end
    appliedCount = net.set_blocked_ips(blocked) or 0
end

-- The poller runs on paint, NOT createmove: CreateMove does not tick in the main menu (the
-- game builds no user commands there), and the block list has to be live BEFORE queueing.
-- paint fires every frame in the menu too; net.set_blocked_ips is a thread-safe publish and
-- http callbacks are polled on this same thread, so paint is the right context.
client.set_event_callback("paint", function()
    local enabledNow = gui.get(enabled)
    local selection = enabledNow and gui.get(region) or 0
    if selection == 0 then
        if appliedIndex ~= 0 then
            appliedIndex, appliedCount = 0, 0
            net.clear_blocked_ips()
        end
    else
        -- keep the relay list fresh: retry every 15s until it loads, refresh every 15min after
        if not pops or client.get_time() - lastFetch > 900 then
            if client.get_time() - lastFetch > (pops and 900 or 15) then
                fetchSdrConfig()
            end
        elseif selection ~= appliedIndex then
            applyRegion(selection)
        end
    end

    if not enabledNow then return end

    local text, status
    if selection == 0 then
        text = "REGION: off"
        status = nil
    else
        -- the dropdown's own label - never "?", even before the relay list loads
        text = "REGION: " .. (options[selection] or "?")
        if not pops then
            status = fetchFailed and "relay list failed - retrying..." or "fetching relay list..."
        else
            local key = keyForIndex[selection]
            if not (key and pops[key]) then
                status = "region missing from relay list - pick another"
            else
                local stats = net.stats()
                local dropped = stats and stats.region_blocked or 0
                status = string.format("pinning: %d relays blocked, %d dropped - queue now", appliedCount, dropped)
            end
        end
    end

    -- top-left corner, clear of the Steam overlay's centered "Back to Game" stack
    local x, y = 14, 10
    local _, th = renderer.text_size(text)
    renderer.text(x, y, text, 150, 127, 238, 255)
    if status then
        renderer.text(x, y + th + 2, status, 150, 127, 238, 190)
    end
end)
