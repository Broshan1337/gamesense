#pragma once

#include <cstdint>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>
#include <SDL/SdlFunctions.h>

// Polls SDL for raw mouse-button state, for the same reasons and in the same shape as KeyboardState
// next door: features on the input path need to know what is held right now, SDL already maintains
// that, and reconstructing it from events would go wrong the moment one was missed.
//
// A separate class rather than a member of KeyboardState because it is a different SDL call -
// SDL_GetKeyboardState covers keys only and has no notion of mouse buttons.
//
// The resolved function is cached, again matching KeyboardState: this runs once per user command,
// and re-resolving would mean a dlopen every tick.
class MouseState {
public:
    [[nodiscard]] static bool isButtonDown(std::uint32_t buttonMask) noexcept
    {
        if (!buttonMask)
            return false;

        const auto getMouseState = resolve();
        if (!getMouseState)
            return false;

        return (getMouseState(nullptr, nullptr) & buttonMask) != 0;
    }

private:
    [[nodiscard]] static sdl3::SDL_GetMouseState* resolve() noexcept
    {
        if (!cached) {
            const DynamicLibrary sdl{cs2::SDL_DLL};
            cached = sdl.getFunctionAddress("SDL_GetMouseState").as<sdl3::SDL_GetMouseState*>();
        }
        return cached;
    }

    // Constant-initialised and trivially destructible: no __cxa_guard under -nostdlib.
    inline static sdl3::SDL_GetMouseState* cached{nullptr};
};
