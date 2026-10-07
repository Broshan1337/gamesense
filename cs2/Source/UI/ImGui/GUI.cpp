#include "GUI.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <mutex>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan_core.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <Platform/Macros/FunctionAttributes.h>
#include <Utils/CrashLogger.h>
#include <Utils/SpinLock.h>
#include <Utils/StatusReport.h>
#include <vac_hook.h>

#include "ImGuiMemAllocBridge.h"
#include "GuiLog.h"
#include "SdlImGuiBackend.h"
#include "SelfIntegrity.h"
#include "Theme.h"
#include "Neverlose/Neverlose.h"

namespace
{







constexpr int kEventQueueCapacity = 256;

SpinLock eventQueueLock;
constinit SDL_Event eventQueue[kEventQueueCapacity]{};
int eventQueueHead = 0; 
int eventQueueCount = 0;



int queueWheelQueued = 0;
int queueWheelDropped = 0;
int queueHighWater = 0;

inline void queueEvent(const SDL_Event& event) noexcept
{
    const std::lock_guard guard{eventQueueLock};

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
        ++queueWheelQueued;

    
    
    if (event.type == SDL_EVENT_MOUSE_MOTION && eventQueueCount > 0) {
        auto& last = eventQueue[(eventQueueHead + eventQueueCount - 1) % kEventQueueCapacity];
        if (last.type == SDL_EVENT_MOUSE_MOTION) {
            last = event;
            return;
        }
    }

    if (eventQueueCount == kEventQueueCapacity) {
        
        
        if (eventQueue[eventQueueHead].type == SDL_EVENT_MOUSE_WHEEL)
            ++queueWheelDropped;
        eventQueueHead = (eventQueueHead + 1) % kEventQueueCapacity;
        --eventQueueCount;
    }
    eventQueue[(eventQueueHead + eventQueueCount) % kEventQueueCapacity] = event;
    ++eventQueueCount;
    if (eventQueueCount > queueHighWater)
        queueHighWater = eventQueueCount;
}

inline void flushEvents() noexcept
{
    SDL_Event batch[kEventQueueCapacity];
    int count = 0;
    int wheelQueued = 0, wheelDropped = 0, highWater = 0;

    {
        const std::lock_guard guard{eventQueueLock};
        for (; count < eventQueueCount; ++count)
            batch[count] = eventQueue[(eventQueueHead + count) % kEventQueueCapacity];
        eventQueueHead = (eventQueueHead + eventQueueCount) % kEventQueueCapacity;
        eventQueueCount = 0;
        wheelQueued = queueWheelQueued;
        queueWheelQueued = 0;
        wheelDropped = queueWheelDropped;
        queueWheelDropped = 0;
        highWater = queueHighWater;
        queueHighWater = 0;
    }

    
    if (wheelDropped > 0)
        gui_log::write("[perf] input queue overflow: dropped %d wheel event(s) (high-water %d, %d wheel queued)", wheelDropped, highWater, wheelQueued);
    else if (highWater >= 32)
        gui_log::write("[perf] input queue high-water %d (%d wheel queued)", highWater, wheelQueued);

    for (int i = 0; i < count; ++i)
        gui_sdl::processEvent(&batch[i]);
}



inline std::atomic<bool> initialized{false};
inline std::atomic<bool> menuOpen{false}; 
inline std::atomic<bool> backendReady{false};
inline std::atomic<bool> backendInitStarted{false};
inline std::atomic<bool> unloadRequested{false};
inline bool loggedFirstPoll = false;
inline bool loggedFirstRender = false;
inline int noWindowLogs = 0;

[[NOINLINE]] void createFont() noexcept;



inline void tryInitBackend(SDL_Window* window) noexcept
{
    bool expected = false;
    if (!backendInitStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    if (gui_sdl::initForWindow(window)) {
        createFont();
        backendReady.store(true, std::memory_order_release);
        gui_log::write("window captured: %p (backend ready)", static_cast<void*>(window));
    } else {
        backendInitStarted.store(false, std::memory_order_release); 
    }
}


constexpr float kAlphaDecay = 45.0f; 
constexpr float kAlphaEpsilon = 0.001f;









inline std::uint64_t lastRenderStart = 0;
inline std::uint64_t lastPerfLog = 0;
inline int vtxHighWater = 0;

[[nodiscard]] inline std::uint64_t perfNow() noexcept
{
    return gui_sdl::functions.getPerformanceCounter ? gui_sdl::functions.getPerformanceCounter() : 0;
}

[[nodiscard]] inline double perfMs(std::uint64_t from, std::uint64_t to) noexcept
{
    if (gui_sdl::performanceFrequency == 0 || to <= from)
        return 0.0;
    return static_cast<double>(to - from) * 1000.0 / static_cast<double>(gui_sdl::performanceFrequency);
}

[[nodiscard]] inline bool perfLogDue(std::uint64_t now) noexcept
{
    if (lastPerfLog != 0 && now - lastPerfLog < gui_sdl::performanceFrequency / 4)
        return false;
    lastPerfLog = now;
    return true;
}

constexpr float expDecay(float current, float target, float decay, float dt) noexcept
{
    return target + (current - target) * std::exp(-decay * dt);
}


[[nodiscard]] bool isMenuToggleKey(const SDL_Event& event) noexcept
{
    return event.key.key == SDLK_INSERT || (event.key.mod == SDL_KMOD_LALT && event.key.key == SDLK_I);
}
















inline bool gameKeyDown[256] = {};
inline bool gameMouseDown[6] = {};

void synthesizeMenuCloseInput() noexcept
{
    if (!gui_sdl::functions.peepEvents)
        return;

    SDL_Event up;
    for (int scancode = 0; scancode < 256; ++scancode) {
        if (!gameKeyDown[scancode])
            continue;
        gameKeyDown[scancode] = false;
        std::memset(&up, 0, sizeof(up));
        up.type = SDL_EVENT_KEY_UP;
        up.key.windowID = gui_sdl::windowId;
        up.key.scancode = static_cast<SDL_Scancode>(scancode);
        up.key.down = false;
        gui_sdl::functions.peepEvents(&up, 1, SDL_ADDEVENT, 0, 0);
    }
    for (int button = 1; button <= 5; ++button) {
        if (!gameMouseDown[button])
            continue;
        gameMouseDown[button] = false;
        std::memset(&up, 0, sizeof(up));
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        up.button.windowID = gui_sdl::windowId;
        up.button.button = static_cast<std::uint8_t>(button);
        up.button.down = false;
        up.button.clicks = 1;
        gui_sdl::functions.peepEvents(&up, 1, SDL_ADDEVENT, 0, 0);
    }
}



[[NOINLINE]] void createFont() noexcept
{
    neverlose::loadFonts();
}

} 

bool GUI::init() noexcept
{
    if (initialized.load(std::memory_order_acquire))
        return true;

    gui_log::write("init: begin");

    
    
    
    
    {
        char unloadRequestPath[192];
        if (ns_paths::join(unloadRequestPath, sizeof(unloadRequestPath), "ns_unload_request"))
            ::unlink(unloadRequestPath);
        ::unlink("/tmp/ns_unload_request");
    }

    if (!gui_sdl::resolveFunctions()) {
        StatusReport::record("GUI: SDL3 function resolution failed - menu disabled", false);
        gui_log::write("init: FAILED - SDL3 function resolution");
        return false;
    }
    gui_log::write("window system: %s", gui_sdl::isUsingWayland() ? "wayland" : "x11/other");

    if (!ImGuiMemAllocBridge::install()) {
        StatusReport::record("GUI: IMemAlloc bridge failed - menu disabled", false);
        gui_log::write("init: FAILED - IMemAlloc bridge");
        return false;
    }
    gui_log::write("init: MemAlloc bridge installed (alloc slot %#zx, free slot %#zx)", ImGuiMemAllocBridge::allocSlotOffset, ImGuiMemAllocBridge::freeSlotOffset);

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "imgui_impl_vulkan";

      gui_theme::apply();

      initialized.store(true, std::memory_order_release);
      gui_log::write("init: ImGui context created");
      StatusReport::record("GUI: ImGui context (allocations bridged to CS2 IMemAlloc)", true);
      
      return true;
}

void GUI::destroy() noexcept
{
    if (!initialized.exchange(false, std::memory_order_acq_rel))
        return;

    backendReady.store(false, std::memory_order_release);

    if (ImGui::GetCurrentContext())
        ImGui::DestroyContext();
}

bool GUI::isMenuOpen() noexcept
{
    return menuOpen.load(std::memory_order_acquire);
}

void GUI::hideMenuNow() noexcept
{
    if (!menuOpen.exchange(false, std::memory_order_acq_rel))
        return;
    gui_log::write("menu CLOSE via escape/delete");
    synthesizeMenuCloseInput(); 
}

bool GUI::isInitialized() noexcept
{
    return initialized.load(std::memory_order_acquire);
}

bool GUI::polledEvents(const SDL_Event* events, int count) noexcept
{
    if (!initialized.load(std::memory_order_acquire))
        return false;

    if (!loggedFirstPoll) {
        loggedFirstPoll = true;
        gui_log::write("polledEvents: first batch, %d event(s)", count);
    }

    bool sawToggleKey = false;

    for (int i = 0; i < count; ++i) {
        const SDL_Event& event = events[i];

        
        if (event.type < SDL_EVENT_KEY_DOWN || event.type > SDL_EVENT_DROP_POSITION)
            continue;
        if (event.type == SDL_EVENT_USER)
            continue;

        
        
        
        if (event.type == SDL_EVENT_KEY_DOWN) {
            const int key = static_cast<int>(event.key.key);
            if (key == SDLK_INSERT || key == SDLK_ESCAPE || key == SDLK_DELETE || key == SDLK_I)
                gui_log::write("key down: key=%d scancode=%d mod=0x%x menuOpen=%d",
                    key, static_cast<int>(event.key.scancode), static_cast<int>(event.key.mod),
                    menuOpen.load(std::memory_order_acquire) ? 1 : 0);
        }

        if (event.type == SDL_EVENT_KEY_DOWN && isMenuToggleKey(event))
            sawToggleKey = true;

        queueEvent(event);
    }

    bool menuWasOpen = false;
    if (sawToggleKey) {
        menuWasOpen = menuOpen.load(std::memory_order_acquire);
        menuOpen.store(!menuWasOpen, std::memory_order_release);
        gui_log::write("menu %s via INSERT", menuWasOpen ? "CLOSE" : "OPEN");
        if (menuWasOpen)
            synthesizeMenuCloseInput(); 
    }

    
    
    
    
    if (!menuOpen.load(std::memory_order_acquire)) {
        for (int i = 0; i < count; ++i) {
            const SDL_Event& event = events[i];
            switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const auto scancode = static_cast<int>(event.key.scancode);
                if (scancode >= 0 && scancode < 256)
                    gameKeyDown[scancode] = event.type == SDL_EVENT_KEY_DOWN;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button >= 1 && event.button.button <= 5)
                    gameMouseDown[event.button.button] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                break;
            default:
                break;
            }
        }
    }

    if (!backendReady.load(std::memory_order_acquire)) {
        
        
        
        for (int i = 0; i < count; ++i) {
            const SDL_Event& event = events[i];
            if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
                if (SDL_Window* window = gui_sdl::functions.getWindowFromID(event.window.windowID))
                    tryInitBackend(window);
                break;
            }
        }
        if (!backendReady.load(std::memory_order_acquire) && !backendInitStarted.load(std::memory_order_acquire) && noWindowLogs < 3) {
            ++noWindowLogs;
            gui_log::write("polledEvents: no window captured yet (batch had no window events)");
        }
    }

    return menuOpen.load(std::memory_order_acquire);
}

void GUI::requestUnload() noexcept
{
    unloadRequested.store(true, std::memory_order_release);
}

bool GUI::consumeUnloadRequest() noexcept
{
    return unloadRequested.exchange(false, std::memory_order_acq_rel);
}

bool GUI::render(VkCommandBuffer commandBuffer) noexcept
{
    if (!initialized.load(std::memory_order_acquire))
        return false;

    const std::uint64_t renderStart = perfNow();
    if (lastRenderStart != 0) {
        const double gapMs = perfMs(lastRenderStart, renderStart);
        if (gapMs > 100.0 && perfLogDue(renderStart))
            gui_log::write("[perf] menu frame gap %.1f ms - present rate collapsed (game-side stall?)", gapMs);
    }
    lastRenderStart = renderStart;

    
    
    self_integrity::tick();

    if (!backendReady.load(std::memory_order_acquire)) {
        
        
        SDL_Window* focused = gui_sdl::functions.getKeyboardFocus ? gui_sdl::functions.getKeyboardFocus() : nullptr;
        if (!focused)
            focused = gui_sdl::functions.getMouseFocus ? gui_sdl::functions.getMouseFocus() : nullptr;
        if (focused) {
            gui_log::write("render: capturing focused window %p", static_cast<void*>(focused));
            tryInitBackend(focused);
        }
        if (!backendReady.load(std::memory_order_acquire))
            return false;
    }

    flushEvents();

    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = isMenuOpen() && gui_sdl::functions.getWindowMouseGrab(gui_sdl::window);

    
    
    gui_sdl::updateMouseMode(isMenuOpen());

    neverlose::processDeferred(); 

    
    {
        static bool menuWasOpen = false;
        if (isMenuOpen() && !menuWasOpen)
            neverlose::beginReveal();
        else if (!isMenuOpen() && menuWasOpen)
            neverlose::beginDismiss();
        menuWasOpen = isMenuOpen();
    }

    ImGui_ImplVulkan_NewFrame();
    CrashLogger::trace(0x201);
    gui_sdl::newFrame(isMenuOpen());
    CrashLogger::trace(0x202);

    
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return false;

    ImGui::NewFrame();
    CrashLogger::trace(0x203);

    const float target = isMenuOpen() ? 1.0f : 0.0f;
    static float alpha = 0.0f; 
    alpha = expDecay(alpha, target, kAlphaDecay, io.DeltaTime);

    if (!isMenuOpen())
        neverlose::cancelKeybindCapture(); 

    
    
    
    neverlose::renderGameOverlay();

    
    neverlose::drawMenuGlow(alpha);

    
    
    if (alpha > kAlphaEpsilon || neverlose::isDismissing()) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        neverlose::render();
        ImGui::PopStyleVar();
    }
    CrashLogger::trace(0x204);

    ImGui::Render();
    CrashLogger::trace(0x205);

    
    
    
    
    
    
    if (const ImDrawData* drawData = ImGui::GetDrawData()) {
        if (vtxHighWater > 0
            && (drawData->TotalVtxCount > vtxHighWater * 2 || drawData->TotalVtxCount > vtxHighWater + 8192)) {
            vtxHighWater = drawData->TotalVtxCount;
            gui_log::write("[perf] vertex high-water %d (jump - oscillating content would resize upload buffers)", vtxHighWater);
        } else if (drawData->TotalVtxCount > vtxHighWater) {
            vtxHighWater = drawData->TotalVtxCount; 
        }
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    CrashLogger::trace(0x206);

    const double frameMs = perfMs(renderStart, perfNow());
    if (frameMs > 4.0 && perfLogDue(perfNow()))
        gui_log::write("[perf] slow menu frame: %.1f ms work", frameMs);

    if (!loggedFirstRender) {
        loggedFirstRender = true;
        gui_log::write("render: first frame done (display %.0fx%.0f, cmd %p)",
            io.DisplaySize.x, io.DisplaySize.y, reinterpret_cast<void*>(commandBuffer));
    }
    return true;
}
