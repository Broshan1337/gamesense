#pragma once

#include <cstdint>

#include <CS2/Constants/DllNames.h>
#include <GameClient/KeyboardState.h>
#include <GameClient/MouseState.h>
#include <Platform/DynamicLibrary.h>
#include <SDL/SdlFunctions.h>

// Keybind value encoding, shared by config, capture UI and feature code. A bind is ONE byte:
//   0        = Off (feature never activates)
//   1..248   = SDL keyboard scancode (layout-independent physical key)
//   249..253 = mouse buttons (MOUSE4, MOUSE5, MOUSE3, MOUSE1, MOUSE2)
// The keybind UI captures whatever key/button the user presses next and stores it directly in
// this encoding - there is no fixed list to keep in sync (CS2-settings style).
class Bind {
public:
    static constexpr int kOff = 0;
    static constexpr int kMinScancode = 1;
    static constexpr int kMaxScancode = 248;   // SDL scancodes of interest all fit below this
    static constexpr int kMouse4 = 249;        // X1 (forward thumb)
    static constexpr int kMouse5 = 250;        // X2 (back thumb)
    static constexpr int kMouse3 = 251;        // middle
    static constexpr int kMouse1 = 252;        // left
    static constexpr int kMouse2 = 253;        // right
    static constexpr int kLast = kMouse2;

    [[nodiscard]] static bool isDown(int value) noexcept
    {
        if (value >= kMinScancode && value <= kMaxScancode)
            return KeyboardState::isKeyDown(value);
        if (value >= kMouse4 && value <= kMouse2)
            return MouseState::isButtonDown(maskFor(value));
        return false;
    }

    // Display name for the keybind UI. Keyboard names come from SDL itself ("Q", "Caps Lock",
    // "Left Shift" - correct on any layout); mouse buttons and Off are static strings.
    [[nodiscard]] static const char* displayName(int value) noexcept
    {
        switch (value) {
        case kOff: return "Off";
        case kMouse4: return "MOUSE4";
        case kMouse5: return "MOUSE5";
        case kMouse3: return "MOUSE3";
        case kMouse1: return "MOUSE1";
        case kMouse2: return "MOUSE2";
        default:
            if (value >= kMinScancode && value <= kMaxScancode)
                return scancodeName(value);
            return "?";
        }
    }

private:
    [[nodiscard]] static std::uint32_t maskFor(int value) noexcept
    {
        switch (value) {
        case kMouse4: return sdl3::mousebutton::kX1;
        case kMouse5: return sdl3::mousebutton::kX2;
        case kMouse3: return sdl3::mousebutton::kMiddle;
        case kMouse1: return sdl3::mousebutton::kLeft;
        case kMouse2: return sdl3::mousebutton::kRight;
        default: return 0;
        }
    }

    [[nodiscard]] static sdl3::SDL_GetScancodeName* resolveScancodeName() noexcept
    {
        if (!cachedScancodeName) {
            const DynamicLibrary sdl{cs2::SDL_DLL};
            cachedScancodeName = sdl.getFunctionAddress("SDL_GetScancodeName").as<sdl3::SDL_GetScancodeName*>();
        }
        return cachedScancodeName;
    }

    [[nodiscard]] static const char* scancodeName(int scancode) noexcept
    {
        if (const auto fn = resolveScancodeName())
            return fn(scancode);
        return "KEY";
    }

    // Constant-initialised and trivially destructible: no __cxa_guard under -nostdlib.
    inline static sdl3::SDL_GetScancodeName* cachedScancodeName{nullptr};
};
