#pragma once

// Activation latch for the triggerbot's Toggle mode (triggerbot_vars::KeyMode == 1).
// Plain namespace state, not a class-template member, so the HUD (binds list +
// status chip) can read "is the triggerbot active" without instantiating the
// HookContext-dependent Triggerbot template.
namespace triggerbot_state
{

inline bool latched{false};
inline bool lastKeyWasDown{false};

}