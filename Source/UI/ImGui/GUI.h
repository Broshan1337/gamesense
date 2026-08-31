#pragma once

#include <SDL3/SDL_events.h>
#include <vulkan/vulkan_core.h>

// The ImGui menu. Freestanding-bridged: allocations route to CS2's IMemAlloc
// (ImGuiMemAllocBridge.h), SDL input arrives through PeepEventsHook (polledEvents), rendering
// happens on the game's Vulkan present path (Hooks/Graphics/VulkanHook.h -> render).
//
// Lifecycle:
//   init()      - once after the global context completes (finishInit), BEFORE the first render
//   polledEvents()/render() - steady state, different threads (SDL poll thread vs present thread;
//                 events cross threads through a spinlock-guarded queue, ImGui itself is only
//                 ever touched on the present thread)
//   destroy()   - on the unload path, after HookQuiesce drained every hook callback
//
// Config directory plumbing note: imgui builds with IMGUI_DISABLE_FILE_FUNCTIONS, so there is
// no ini persistence to point at the config directory today; the path will be plumbed in when
// a feature needs file IO (font fallback cache, ini persistence via our own writer, etc.).
namespace GUI
{

bool init() noexcept;
void destroy() noexcept;

[[nodiscard]] bool isMenuOpen() noexcept;
[[nodiscard]] bool isInitialized() noexcept;

// Called from SDLHook_PeepEvents after the original consumed a GET-poll batch. Feeds the events
// into ImGui's input queue and handles the menu toggle key. Returns true when the caller must
// swallow the batch (menu open - the game must not see input).
[[nodiscard]] bool polledEvents(const SDL_Event* events, int count) noexcept;

// Present-thread render entry, called by the Vulkan hook for every swapchain present. No-op
// (returns false) while not initialized / not ready / minimized; true once ImGui::Render() and
// ImGui_ImplVulkan_RenderDrawData() actually ran - the Vulkan hook uses that to mirror the
// backend's upload-buffer rotation for its per-slot fences.
[[nodiscard]] bool render(VkCommandBuffer commandBuffer) noexcept;

// Menu-initiated unload (the Unload button). Consumed once per frame by the render-start hook,
// which owns the actual teardown.
void requestUnload() noexcept;
[[nodiscard]] bool consumeUnloadRequest() noexcept;

}
