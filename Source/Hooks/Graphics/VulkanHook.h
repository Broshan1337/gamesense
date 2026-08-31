#pragma once

// Vulkan presentation hook, pointer-cache-swap style (the same technique class as
// PeepEventsHook: swap a pointer the game itself stores and calls through, call the original,
// restore on unload - no code patching, no trampolines, unload guarantees intact).
//
// How it works:
//
//   1. CS2's renderer (librendersystemvulkan.so) resolves its device-level Vulkan functions at
//      init through vkGetDeviceProcAddr and caches the returned pointers in a .bss table. The
//      resolution site is a generated chain of identical 23-byte iterations:
//
//        lea  <name string>(%rip),%rsi     48 8D 35 disp32
//        mov  %rbx,%rdi                    48 89 DF
//        mov  %rax,<slot>(%rip)            48 89 05 disp32   <- stores the PREVIOUS iteration's result
//        call *<resolver>(%rip)            FF 15 disp32      <- vkGetDeviceProcAddr
//
//      (RE'd on the 2026-08 build: 1176 iterations; vkQueuePresentKHR's cache slot lands at
//      renderer .bss 0x8bee78, vkAcquireNextImageKHR at 0x8bee98, vkCreateSwapchainKHR at
//      0x8bee90 - two compiled copies of the chain write the same table.)
//
//   2. We scan the renderer's .text for that iteration shape, decode each iteration's name
//      string, and take the NEXT iteration's store slot (the store lags one iteration behind
//      the name). That yields the exact cache slot the game calls through - whatever pointer
//      the loader handed it - so no assumption about terminator identity is needed. Swapping
//      the qword routes the game's present/acquire/createSwapchain calls to our handlers.
//
//   3. On the first present of a swapchain we build our own render pass + per-backbuffer frame
//      buffers (donor parity), initialize ImGui's Vulkan backend against the REAL device
//      (captured from vkAcquireNextImageKHR's first argument), render the menu into the current
//      backbuffer and submit - all before the original vkQueuePresentKHR runs. The menu submit
//      signals a per-backbuffer semaphore that the hooked present APPENDS to the game's own
//      wait list, so the compositor never sees a frame before the menu pass completed.
//
//   4. Site list is re-verified on every present in case the game re-resolves its cache, and
//      every slot is restored on unload.
//
// Escalation path (agreed decision): if a CS2 update ever breaks the chain pattern or stops
// caching the pointers, fall back to vendoring the donor's detour stack for vkQueuePresentKHR.
namespace VulkanHook
{

// Idempotent. Cheap early-out once installed; internally throttled while unsuccessful, so
// calling it every frame from the render-start hook is fine. Returns true when the presentation
// path is hooked. False means "retry later" (the renderer module or its cache may not exist yet
// during early init).
[[nodiscard]] bool tryInstall() noexcept;

// Unload phase 1: restore every swapped cache pointer (stop new renders from reaching us).
void restorePointers() noexcept;

// Unload phase 2: destroy the ImGui renderer state and our Vulkan resources. Call only after
// HookQuiesce drained every callback, and before GUI::destroy (the renderer shutdown it does
// needs the ImGui context still alive).
void destroyResources() noexcept;

// Blocks until every submit already handed to the device (ours and the game's) has finished.
// MUST run before freeing any GPU texture the menu has been sampling: our menu submits are
// unchained (no semaphore waits), so their in-flight depth is NOT bounded by the swapchain and
// a draw submitted frames ago can still execute after the free. The font reload (menu-scale
// change) skipped this once and the GPU read the freed font image: amdgpu TCP read page fault
// -> ring timeout -> device wedged -> VK_ERROR_DEVICE_LOST fatal (2026-08-29 crash).
void waitUntilDeviceIdle() noexcept;

// Avatar texture plumbing for the ImGui menu (the account-bar image). The UI decodes a file on
// the present thread and stages the RGBA8 pixels here; the GPU upload is recorded into the next
// menu frame's command buffer (before the render pass), submitted with that frame's regular
// slot fence, and query() starts returning the descriptor set once a fence poll proves the
// upload completed. Present-thread only, like everything else in the menu.
namespace avatar_texture
{

// Stages RGBA8 pixels for upload. Takes ownership (freed with free() once staged or rejected);
// only the first request before the texture is ready is honored.
void request(const void* pixelsRgba, int width, int height) noexcept;

// nullptr until the image is uploaded and ready to sample (an ImTextureID / VkDescriptorSet).
[[nodiscard]] void* query() noexcept;

}

// Soft-shadow stamp for the menu: a precomputed gaussian-blurred rounded box, generated on the
// CPU once and uploaded through the same staging machinery as the avatar texture. query()
// returns null until the stamp is ready - the UI keeps its layered-rect fake shadows until
// then (and permanently if the upload failed).
namespace shadow_texture
{

// Stamp layout, mirrored by the UI's 9-slice shadow drawing (UI/ImGui/Neverlose.cpp).
constexpr int kStampSize = 128; // px per side
constexpr int kMargin = 24;     // px per side outside the box edge (the UI maps these to s(12))

// nullptr until the stamp is uploaded and ready to sample (an ImTextureID / VkDescriptorSet).
[[nodiscard]] void* query() noexcept;

}

}
