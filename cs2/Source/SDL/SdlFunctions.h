#pragma once

#include <cstdint>

namespace sdl3
{

struct SDL_Window;
using SDL_ShowSimpleMessageBox = int(std::uint32_t flags, const char* title, const char* message, SDL_Window* window);
using SDL_PeepEvents = int(void* events, int numevents, int action, unsigned minType, unsigned maxType);



using SDL_GetKeyboardState = const std::uint8_t*(int* numkeys);




using SDL_GetMouseState = std::uint32_t(float* x, float* y);



using SDL_GetScancodeName = const char*(int scancode);
// Layout-translated key name (AZERTY etc.): SDL3 maps the scancode through the
// OS keyboard layout, unlike SDL_GetScancodeName which is physical/QWERTY.
using SDL_GetKeyFromScancode = int(int scancode);
using SDL_GetKeyName = const char*(int key);



namespace mousebutton
{
constexpr std::uint32_t kLeft = 1u << 0;
constexpr std::uint32_t kMiddle = 1u << 1;
constexpr std::uint32_t kRight = 1u << 2;
constexpr std::uint32_t kX1 = 1u << 3;
constexpr std::uint32_t kX2 = 1u << 4;
}



namespace scancode
{
constexpr int kE = 8;
constexpr int kTab = 43;
constexpr int kSpace = 44;
}

}
