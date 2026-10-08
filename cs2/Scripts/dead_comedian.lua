-- DEAD COMEDIAN every time you die, fetch a random joke and deliver it to the team


local enabled   = gui.checkbox("Enabled")
local cooldownS = gui.slider("Min cooldown (s)", 10, 120, 30)

local lastDeath = 0.0
local pendingLine = nil

client.set_event_callback("player_death", function(event)
    if not gui.get(enabled) or not event then return end
    local me = entity.get_local_player()
    if not me or event.userid ~= me - 1 then return end

    local now = client.get_time()
    if now - lastDeath < gui.get(cooldownS) then return end
    lastDeath = now

    http.get("https://official-joke-api.appspot.com/random_joke", function(body)
        if not body then return end
        local setup   = string.match(body, '"setup"%s*:%s*"(.-)"')
        local punch   = string.match(body, '"punchline"%s*:%s*"(.-)"')
        local line = (setup and punch) and (setup .. " ... " .. punch) or body
        line = string.gsub(line, '[\\%c"]', ' ')
        line = string.gsub(line, "%s+", " ")
        line = string.sub(line, 1, 120)
        if #line > 10 then
            pendingLine = line
        end
    end)
end)

client.set_event_callback("createmove", function()
    if pendingLine then
        client.exec("say " .. pendingLine)
        pendingLine = nil
    end
end)
