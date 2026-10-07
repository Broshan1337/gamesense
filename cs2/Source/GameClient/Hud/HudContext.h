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













namespace hud_root_walk
{
struct MappingTable {
    static constexpr std::size_t kMaxMappings = 4096;

    std::uintptr_t listKey = 0; 
    bool valid = false;
    std::uintptr_t listRegionEnd = 0; 

    std::size_t writableCount = 0; 
    std::uintptr_t writableStart[kMaxMappings];
    std::uintptr_t writableEnd[kMaxMappings];
    std::size_t imageCount = 0; 
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
    
    
    
    
    
    
    
    while (std::fgets(line, sizeof(line), maps)) {
        std::uintptr_t start = 0;
        std::uintptr_t end = 0;
        if (std::sscanf(line, "%lx-%lx", &start, &end) != 2)
            continue;
        
        
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
            CrashLogger::trace(0x376); 
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

    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] std::uintptr_t resolveEnginePanelList() noexcept
    {
        const auto engine = hookContext.patternSearchResults().template get<UiEnginePointer>();
        if (!engine || !*engine) {
            CrashLogger::trace(0x37A); 
            return 0;
        }

        const auto engineObject = reinterpret_cast<std::uintptr_t>(*engine);
        
        
        
        
        if (!hud_root_walk::inWritable(engineObject)) {
            if (!hud_root_walk::reparseIfStale() || !hud_root_walk::inWritable(engineObject)) {
                CrashLogger::trace(0x37B); 
                return 0;
            }
        }

        const auto list = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<cs2::CUIPanel***>(engineObject + 0x218));
        if (!list || !hud_root_walk::refreshForList(list)) {
            CrashLogger::trace(0x37C); 
            return 0;
        }
        return list;
    }

    
    
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
