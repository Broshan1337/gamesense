#pragma once

#include <cstdint>

#include <CS2/Constants/DllNames.h>
#include <GameClient/KeyboardState.h>
#include <GameClient/MouseState.h>
#include <Platform/DynamicLibrary.h>
#include <SDL/SdlFunctions.h>







class Bind {
public:
    static constexpr int kOff = 0;
    static constexpr int kMinScancode = 1;
    static constexpr int kMaxScancode = 248;   
    static constexpr int kMouse4 = 249;        
    static constexpr int kMouse5 = 250;        
    static constexpr int kMouse3 = 251;        
    static constexpr int kMouse1 = 252;        
    static constexpr int kMouse2 = 253;        
    static constexpr int kLast = kMouse2;

    [[nodiscard]] static bool isDown(int value) noexcept
    {
        if (value >= kMinScancode && value <= kMaxScancode)
            return KeyboardState::isKeyDown(value);
        if (value >= kMouse4 && value <= kMouse2)
            return MouseState::isButtonDown(maskFor(value));
        return false;
    }

    
    
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

    
    inline static sdl3::SDL_GetScancodeName* cachedScancodeName{nullptr};
};
