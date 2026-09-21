#pragma once

// Present interception.
//
// Iteration 1 (inline hook on libvulkan's public vkQueuePresentKHR) proved the loader
// export is NOT on the game's present path: shaderapivk resolved its present pointer
// through vkGetDeviceProcAddr at device creation (pre-injection), which returns the
// loader's internal terminator - a different code address. Kept as an observer.
//
// Iteration 2 goes after the SHARED destination instead: the loader's queue wrapper
// objects (magic 0x10ADED040410ADED at +0) hold the driver's function table, and the
// loader's own fast path is literally `jmp *0x690(%rax)` through it. The terminator
// path funnels into the same driver function. Overwriting the slot at wrapper+0x690
// (verified: value must point into libvulkan_radeon's exec range) intercepts presents
// from EVERY resolution path, retroactively, with an atomic 8-byte store.
namespace ns_tf2 {

// Iteration 1: inline hook on the public export. Returns true when installed.
bool installVkPresentHook();

// Iteration 2: dispatch-slot patch. Returns the number of slots patched (0 = nothing found).
int installVkPresentSlotHook();

// Stops the background rescan thread.
void shutdownVkPresentSlotHook();

} // namespace ns_tf2
