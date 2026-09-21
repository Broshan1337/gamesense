#pragma once

#include <cstdint>

namespace sdl3
{

struct SDL_Window;
using SDL_ShowSimpleMessageBox = int(std::uint32_t flags, const char* title, const char* message, SDL_Window* window);
using SDL_PeepEvents = int(void* events, int numevents, int action, unsigned minType, unsigned maxType);

// SDL3 returns `const bool*` here where SDL2 returned `const Uint8*`. Both are one byte per key, so
// reading it as bytes is correct either way and avoids depending on which of the two is loaded.
using SDL_GetKeyboardState = const std::uint8_t*(int* numkeys);

// Returns a bitmask of the currently held mouse buttons. Both output parameters are always passed
// as nullptr here, which incidentally makes this correct against either SDL version: SDL3 declares
// them `float*` where SDL2 used `int*`, and a null pointer is a null pointer to both.
using SDL_GetMouseState = std::uint32_t(float* x, float* y);

// Localized-ish display name of a scancode ("Q", "Caps Lock", "Left Shift"). Present in SDL2 and
// SDL3 with the same signature. Only used for the keybind UI labels.
using SDL_GetScancodeName = const char*(int scancode);

// SDL builds this mask as `1 << (button - 1)` over its button numbering, where X1 and X2 are the
// two thumb buttons (4 and 5). Those are what CS2 calls MOUSE4 and MOUSE5.
namespace mousebutton
{
constexpr std::uint32_t kLeft = 1u << 0;
constexpr std::uint32_t kMiddle = 1u << 1;
constexpr std::uint32_t kRight = 1u << 2;
constexpr std::uint32_t kX1 = 1u << 3;
constexpr std::uint32_t kX2 = 1u << 4;
}

// Scancodes are layout-independent - the physical key, not the character it produces - so this
// stays correct on non-QWERTY layouts, which a keycode would not.
namespace scancode
{
constexpr int kE = 8;
constexpr int kSpace = 44;
}

}
