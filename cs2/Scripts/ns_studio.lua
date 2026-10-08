-- ns_studio.lua - exercises the full "script = native feature" surface added in API v4:
--   * gui.page(...)  -> controls rendered in cards INSIDE native tabs (Visuals + Misc)
--   * imgui.*        -> a real draggable ImGui window with widgets ("menu" callback)
--   * renderer v2    -> polygon / arc / gradient / rounded / clip primitives ("paint" callback)
--
-- Values persist via the usual sidecar (<name>.gui) across reloads.

gui.tab("STUDIO")

---------------------------------------------------------------- script controls
-- These two land in a card at the bottom of the VISUALS page.
gui.page("Visuals")
local showDemoHud   = gui.checkbox("Studio demo HUD", true)
local demoThreatYaw = gui.slider("Demo threat yaw", 0, 90, 30)

-- These two land in a card at the bottom of MISC.
gui.page("Misc")
local studioVerbose = gui.checkbox("Verbose toasts", false)
local studioNote    = gui.dropdown("Studio note", {"off", "on", "verbose"}, 0)

-- Everything after this call lives on the script's own sub-tab again.
gui.page()
gui.divider("Window state")
local showStudioWindow = gui.checkbox("Show studio window", true)
local windowScale      = gui.slider("Window scale %", 50, 200, 100)

---------------------------------------------------------------- imgui window
local hue = { 150, 127, 238, 255 }
local noteText = "feature notes..."
local windowOpen = true

client.set_event_callback("menu", function()
    if not gui.get(showStudioWindow) then return end

    local scale = gui.get(windowScale) / 100.0
    imgui.set_next_window_pos(80, 80)
    local visible, open = imgui.begin("ns studio", windowOpen, 320 * scale, 0)
    if visible then
        imgui.text("script-owned ImGui window")
        imgui.separator()

        local v = gui.get(showDemoHud)
        local nv = imgui.checkbox("Demo HUD (mirrors Visuals card)", v)
        if nv ~= v then gui.set(showDemoHud, nv) end

        local s = gui.get(demoThreatYaw)
        local ns = imgui.slider_int("Threat yaw", s, 0, 90)
        if ns ~= s then gui.set(demoThreatYaw, ns) end

        noteText = imgui.input_text("Notes", noteText)

        local choice = imgui.combo("Preset", 1, "raw", "faded", "boxed")
        imgui.same_line()
        if imgui.button("apply") then
            client.notify("studio: applied choice " .. choice, 150, 127, 238)
        end

        local r, g, b, a = imgui.color_edit("Window accent", hue[1], hue[2], hue[3], hue[4])
        hue[1], hue[2], hue[3], hue[4] = r, g, b, a
    end
    windowOpen = open
    imgui.end_window()
end)

---------------------------------------------------------------- renderer demo (paint)
client.set_event_callback("paint", function()
    if gui.get(showDemoHud) then
        local sw, sh = client.get_screen_size()
        -- rounded HUD chip with a gradient wash
        renderer.rounded_rect(sw - 260, sh - 150, 220, 64, 10, 14, 14, 16, 235, 1.0, true)
        renderer.gradient_rect(sw - 250, 140, 40, 12, gui.get(demoThreatYaw) * 2, 60, 220, 255, 40, 40, 60, 255, false)
        -- threat arc: sweep follows the yaw slider
        renderer.arc(sw - 120, 96, 40, 90 - gui.get(demoThreatYaw), 90 + gui.get(demoThreatYaw), 150, 127, 238, 255, 2.0)
        -- clipped zone + polygon
        renderer.push_clip(sw - 260, 150, 220, 56)
        renderer.polygon({ sw - 240, 160, sw - 180, 200, sw - 120, 200, sw - 60, 160 }, 96, 210, 130, 180, true)
        renderer.pop_clip()
        renderer.text(sw - 246, 154, "ns_studio", 226, 228, 235, 255)
    end
end)

client.set_event_callback("player_hurt", function(e)
    if e and gui.get(studioVerbose) then
        client.notify("hurt " .. e.dmg_health)
    end
end)