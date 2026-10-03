#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <Utils/CrashLogger.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <MemoryPatterns/PatternTypes/UiEnginePatternTypes.h>
#include <GameClient/Panorama/PanelHandle.h>
#include <GameClient/Panorama/PanoramaUiPanel.h>
#include <CS2/Panorama/CPanel2D.h>
#include <CS2/Panorama/CUIPanel.h>

// 2026-09-26 5GB update: the HUD root ('CSGOHud') has NO static holder anymore - the
// wrapper-global concept is dead (disproven live across ALL writable module ranges, see
// the ClientPatternsLinux.h comment). The root resolves through the panorama engine:
// UiEnginePointer (green, libclient 0x4B9BDD8) -> [engine+0x218] = the global CUIPanel
// slot array (entries at +0x10, stride 0x20: {CUIPanel* panel, u64 serial}); 'CSGOHud'
// = entry 3, CPanel2D round-trip verified live. Everything below is gated on a cached
// /proc/self/maps region snapshot so that any garbage value in the chain (the 18:39
// crash: engine+0x218 read "valid" but the list pointer itself garbage; the walk then
// stepped into unmapped list space = SEGV at 0x73300000008) degrades to a null panel
// instead of faulting. The snapshot is refreshed only when the list identity changes -
// a failed lookup short-circuits without re-parsing (list stays invalid until it is
// reallocated), so there is no per-frame /proc cost on the failure path either.
namespace hud_root_walk
{
struct MappingTable {
    static constexpr std::size_t kMaxMappings = 4096;

    std::uintptr_t listKey = 0; // the list pointer this snapshot was taken for
    bool valid = false;
    std::uintptr_t listRegionEnd = 0; // end of the writable region that holds the list

    std::size_t writableCount = 0; // anon/heap 'rw' regions (panels, lists, strings)
    std::uintptr_t writableStart[kMaxMappings];
    std::uintptr_t writableEnd[kMaxMappings];
    std::size_t imageCount = 0; // libclient/libpanorama/libpanoramauiclient regions (vtables)
    std::uintptr_t imageStart[kMaxMappings];
    std::uintptr_t imageEnd[kMaxMappings];
};

inline MappingTable table;

[[nodiscard]] inline bool inRanges(std::uintptr_t address, const std::uintptr_t* starts, const std::uintptr_t* ends, std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i) {
        if (starts[i] <= address && address < ends[i])
            return true;
    }
    return false;
}

[[nodiscard]] inline bool inWritable(std::uintptr_t address) noexcept
{
    return inRanges(address, table.writableStart, table.writableEnd, table.writableCount);
}

[[nodiscard]] inline bool inImage(std::uintptr_t address) noexcept
{
    return inRanges(address, table.imageStart, table.imageEnd, table.imageCount);
}

// A candidate id string must be NUL-terminated inside its own region before strcmp -
// strcmp itself has no bound and a corrupted string would fault past the region end.
[[nodiscard]] inline bool idStringTerminated(const char* chars) noexcept
{
    for (std::size_t i = 0; i < table.writableCount; ++i) {
        const auto start = table.writableStart[i];
        const auto end = table.writableEnd[i];
        const auto address = reinterpret_cast<std::uintptr_t>(chars);
        if (start <= address && address < end) {
            const auto limit = (address + 128 < end) ? address + 128 : end;
            for (const char* p = chars; reinterpret_cast<std::uintptr_t>(p) < limit; ++p) {
                if (*p == '\0')
                    return true;
            }
            return false;
        }
    }
    return false;
}

// CLOCK_MONOTONIC ms of the last maps parse (CLOCK_MONOTONIC, not ImGui::GetTime - the
// established pattern for this codebase; vdso, safe on any thread).
inline long long lastParseMs = 0;

inline void refresh(std::uintptr_t list) noexcept
{
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        lastParseMs = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    table.listKey = list;
    table.valid = false;
    table.listRegionEnd = 0;
    table.writableCount = 0;
    table.imageCount = 0;

    std::FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps)
        return;
    char line[512];
    // THE 2026-10-03 LESSON: this loop used to stop the moment EITHER counter hit the cap
    // (writable && image < kMaxMappings). The maps are address-sorted, so the game's heap
    // regions come FIRST and the module images LAST - the 5GB update pushed the writable
    // region count past 4096, the loop stopped before ever reaching libclient/libpanorama,
    // imageCount stayed 0, and EVERY panel failed the vmt-in-image gate: the HUD walk
    // rejected all panels and the ESP died silently. Now each category is capped
    // independently and the parse always runs to the end of the file.
    while (std::fgets(line, sizeof(line), maps)) {
        std::uintptr_t start = 0;
        std::uintptr_t end = 0;
        if (std::sscanf(line, "%lx-%lx", &start, &end) != 2)
            continue;
        // perms live in field 2 - strstr on the whole line would let a path containing
        // "rw" masquerade as a writable mapping
        const char* dash = std::strchr(line, '-');
        const char* space = dash ? std::strchr(dash, ' ') : nullptr;
        const bool writable = space && space[1] == 'r' && space[2] == 'w';
        if (writable) {
            if (table.writableCount < MappingTable::kMaxMappings) {
                table.writableStart[table.writableCount] = start;
                table.writableEnd[table.writableCount] = end;
                ++table.writableCount;
            }
        } else if (std::strstr(line, "libclient.so") != nullptr || std::strstr(line, "libpanorama.so") != nullptr || std::strstr(line, "libpanoramauiclient.so") != nullptr) {
            if (table.imageCount < MappingTable::kMaxMappings) {
                table.imageStart[table.imageCount] = start;
                table.imageEnd[table.imageCount] = end;
                ++table.imageCount;
            }
        }
    }
    std::fclose(maps);

    // The list itself must sit inside one of the writable regions we just recorded -
    // a garbage list pointer (the 18:39 crash) fails here and the walk never starts.
    for (std::size_t i = 0; i < table.writableCount; ++i) {
        if (table.writableStart[i] <= list && list < table.writableEnd[i]) {
            table.listRegionEnd = table.writableEnd[i];
            table.valid = true;
            return;
        }
    }
}

[[nodiscard]] inline bool refreshForList(std::uintptr_t list) noexcept
{
    if (table.listKey != list)
        refresh(list);
    return table.valid;
}

// The table bootstrap / staleness escape. 2026-09-27 fix for the 02:06 crash class: the
// engine-object gate below ran BEFORE any refresh could ever populate the table (the table
// was only built inside refreshForList, which sits AFTER that gate) - an empty table made
// inWritable() false on the very first call, the walk returned null forever, and the null
// panel then dereferenced at [nullptr+countOffset] in PanoramaUiPanel::children(). The
// walk had NEVER succeeded once (no 0x376/0x377/0x379 in any trace ring since it shipped).
// This is called on a FAILED gate: it reparses /proc/self/maps at most once per second
// (never per frame), repopulating the writable/image ranges without touching the list
// identity (listKey is set to 0 so the next real list re-validates through refreshForList).
// Self-heals both the first-call bootstrap and boot races where the engine object lands
// in a region mapped after an older snapshot.
[[nodiscard]] inline bool reparseIfStale() noexcept
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const auto now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (now - lastParseMs < 1000)
        return false;
    refresh(0);
    return true;
}

// Walks the engine's CUIPanel slot array for a panel with the wanted id. Live-verified
// layout: entries at +0x10, stride 0x20 = {CUIPanel* panel, u64 serial}; the serial is
// an allocation counter, NOT a validity invariant (the 'Hud' entry carries 0x55f1_0000_0005
// while 'CSGOHud' carries 4) - do NOT filter on it. CUIPanel: vmt@+0, clientPanel@+0x8
// (round-trips: [cpanel2d+8] == the CUIPanel), id@+0x10 (char*), parent@+0x18.
// Every deref below is region-gated first: the entry read is bounded by the list's own
// region end, and a panel pointer must be in a writable region with a plausible
// (module-image-range) vmt BEFORE its clientPanel/round-trip/id are touched.
[[nodiscard]] inline cs2::CUIPanel* findPanelById(std::uintptr_t list, const char* wanted) noexcept
{
    constexpr std::size_t kMaxCandidates = 4096;
    const std::uintptr_t entryLimit = table.listRegionEnd;
    std::size_t candidates = 0;
    for (std::uintptr_t entry = list + 0x10; entry + 0x10 <= entryLimit && candidates < kMaxCandidates; entry += 0x20, ++candidates) {
        const std::uintptr_t panelValue = *reinterpret_cast<const std::uintptr_t*>(entry);
        if (panelValue < 0x10000 || !inWritable(panelValue))
            continue;
        const std::uintptr_t vmt = *reinterpret_cast<const std::uintptr_t*>(panelValue);
        if (vmt < 0x10000 || !inImage(vmt))
            continue;
        const std::uintptr_t clientPanel = *reinterpret_cast<const std::uintptr_t*>(panelValue + 0x8);
        if (clientPanel < 0x10000 || !inWritable(clientPanel))
            continue;
        if (*reinterpret_cast<const std::uintptr_t*>(clientPanel + 0x8) != panelValue)
            continue;
        const char* idChars = *reinterpret_cast<const char* const*>(panelValue + 0x10);
        if (!inWritable(reinterpret_cast<std::uintptr_t>(idChars)) || !idStringTerminated(idChars))
            continue;
        if (std::strcmp(idChars, wanted) == 0) {
            CrashLogger::trace(0x376); // CSGOHud root found (0x377 = walk ended null)
            return reinterpret_cast<cs2::CUIPanel*>(panelValue);
        }
    }
    CrashLogger::trace(0x377);
    return nullptr;
}
}

template <typename HookContext>
struct HudContext {
    explicit HudContext(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] auto panel() noexcept
    {
        return hookContext.template make<PanoramaUiPanel>(hud_root_walk::findPanelById(resolveEnginePanelList(), "CSGOHud"));
    }

    // Resolves the engine's CUIPanel slot array through the same gates panel() uses
    // (UiEnginePointer -> the object -> the writable-region gate -> the list at +0x218).
    // Returns 0 on any failure; callers treat that as "no panel".
    //
    // THE 5GB 2026-09-25 UPDATE REORGANIZED THE PANEL TREE: 'HudInWorld' no longer appears
    // in its parent's children array (live-verified: 'HudReticle' is now a DIRECT child of
    // 'Hud' at index 2, while 'HudInWorld' keeps a valid parent pointer but is absent from
    // the array - hidden panels are dropped from it). Any findChildInLayoutFile walk through
    // 'HudInWorld' therefore returns null forever, which silently killed every in-world ESP
    // panel (createPanel entered with a null parent, 0x378 without 0x379). Panels that are
    // unique per session (HudReticle has exactly one instance) are looked up directly in the
    // slot array instead - that is layout-independent and is what this helper is for.
    [[nodiscard]] std::uintptr_t resolveEnginePanelList() noexcept
    {
        const auto engine = hookContext.patternSearchResults().template get<UiEnginePointer>();
        if (!engine || !*engine) {
            CrashLogger::trace(0x37A); // gate 1: UiEnginePointer null / object null
            return 0;
        }

        const auto engineObject = reinterpret_cast<std::uintptr_t>(*engine);
        // The table can be empty on the first call (or stale after a boot race) - a failed
        // membership check reparses the maps snapshot (rate-limited to 1/s) and retests.
        // This is the 2026-09-27 fix: the gate used to fail permanently on the unpopulated
        // table, so the walk NEVER succeeded and every consumer got a null panel.
        if (!hud_root_walk::inWritable(engineObject)) {
            if (!hud_root_walk::reparseIfStale() || !hud_root_walk::inWritable(engineObject)) {
                CrashLogger::trace(0x37B); // gate 2: engine object not in a writable region
                return 0;
            }
        }

        const auto list = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<cs2::CUIPanel***>(engineObject + 0x218));
        if (!list || !hud_root_walk::refreshForList(list)) {
            CrashLogger::trace(0x37C); // gate 3: panel list null / not in a writable region
            return 0;
        }
        return list;
    }

    // Slot-array lookup for a panel with a unique per-session id. Layout-independent: see
    // the tree-reorganization note on resolveEnginePanelList().
    [[nodiscard]] auto findUniquePanelById(const char* panelId) noexcept
    {
        const auto list = resolveEnginePanelList();
        if (!list)
            return hookContext.template make<PanoramaUiPanel>(nullptr);
        return hookContext.template make<PanoramaUiPanel>(hud_root_walk::findPanelById(list, panelId));
    }

    [[nodiscard]] auto deathNoticesPanelHandle() noexcept
    {
        return hookContext.template make<PanelHandle>(hookContext.hudState().deathNoticesPanelHandle);
    }

    [[nodiscard]] auto scoreAndTimeAndBombPanelHandle() noexcept
    {
        return hookContext.template make<PanelHandle>(hookContext.hudState().scoreAndTimeAndBombPanelHandle);
    }

    [[nodiscard]] auto bombStatusPanelHandle() noexcept
    {
        return hookContext.template make<PanelHandle>(hookContext.hudState().bombStatusPanelHandle);
    }

    [[nodiscard]] auto bombPlantedPanelHandle() noexcept
    {
        return hookContext.template make<PanelHandle>(hookContext.hudState().bombPlantedPanelHandle);
    }

    [[nodiscard]] auto timerTextPanelHandle() noexcept
    {
        return hookContext.template make<PanelHandle>(hookContext.hudState().timerTextPanelHandle);
    }

    void resetBombStatusVisibility() noexcept
    {
        hookContext.bombStatusPanelState().resetVisibility();
    }
    
private:
    HookContext& hookContext;
};
