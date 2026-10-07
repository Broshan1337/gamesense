#pragma once

#include <cstdint>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>
#include <SDL/SdlFunctions.h>










class KeyboardState {
public:
    [[nodiscard]] static bool isKeyDown(int scancode) noexcept
    {
        if (scancode < 0)
            return false;

        const auto getKeyboardState = resolve();
        if (!getKeyboardState)
            return false;

        int keyCount = 0;
        const auto keys = getKeyboardState(&keyCount);
        if (!keys || scancode >= keyCount)
            return false;

        return keys[scancode] != 0;
    }

    
    
    [[nodiscard]] static int firstDownIndex(int minScancode, int maxScancode) noexcept
    {
        if (maxScancode < minScancode)
            return -1;

        const auto getKeyboardState = resolve();
        if (!getKeyboardState)
            return -1;

        int keyCount = 0;
        const auto keys = getKeyboardState(&keyCount);
        if (!keys)
            return -1;

        const auto upper = maxScancode < keyCount ? maxScancode : keyCount - 1;
        for (int scancode = minScancode; scancode <= upper; ++scancode) {
            if (keys[scancode] != 0)
                return scancode;
        }
        return -1;
    }

private:
    [[nodiscard]] static sdl3::SDL_GetKeyboardState* resolve() noexcept
    {
        if (!cached) {
            const DynamicLibrary sdl{cs2::SDL_DLL};
            cached = sdl.getFunctionAddress("SDL_GetKeyboardState").as<sdl3::SDL_GetKeyboardState*>();
        }
        return cached;
    }

    
    inline static sdl3::SDL_GetKeyboardState* cached{nullptr};
};
