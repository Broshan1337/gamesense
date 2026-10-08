#pragma once

#include <SDL3/SDL_events.h>
#include <vulkan/vulkan_core.h>















namespace GUI
{

bool init() noexcept;
void destroy() noexcept;

[[nodiscard]] bool isMenuOpen() noexcept;
[[nodiscard]] bool isInitialized() noexcept;



void hideMenuNow() noexcept;
// Relative motion enters the game's ordinary SDL input path (visible aim).
[[nodiscard]] bool applyAimMotion(float dx, float dy) noexcept;
[[nodiscard]] bool hasRecentPhysicalMouseMotion() noexcept;




[[nodiscard]] int polledEvents(SDL_Event* events, int count) noexcept;





[[nodiscard]] bool render(VkCommandBuffer commandBuffer) noexcept;



void requestUnload() noexcept;
[[nodiscard]] bool consumeUnloadRequest() noexcept;

}
