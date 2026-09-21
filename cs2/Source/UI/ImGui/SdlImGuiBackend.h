#pragma once

#include <cstdint>
#include <string_view>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <imgui.h>

#include <Platform/DynamicLibrary.h>
#include <SDL/SdlDll.h>
#include <Utils/NsStr.h>

// Minimal SDL3 platform backend for ImGui, speaking to the game's own libSDL3 through dlsym'd
// function pointers (no SDL link dependency, mirroring the house pattern of SdlDll +
// PeepEventsHook). Behavior mirrors the parts of imgui_impl_sdl3 that matter here:
//   * processEvent translates key/mouse/text/window events into io.Add*Event calls.
//   * newFrame feeds DisplaySize / DisplayFramebufferScale / DeltaTime and toggles SDL text
//     input on io.WantTextInput.
// Deliberately omitted for now: system cursor shaping (ImGui draws its own cursor while the
// game grabs the mouse), gamepads, and IME area handling - each is a small follow-up against
// the same dlsym table. The clipboard IS wired (real desktop clipboard through the game's SDL3).
namespace gui_sdl
{

// SDL keycode/scancode -> ImGuiKey. Ported from imgui_impl_sdl3 (v1.91.7) so the mapping stays
// canonical; keypad keys are scancode-based (SDL3 has no distinct keycodes for them).
[[nodiscard]] inline ImGuiKey toImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode) noexcept
{
    switch (scancode) {
    case SDL_SCANCODE_KP_0: return ImGuiKey_Keypad0;
    case SDL_SCANCODE_KP_1: return ImGuiKey_Keypad1;
    case SDL_SCANCODE_KP_2: return ImGuiKey_Keypad2;
    case SDL_SCANCODE_KP_3: return ImGuiKey_Keypad3;
    case SDL_SCANCODE_KP_4: return ImGuiKey_Keypad4;
    case SDL_SCANCODE_KP_5: return ImGuiKey_Keypad5;
    case SDL_SCANCODE_KP_6: return ImGuiKey_Keypad6;
    case SDL_SCANCODE_KP_7: return ImGuiKey_Keypad7;
    case SDL_SCANCODE_KP_8: return ImGuiKey_Keypad8;
    case SDL_SCANCODE_KP_9: return ImGuiKey_Keypad9;
    case SDL_SCANCODE_KP_PERIOD: return ImGuiKey_KeypadDecimal;
    case SDL_SCANCODE_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case SDL_SCANCODE_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case SDL_SCANCODE_KP_MINUS: return ImGuiKey_KeypadSubtract;
    case SDL_SCANCODE_KP_PLUS: return ImGuiKey_KeypadAdd;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_KP_EQUALS: return ImGuiKey_KeypadEqual;
    default: break;
    }

    switch (keycode) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_INSERT: return ImGuiKey_Insert;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_APOSTROPHE: return ImGuiKey_Apostrophe;
    case SDLK_COMMA: return ImGuiKey_Comma;
    case SDLK_MINUS: return ImGuiKey_Minus;
    case SDLK_PERIOD: return ImGuiKey_Period;
    case SDLK_SLASH: return ImGuiKey_Slash;
    case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
    case SDLK_EQUALS: return ImGuiKey_Equal;
    case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
    case SDLK_BACKSLASH: return ImGuiKey_Backslash;
    case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
    case SDLK_GRAVE: return ImGuiKey_GraveAccent;
    case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
    case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
    case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
    case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
    case SDLK_PAUSE: return ImGuiKey_Pause;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_LGUI: return ImGuiKey_LeftSuper;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    case SDLK_RGUI: return ImGuiKey_RightSuper;
    case SDLK_APPLICATION: return ImGuiKey_Menu;
    case SDLK_0: return ImGuiKey_0;
    case SDLK_1: return ImGuiKey_1;
    case SDLK_2: return ImGuiKey_2;
    case SDLK_3: return ImGuiKey_3;
    case SDLK_4: return ImGuiKey_4;
    case SDLK_5: return ImGuiKey_5;
    case SDLK_6: return ImGuiKey_6;
    case SDLK_7: return ImGuiKey_7;
    case SDLK_8: return ImGuiKey_8;
    case SDLK_9: return ImGuiKey_9;
    case SDLK_A: return ImGuiKey_A;
    case SDLK_B: return ImGuiKey_B;
    case SDLK_C: return ImGuiKey_C;
    case SDLK_D: return ImGuiKey_D;
    case SDLK_E: return ImGuiKey_E;
    case SDLK_F: return ImGuiKey_F;
    case SDLK_G: return ImGuiKey_G;
    case SDLK_H: return ImGuiKey_H;
    case SDLK_I: return ImGuiKey_I;
    case SDLK_J: return ImGuiKey_J;
    case SDLK_K: return ImGuiKey_K;
    case SDLK_L: return ImGuiKey_L;
    case SDLK_M: return ImGuiKey_M;
    case SDLK_N: return ImGuiKey_N;
    case SDLK_O: return ImGuiKey_O;
    case SDLK_P: return ImGuiKey_P;
    case SDLK_Q: return ImGuiKey_Q;
    case SDLK_R: return ImGuiKey_R;
    case SDLK_S: return ImGuiKey_S;
    case SDLK_T: return ImGuiKey_T;
    case SDLK_U: return ImGuiKey_U;
    case SDLK_V: return ImGuiKey_V;
    case SDLK_W: return ImGuiKey_W;
    case SDLK_X: return ImGuiKey_X;
    case SDLK_Y: return ImGuiKey_Y;
    case SDLK_Z: return ImGuiKey_Z;
    case SDLK_F1: return ImGuiKey_F1;
    case SDLK_F2: return ImGuiKey_F2;
    case SDLK_F3: return ImGuiKey_F3;
    case SDLK_F4: return ImGuiKey_F4;
    case SDLK_F5: return ImGuiKey_F5;
    case SDLK_F6: return ImGuiKey_F6;
    case SDLK_F7: return ImGuiKey_F7;
    case SDLK_F8: return ImGuiKey_F8;
    case SDLK_F9: return ImGuiKey_F9;
    case SDLK_F10: return ImGuiKey_F10;
    case SDLK_F11: return ImGuiKey_F11;
    case SDLK_F12: return ImGuiKey_F12;
    case SDLK_F13: return ImGuiKey_F13;
    case SDLK_F14: return ImGuiKey_F14;
    case SDLK_F15: return ImGuiKey_F15;
    case SDLK_F16: return ImGuiKey_F16;
    case SDLK_F17: return ImGuiKey_F17;
    case SDLK_F18: return ImGuiKey_F18;
    case SDLK_F19: return ImGuiKey_F19;
    case SDLK_F20: return ImGuiKey_F20;
    case SDLK_F21: return ImGuiKey_F21;
    case SDLK_F22: return ImGuiKey_F22;
    case SDLK_F23: return ImGuiKey_F23;
    case SDLK_F24: return ImGuiKey_F24;
    default: return ImGuiKey_None;
    }
}

inline void updateKeyModifiers(SDL_Keymod keyMods) noexcept
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (keyMods & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (keyMods & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (keyMods & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (keyMods & SDL_KMOD_GUI) != 0);
}

// Runtime-resolved SDL entry points (the game's libSDL3).
struct Functions {
    bool (*getWindowMouseGrab)(SDL_Window*) = nullptr;
    bool (*setWindowMouseGrab)(SDL_Window*, bool) = nullptr;
    bool (*getWindowRelativeMouseMode)(SDL_Window*) = nullptr;
    bool (*setWindowRelativeMouseMode)(SDL_Window*, bool) = nullptr;
    std::uint32_t (*getMouseState)(float*, float*) = nullptr;
    sdl3::SDL_PeepEvents* peepEvents = nullptr;
    const char* (*getCurrentVideoDriver)() = nullptr;
    std::uint64_t (*getPerformanceCounter)() = nullptr;
    std::uint64_t (*getPerformanceFrequency)() = nullptr;
    SDL_DisplayID (*getDisplayForWindow)(SDL_Window*) = nullptr;
    const SDL_DisplayMode* (*getCurrentDisplayMode)(SDL_DisplayID) = nullptr;
    SDL_Window* (*getWindowFromID)(SDL_WindowID) = nullptr;
    SDL_WindowID (*getWindowID)(SDL_Window*) = nullptr;
    SDL_Window* (*getKeyboardFocus)() = nullptr;
    SDL_Window* (*getMouseFocus)() = nullptr;
    bool (*getWindowSize)(SDL_Window*, int*, int*) = nullptr;
    bool (*getWindowSizeInPixels)(SDL_Window*, int*, int*) = nullptr;
    SDL_WindowFlags (*getWindowFlags)(SDL_Window*) = nullptr;
    bool (*textInputActive)(SDL_Window*) = nullptr;
    bool (*startTextInput)(SDL_Window*) = nullptr;
    bool (*stopTextInput)(SDL_Window*) = nullptr;
    char* (*getClipboardText)() = nullptr;
    bool (*setClipboardText)(const char*) = nullptr;
    void (*free)(void*) = nullptr;
};

inline Functions functions;

// Resolved once in resolveFunctions; a namespace-level global instead of a function-local
// static because a runtime-initialized static would need __cxa_guard_* which the freestanding
// build does not provide.
inline std::uint64_t performanceFrequency = 0;
inline const char* videoDriver = nullptr;
inline bool videoDriverResolved = false;

[[nodiscard]] inline bool resolveFunctions() noexcept
{
    if (functions.getWindowSize)
        return true;

    const SdlDll sdl;
    if (!static_cast<bool>(sdl))
        return false;

    functions.getWindowMouseGrab = sdl.getFunctionAddress("SDL_GetWindowMouseGrab").as<decltype(functions.getWindowMouseGrab)>();
    functions.setWindowMouseGrab = sdl.getFunctionAddress("SDL_SetWindowMouseGrab").as<decltype(functions.setWindowMouseGrab)>();
    functions.getWindowRelativeMouseMode = sdl.getFunctionAddress("SDL_GetWindowRelativeMouseMode").as<decltype(functions.getWindowRelativeMouseMode)>();
    functions.setWindowRelativeMouseMode = sdl.getFunctionAddress("SDL_SetWindowRelativeMouseMode").as<decltype(functions.setWindowRelativeMouseMode)>();
    functions.getMouseState = sdl.getFunctionAddress("SDL_GetMouseState").as<decltype(functions.getMouseState)>();
    functions.peepEvents = sdl.getFunctionAddress("SDL_PeepEvents").as<decltype(functions.peepEvents)>();
    functions.getCurrentVideoDriver = sdl.getFunctionAddress("SDL_GetCurrentVideoDriver").as<decltype(functions.getCurrentVideoDriver)>();
    functions.getPerformanceCounter = sdl.getFunctionAddress("SDL_GetPerformanceCounter").as<decltype(functions.getPerformanceCounter)>();
    functions.getPerformanceFrequency = sdl.getFunctionAddress("SDL_GetPerformanceFrequency").as<decltype(functions.getPerformanceFrequency)>();
    if (functions.getPerformanceFrequency)
        performanceFrequency = functions.getPerformanceFrequency();
    functions.getDisplayForWindow = sdl.getFunctionAddress("SDL_GetDisplayForWindow").as<decltype(functions.getDisplayForWindow)>();
    functions.getCurrentDisplayMode = sdl.getFunctionAddress("SDL_GetCurrentDisplayMode").as<decltype(functions.getCurrentDisplayMode)>();
    functions.getWindowFromID = sdl.getFunctionAddress("SDL_GetWindowFromID").as<decltype(functions.getWindowFromID)>();
    functions.getWindowID = sdl.getFunctionAddress("SDL_GetWindowID").as<decltype(functions.getWindowID)>();
    functions.getKeyboardFocus = sdl.getFunctionAddress("SDL_GetKeyboardFocus").as<decltype(functions.getKeyboardFocus)>();
    functions.getMouseFocus = sdl.getFunctionAddress("SDL_GetMouseFocus").as<decltype(functions.getMouseFocus)>();
    functions.getWindowSize = sdl.getFunctionAddress("SDL_GetWindowSize").as<decltype(functions.getWindowSize)>();
    functions.getWindowSizeInPixels = sdl.getFunctionAddress("SDL_GetWindowSizeInPixels").as<decltype(functions.getWindowSizeInPixels)>();
    functions.getWindowFlags = sdl.getFunctionAddress("SDL_GetWindowFlags").as<decltype(functions.getWindowFlags)>();
    functions.textInputActive = sdl.getFunctionAddress("SDL_TextInputActive").as<decltype(functions.textInputActive)>();
    functions.startTextInput = sdl.getFunctionAddress("SDL_StartTextInput").as<decltype(functions.startTextInput)>();
    functions.stopTextInput = sdl.getFunctionAddress("SDL_StopTextInput").as<decltype(functions.stopTextInput)>();
    functions.getClipboardText = sdl.getFunctionAddress("SDL_GetClipboardText").as<decltype(functions.getClipboardText)>();
    functions.setClipboardText = sdl.getFunctionAddress("SDL_SetClipboardText").as<decltype(functions.setClipboardText)>();
    functions.free = sdl.getFunctionAddress("SDL_free").as<decltype(functions.free)>();

    return functions.getWindowSize != nullptr
        && functions.getWindowSizeInPixels != nullptr
        && functions.getPerformanceCounter != nullptr
        && functions.getPerformanceFrequency != nullptr;
}

[[nodiscard]] inline bool isUsingWayland() noexcept
{
    if (!functions.getCurrentVideoDriver)
        return false;
    if (!videoDriverResolved) {
        videoDriver = functions.getCurrentVideoDriver();
        videoDriverResolved = true;
    }
    return videoDriver && std::string_view{videoDriver} == "wayland";
}

// Per-window backend state. There is exactly one game window in practice; the last window
// event wins.
inline SDL_Window* window = nullptr;
inline SDL_WindowID windowId = 0;
inline std::uint64_t time = 0;
inline std::uint32_t mousePendingLeaveFrame = 0;
inline std::uint32_t mouseButtonsDown = 0;
inline bool textInputWasWanted = false;

// Raw scancode/button state for the keybind-capture widget (the config binds store SDL
// scancodes, not layout keycodes, so ImGui's key state alone is not enough). Index ranges:
// scancodeDown[0..255] by SDL scancode; mouseButtonDown[1..5] by SDL button id.
inline bool scancodeDown[256] = {};
inline bool mouseButtonDown[6] = {};



// US-QWERTY character for a scancode (0 = none). The menu's text fields (search, config
// rename, slider edits) are all ASCII, and SDL's own text-event translation is unusable inside
// CS2: the game manages its own XKB keymap, which delivered wrong characters outright (pressed
// B, received 'A') and U+FFFD garbage. Synthesizing from raw scancodes matches the keycaps and
// ignores whatever the game did to the keymap.
[[nodiscard]] inline char scancodeToChar(SDL_Scancode scancode, bool shift) noexcept
{
    const int s = static_cast<int>(scancode);
    if (s >= 4 && s <= 29) { // SDL_SCANCODE_A .. SDL_SCANCODE_Z
        return static_cast<char>((shift ? 'A' : 'a') + (s - 4));
    }
    if (s >= 30 && s <= 39) { // SDL_SCANCODE_1 .. SDL_SCANCODE_0
        constexpr char digits[10] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' };
        constexpr char shifted[10] = { '!', '@', '#', '$', '%', '^', '&', '*', '(', ')' };
        return shift ? shifted[s - 30] : digits[s - 30];
    }
    switch (s) {
    case SDL_SCANCODE_SPACE: return ' ';
    case SDL_SCANCODE_MINUS: return shift ? '_' : '-';
    case SDL_SCANCODE_EQUALS: return shift ? '+' : '=';
    case SDL_SCANCODE_LEFTBRACKET: return shift ? '{' : '[';
    case SDL_SCANCODE_RIGHTBRACKET: return shift ? '}' : ']';
    case SDL_SCANCODE_BACKSLASH: return shift ? '|' : '\\';
    case SDL_SCANCODE_SEMICOLON: return shift ? ':' : ';';
    case SDL_SCANCODE_APOSTROPHE: return shift ? '"' : '\'';
    case SDL_SCANCODE_GRAVE: return shift ? '~' : '`';
    case SDL_SCANCODE_COMMA: return shift ? '<' : ',';
    case SDL_SCANCODE_PERIOD: return shift ? '>' : '.';
    case SDL_SCANCODE_SLASH: return shift ? '?' : '/';
    default: return 0;
    }
}

inline void processEvent(const SDL_Event* event) noexcept
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.windowID != windowId) return;
        io.AddMousePosEvent(event->motion.x, event->motion.y);
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event->wheel.windowID != windowId) return;
        io.AddMouseWheelEvent(-event->wheel.x, event->wheel.y);
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.windowID != windowId) return;
        int mouseButton = -1;
        if (event->button.button == SDL_BUTTON_LEFT) mouseButton = 0;
        if (event->button.button == SDL_BUTTON_RIGHT) mouseButton = 1;
        if (event->button.button == SDL_BUTTON_MIDDLE) mouseButton = 2;
        if (event->button.button == SDL_BUTTON_X1) mouseButton = 3;
        if (event->button.button == SDL_BUTTON_X2) mouseButton = 4;
        if (mouseButton == -1) return;
        const bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        // Deliberately NOT fed to ImGui here. Buttons are polled live every frame in newFrame,
        // which is the single source of truth: the event stream trails the poll by an
        // unbounded number of frames (the game thread's SDLHook_PeepEvents batch cadence), and
        // a stale up flushed after the next press's polled down made ImGui see
        // down->up->down - a phantom second click that instantly re-toggled every latching
        // control (account bar, config chip, Visuals expand: "opens then disappears").
        // Position is position-atomic, wheel has no polled API - buttons are the redundant one.
        mouseButtonsDown = down ? (mouseButtonsDown | (1u << mouseButton)) : (mouseButtonsDown & ~(1u << mouseButton));
        if (event->button.button >= 1 && event->button.button <= 5)
            mouseButtonDown[event->button.button] = down;
        return;
    }
    case SDL_EVENT_TEXT_INPUT:
        // Deliberately NOT fed to ImGui anymore - see scancodeToChar. The game's text-event
        // translation is broken inside CS2's window; text comes from key-down synthesis.
        return;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (event->key.windowID != windowId) return;
        updateKeyModifiers(static_cast<SDL_Keymod>(event->key.mod));
        const ImGuiKey key = toImGuiKey(event->key.key, static_cast<SDL_Scancode>(event->key.scancode));
        io.AddKeyEvent(key, event->type == SDL_EVENT_KEY_DOWN);
        const auto scancode = static_cast<int>(event->key.scancode);
        if (scancode >= 0 && scancode < 256)
            scancodeDown[scancode] = event->type == SDL_EVENT_KEY_DOWN;
        // Text input, our way (see scancodeToChar): synthesize ASCII from raw scancodes on
        // key-down whenever a text field wants input. SDL's own text events are unusable here.
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && io.WantTextInput) {
            if (const char c = scancodeToChar(event->key.scancode, (event->key.mod & SDL_KMOD_SHIFT) != 0)) {
                const char text[2] = { c, 0 };
                io.AddInputCharactersUTF8(text);
            }
        }
        return;
    }
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        if (event->window.windowID != windowId) return;
        mousePendingLeaveFrame = 0;
        return;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        if (event->window.windowID != windowId) return;
        // Delayed by one frame, mirroring imgui_impl_sdl3 (issue #5012 - the leave event may
        // arrive while a drag is in progress and would clear the mouse position mid-operation).
        mousePendingLeaveFrame = ImGui::GetFrameCount() + 1;
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event->window.windowID != windowId) return;
        io.AddFocusEvent(event->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
        return;
    default:
        return;
    }
}

// --- mouse mode management -----------------------------------------------------
//
// CS2 plays with the mouse grabbed and in relative mode (FPS camera). Relative mode makes
// SDL motion events carry deltas, which ImGui would apply as absolute positions - the cursor
// "crawls" at a fraction of real speed and dragging feels like 300ms ping. While the menu is
// open we release grab + relative mode so motion events carry real coordinates, and restore
// the game's mode when it closes. Idempotent, present-thread only.

inline int savedMouseGrab = -1;      // -1 = unchanged (0/1 = SDL_bool state when saved)
inline bool savedRelativeMode = false;

inline void updateMouseMode(bool menuOpen) noexcept
{
    if (!window || !functions.setWindowMouseGrab || !functions.setWindowRelativeMouseMode)
        return;

    if (menuOpen) {
        if (savedMouseGrab == -1 && functions.getWindowMouseGrab) {
            savedMouseGrab = functions.getWindowMouseGrab(window) ? 1 : 0;
            savedRelativeMode = functions.getWindowRelativeMouseMode ? functions.getWindowRelativeMouseMode(window) : false;
            functions.setWindowMouseGrab(window, false);
            functions.setWindowRelativeMouseMode(window, false);
        }
    } else if (savedMouseGrab != -1) {
        functions.setWindowMouseGrab(window, savedMouseGrab != 0);
        functions.setWindowRelativeMouseMode(window, savedRelativeMode);
        savedMouseGrab = -1;
    }
}

// Live SDL mouse-button bitmask for the keybind capture (mirrors the Panorama capture, which
// polled MouseState::isButtonDown instead of events - gameplay buttons are not guaranteed to
// travel through the event queue).
[[nodiscard]] inline std::uint32_t liveMouseMask() noexcept
{
    if (!functions.getMouseState)
        return 0;
    return functions.getMouseState(nullptr, nullptr);
}

[[nodiscard]] inline bool anyInputHeld() noexcept
{
    for (bool down : scancodeDown)
        if (down)
            return true;
    return liveMouseMask() != 0;
}


// --- real desktop clipboard ----------------------------------------------------------------
//
// ImGui's Ctrl+C/Ctrl+V routed through the game's own SDL3, whose clipboard IS the desktop
// session clipboard (X11/XWayland/Wayland all bridge it through the compositor). Without this,
// ImGui used its internal per-context buffer - "the in-game clipboard" that only worked between
// our own fields. SDL_GetClipboardText returns an SDL_malloc'd string owned by the caller: the
// thunk caches it and frees the PREVIOUS cache on the next call (ImGui copies the text out
// before the next clipboard read, so nothing can still reference the freed buffer).
inline char* clipboardCache = nullptr;

inline const char* getClipboardTextThunk(void*) noexcept
{
    if (clipboardCache && functions.free)
        functions.free(clipboardCache);
    clipboardCache = functions.getClipboardText ? functions.getClipboardText() : nullptr;
    return clipboardCache ? clipboardCache : "";
}

inline void setClipboardTextThunk(void*, const char* text) noexcept
{
    if (functions.setClipboardText)
        functions.setClipboardText(text);
}

// Returns true once a window was captured and the backend is usable.
inline bool initForWindow(SDL_Window* w) noexcept
{
    if (!w || !functions.getWindowID)
        return false;
    window = w;
    windowId = functions.getWindowID(w);
    ImGuiIO& io = ImGui::GetIO();
    // ImGui keeps the pointer across frames, so the plaintext must outlive this function.
    // Static POD storage, re-decrypted per call (a 17-byte XOR - and a function-scope
    // non-trivial static is not an option: its guard machinery __cxa_guard_* is unavailable
    // under -nostdlib). No literal initializer here - that would put the plaintext in .rodata.
    constexpr ::ns_str::Encrypted<sizeof("neversnooze_sdl3")> backendNameEnc("neversnooze_sdl3");
    static char backendNameBuf[backendNameEnc.decrypted_size()];
    backendNameEnc.decrypt(backendNameBuf);
    io.BackendPlatformName = backendNameBuf;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    if (functions.getClipboardText && functions.setClipboardText) {
        io.GetClipboardTextFn = &getClipboardTextThunk;
        io.SetClipboardTextFn = &setClipboardTextThunk;
        io.ClipboardUserData = nullptr;
    }
    return true;
}

inline void newFrame(bool menuOpen) noexcept
{
    ImGuiIO& io = ImGui::GetIO();

    // Live mouse position AND buttons every frame (the official ImGui SDL backend polls the
    // same state in UpdateMouseData): relying on queued motion/button events made the cursor
    // lag and even dropped click/release events under high-report-rate motion floods.
    if (functions.getMouseState) {
        float mx = 0.0f, my = 0.0f;
        const std::uint32_t buttons = functions.getMouseState(&mx, &my);
        if (mx >= 0.0f && my >= 0.0f)
            io.AddMousePosEvent(mx, my);
        for (int button = 0; button < 5; ++button)
            io.AddMouseButtonEvent(button, (buttons & (1u << button)) != 0);
    }

    // Wheel has no polled-state API - it is event-only in SDL, and the hook path only gets to
    // see events the GAME consumed in its own poll batches. When those batches miss wheel
    // (narrow poll ranges, full batch budgets), wheel events pile up unconsumed and the menu
    // cannot scroll until some later batch drains them - the "scroll dead for seconds" glitch.
    // While the menu is open the game's input is swallowed anyway, so drain wheel straight out
    // of SDL's queue here: SDL_PeepEvents takes the event lock itself (thread-safe), and
    // consumption stays exclusive with the game's poll, so no event is ever delivered twice.
    // When the menu is closed we do not drain - weapon switching keeps its wheel.
    if (menuOpen && functions.peepEvents) {
        SDL_Event wheel[16];
        const int drained = functions.peepEvents(wheel, 16, SDL_GETEVENT, SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_MOUSE_WHEEL);
        for (int i = 0; i < drained; ++i)
            io.AddMouseWheelEvent(-wheel[i].wheel.x, wheel[i].wheel.y);
    }

    int w = 0, h = 0;
    int displayW = 0, displayH = 0;
    functions.getWindowSize(window, &w, &h);
    if (functions.getWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        w = h = 0;
    functions.getWindowSizeInPixels(window, &displayW, &displayH);
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    if (w > 0 && h > 0)
        io.DisplayFramebufferScale = ImVec2(static_cast<float>(displayW) / w, static_cast<float>(displayH) / h);

    // (Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value;
    // happens in VMs - same workaround as imgui_impl_sdl3.)
    const std::uint64_t frequency = performanceFrequency;
    std::uint64_t currentTime = functions.getPerformanceCounter();
    if (currentTime <= time)
        currentTime = time + 1;
    // Clamp: a stall (map load, alt-tab) or a broken counter must not feed a huge or NaN dt
    // into the frame - unclamped values poisoned every motion animation and garbled the menu.
    // !(dt > 0) also catches NaN.
    float deltaTime = time > 0 ? static_cast<float>(static_cast<double>(currentTime - time) / static_cast<double>(frequency)) : 1.0f / 60.0f;
    if (!(deltaTime > 0.0f) || deltaTime > 0.1f)
        deltaTime = 1.0f / 60.0f;
    io.DeltaTime = deltaTime;
    time = currentTime;

    if (mousePendingLeaveFrame != 0 && mousePendingLeaveFrame >= static_cast<std::uint32_t>(ImGui::GetFrameCount()) && mouseButtonsDown == 0) {
        mousePendingLeaveFrame = 0;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    // Text input lifecycle: SDL only produces SDL_EVENT_TEXT_INPUT while text input is active.
    if (io.WantTextInput && !textInputWasWanted && functions.startTextInput)
        functions.startTextInput(window);
    else if (!io.WantTextInput && textInputWasWanted && functions.stopTextInput)
        functions.stopTextInput(window);
    textInputWasWanted = io.WantTextInput;
}

}
