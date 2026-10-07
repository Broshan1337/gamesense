#pragma once

#include <cstdint>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>
#include <SDL/SdlFunctions.h>










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

    
    inline static sdl3::SDL_GetMouseState* cached{nullptr};
};
