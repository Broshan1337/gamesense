-- Neversneeze example script
-- Events: "paint" (every frame, present thread), "createmove" (per input tick),
--         plus any game event name, e.g. "player_hurt", "weapon_fire".
-- ffi (LuaJIT FFI) is available globally for raw memory work.

local frames = 0

client.set_event_callback("paint", function()
    frames = frames + 1
    local w, h = client.get_screen_size()
    renderer.text(202, h - 400, string.format("lua alive - frame %d", frames), 120, 220, 255, 255)
    renderer.rect(200, h - 400, 190, 14, 120, 220, 255, 200, 1.0)
end)

client.set_event_callback("player_hurt", function()
    client.log("someone got hurt!")
end)
