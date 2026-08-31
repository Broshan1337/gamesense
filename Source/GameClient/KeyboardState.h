#pragma once

#include <cstdint>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>
#include <SDL/SdlFunctions.h>

// Polls SDL for raw key state.
//
// Deliberately a poll rather than event tracking: features on the input path need to know whether a
// key is held right now, and SDL already maintains that array for us. Tracking key-down/key-up
// through PeepEventsHook would mean reconstructing state the game already has, and would go wrong
// the moment an event is missed (alt-tab, focus loss).
//
// The resolved function is cached: this runs once per user command, and re-resolving it would mean
// a dlopen on every tick - the same mistake that made the hit sound audibly late.
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

    // First held scancode in [minScancode, maxScancode], or -1 if none. One state fetch for the
    // whole scan - the keybind capture UI uses this to find "whatever key was just pressed".
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

    // Constant-initialised and trivially destructible: no __cxa_guard under -nostdlib.
    inline static sdl3::SDL_GetKeyboardState* cached{nullptr};
};
