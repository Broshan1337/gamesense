#include "GUI.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdint>
#include <cmath>
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

// ---------------------------------------------------------------------------
// Event queue. Producers: game threads inside SDLHook_PeepEvents. Consumer: the present thread
// in GUI::render (flushed before ImGui::NewFrame). ImGui's io.Add*Event functions are not
// thread-safe against NewFrame, so raw SDL events are staged here - the same reason the donor
// keeps a queue (ocornut/imgui#6895). SDL3 keeps text/editing payload in fixed inline arrays,
// so a plain struct copy is self-contained and allocation-free.
constexpr int kEventQueueCapacity = 256;

SpinLock eventQueueLock;
constinit SDL_Event eventQueue[kEventQueueCapacity]{};
int eventQueueHead = 0; // consumer position
int eventQueueCount = 0;

// Input diagnostics for the glitch hunt (producers update these under the lock; flushEvents
// reads+resets them on the present thread and does the actual logging).
int queueWheelQueued = 0;
int queueWheelDropped = 0;
int queueHighWater = 0;

inline void queueEvent(const SDL_Event& event) noexcept
{
    const std::lock_guard guard{eventQueueLock};

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
        ++queueWheelQueued;

    // Coalesce mouse motion: ImGui only needs the latest position, and high-report-rate mice
    // (500-8000 Hz) would otherwise flood the ring and starve fresher events.
    if (event.type == SDL_EVENT_MOUSE_MOTION && eventQueueCount > 0) {
        auto& last = eventQueue[(eventQueueHead + eventQueueCount - 1) % kEventQueueCapacity];
        if (last.type == SDL_EVENT_MOUSE_MOTION) {
            last = event;
            return;
        }
    }

    if (eventQueueCount == kEventQueueCapacity) {
        // Overrun: drop the OLDEST event. Dropping the newest would keep a rolling window of
        // stale input - the sluggish, lagging cursor seen at high mouse report rates.
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

    // Anomaly-only logging: a healthy session never writes these lines.
    if (wheelDropped > 0)
        gui_log::write("[perf] input queue overflow: dropped %d wheel event(s) (high-water %d, %d wheel queued)", wheelDropped, highWater, wheelQueued);
    else if (highWater >= 32)
        gui_log::write("[perf] input queue high-water %d (%d wheel queued)", highWater, wheelQueued);

    for (int i = 0; i < count; ++i)
        gui_sdl::processEvent(&batch[i]);
}

// ---------------------------------------------------------------------------
// State.
inline std::atomic<bool> initialized{false};
inline std::atomic<bool> menuOpen{false}; // closed by default; INSERT / ALT+I toggles
inline std::atomic<bool> backendReady{false};
inline std::atomic<bool> backendInitStarted{false};
inline std::atomic<bool> unloadRequested{false};
inline bool loggedFirstPoll = false;
inline bool loggedFirstRender = false;
inline int noWindowLogs = 0;

[[NOINLINE]] void createFont() noexcept;

// Boots the SDL side of the backend once - safe to call from any thread, exactly one wins the
// CAS and runs the (idempotent) font + window setup while the others fall through.
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
        backendInitStarted.store(false, std::memory_order_release); // allow a retry with a real window
    }
}

// Same fade the donor uses for menu open/close (b + (a - b) * exp(-decay * dt)).
constexpr float kAlphaDecay = 45.0f; // close fade: ~120ms to invisible (25 left the shell hanging ~300ms)
constexpr float kAlphaEpsilon = 0.001f;

// Present-thread frame diagnostics for the glitch hunt. GUI::render runs inside
// hkQueuePresentKHR, so:
//   * gapMs  - interval since the previous menu frame == the present rate. A burst of large
//              gaps means the GAME stopped/overslowed presenting (game-side stall), not us.
//   * frameMs- time our own frame work took (NewFrame -> RenderDrawData). Spikes here point at
//              our per-frame cost - e.g. upload-buffer resizes inside the ImGui Vulkan backend.
// Anomalies log rate-limited (250 ms), so a multi-second stall shows as a burst of lines whose
// timestamps reveal the true duration. Healthy sessions write nothing.
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

// The menu opens with INSERT; on keyboards without one, ALT+I works too (donor parity).
[[nodiscard]] bool isMenuToggleKey(const SDL_Event& event) noexcept
{
    return event.key.key == SDLK_INSERT || (event.key.mod == SDL_KMOD_LALT && event.key.key == SDLK_I);
}

// Font setup: the Neverlose design ships its own fonts (Inter + FontAwesome), embedded into
// the binary and loaded by the menu layer. No Noto fallback chain needed anymore.
[[NOINLINE]] void createFont() noexcept
{
    neverlose::loadFonts();
}

} // namespace

bool GUI::init() noexcept
{
    if (initialized.load(std::memory_order_acquire))
        return true;

    gui_log::write("init: begin");

    // STALE-REQUEST GUARD: an /tmp/ns_unload_request left over from a PREVIOUS module lifetime
    // (e.g. an unload that failed against an older build) must not unload this fresh instance
    // on its first frame. Wipe it here - anything written AFTER this point is a genuine request.
    ::unlink("/tmp/ns_unload_request");

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

        // Same range the donor feeds to ImGui: keyboard, mouse, text, window and drop events.
        if (event.type < SDL_EVENT_KEY_DOWN || event.type > SDL_EVENT_DROP_POSITION)
            continue;
        if (event.type == SDL_EVENT_USER)
            continue;

        if (event.type == SDL_EVENT_KEY_DOWN && isMenuToggleKey(event))
            sawToggleKey = true;

        queueEvent(event);
    }

    if (sawToggleKey)
        menuOpen.store(!menuOpen.load(std::memory_order_acquire), std::memory_order_release);

    if (!backendReady.load(std::memory_order_acquire)) {
        // Capture the game's main window. Primary path: a window event in this batch (the game
        // window usually exists long before injection, so periodic focus/enter events and the
        // render-thread fallback below both matter).
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

    // self-integrity watchdog (.text checksum drift + attached-tracer detection), internally
    // throttled to one pass every ~15 s - see SelfIntegrity.h
    self_integrity::tick();

    if (!backendReady.load(std::memory_order_acquire)) {
        // Fallback window capture: if no window event was seen yet, take whatever window holds
        // keyboard/mouse focus right now (the game window in practice).
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

    // Release the game's mouse grab + relative mode while the menu is open - relative mode
    // reports deltas as positions and made the cursor crawl.
    gui_sdl::updateMouseMode(isMenuOpen());

    neverlose::processDeferred(); // menu-scale changes: font reload happens outside the frame

    // menu-open/close edges: arm the cinematic reveal / its mirror-image dismissal
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

    // Minimized window: nothing to draw into this frame (0-sized render passes are invalid).
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return false;

    ImGui::NewFrame();
    CrashLogger::trace(0x203);

    const float target = isMenuOpen() ? 1.0f : 0.0f;
    static float alpha = 0.0f; // present-thread only state
    alpha = expDecay(alpha, target, kAlphaDecay, io.DeltaTime);

    if (!isMenuOpen())
        neverlose::cancelKeybindCapture(); // never bind gameplay keys while the menu is closed

    // Game-anchored HUD (hitmarker, player list) draws EVERY frame -
    // it must not disappear with the menu (it did when it lived inside neverlose::render,
    // which is alpha-gated on menu visibility).
    neverlose::renderGameOverlay();

    // The outer glow fades with the shell (menuAlpha), like the shell itself.
    neverlose::drawMenuGlow(alpha);

    // The dismissal keeps calling render() past the alpha fade so its scale/slide transform
    // lands visibly instead of being cut off when alpha hits zero.
    if (alpha > kAlphaEpsilon || neverlose::isDismissing()) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        neverlose::render();
        ImGui::PopStyleVar();
    }
    CrashLogger::trace(0x204);

    ImGui::Render();
    CrashLogger::trace(0x205);

    // Vertex high-water tracking. The ImGui Vulkan backend grows its upload buffers
    // geometrically, so a slowly climbing content max (normal interaction) is irrelevant - it
    // only logs on a DOUBLING or a +8k jump, which is what a resize storm (content size
    // oscillation between two large maxima) produces frame after frame. Healthy sessions write
    // nothing; a storm shows as repeated lines. A one-line note at a brand-new maximum is
    // suppressed unless it doubles - the climb itself is not the anomaly.
    if (const ImDrawData* drawData = ImGui::GetDrawData()) {
        if (vtxHighWater > 0
            && (drawData->TotalVtxCount > vtxHighWater * 2 || drawData->TotalVtxCount > vtxHighWater + 8192)) {
            vtxHighWater = drawData->TotalVtxCount;
            gui_log::write("[perf] vertex high-water %d (jump - oscillating content would resize upload buffers)", vtxHighWater);
        } else if (drawData->TotalVtxCount > vtxHighWater) {
            vtxHighWater = drawData->TotalVtxCount; // track silently until a real jump happens
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
