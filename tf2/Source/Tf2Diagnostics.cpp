#include "Tf2Diagnostics.h"
#include "Tf2Log.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <link.h>

namespace ns_tf2 {

namespace {



const char *const kGameModules[] = {
    "client.so",
    "engine.so",
    "vstdlib.so",
    "tier0.so",
    "materialsystem.so",
    "vguimatsurface.so",
    "vgui2.so",
    "inputsystem.so",
    "shaderapidx9.so",
    "shaderapivk.so",
    "studiorender.so",
    "vphysics.so",
    "datacache.so",
    "filesystem_stdio.so",
    "libsteam_api.so",
    "soundemittersystem.so",
    
    "steamclient.so",
};



bool modulePathMatches(const char *path, const char *name)
{
    const size_t len = std::strlen(name);
    const size_t pathLen = std::strlen(path);
    if (pathLen <= len)
        return false;
    if (std::strcmp(path + pathLen - len, name) != 0)
        return false;
    return path[pathLen - len - 1] == '/';
}



const char *const kInterfaceCandidates[] = {
    
    "VClient017",
    "VClientEntityList003",
    "VClientPrediction001",
    "VClientDllSharedAppSystems001",
    
    "VEngineClient014",
    "VEngineClient013",
    "VEngineServer023",
    "VEngineCvar004",
    "VEngineVGui002",
    "VEngineModel016",
    "VEngineRenderView014",
    "VEngineEffects001",
    "VEngineRandom001",
    "VEngineStringTable001",
    "VEngineClientStringTable001",
    "VEngineServerStringTable001",
    "VEngineShadowMgr002",
    "VModelInfoClient006",
    "VDebugOverlay003",
    "VCvarQuery001",
    "VoiceServer002",
    "VSoundEmitter002",
    
    "VGUI_ivgui008",
    "VGUI_Surface030",
    "VGUI_Panel009",
    "VGUI_Scheme010",
    "VGUI_Input005",
    "VGUI_InputInternal001",
    "VGUI_System010",
    "VGUI_Localize005",
    
    "VMaterialSystem080",
    "VMaterialSystem082",
    "VMaterialSystemConfig002",
    "VStudioRender025",
    "VPhysics031",
    "VPhysicsCollision007",
    "VPhysicsSurfaceProps001",
    
    "VFileSystem022",
    "VDataCache003",
    "VProcessUtils001",
    "VMDLLIB001",
    "VAudio002",
};

struct ModuleInfo {
    const char *name;
    void *handle = nullptr;
    void *createInterface = nullptr;
    int resolved = 0;
};

int phdrCensusCb(dl_phdr_info *info, size_t, void *data)
{
    auto *found = static_cast<std::vector<std::string> *>(data);
    const char *name = info->dlpi_name ? info->dlpi_name : "";
    for (const char *candidate : kGameModules) {
        if (!modulePathMatches(name, candidate))
            continue;
        char line[160];
        snprintf(line, sizeof(line), "module: %-24s base=%p", candidate,
                 reinterpret_cast<void *>(info->dlpi_addr));
        log("%s", line);
        found->push_back(candidate);
        break;
    }
    return 0;
}

} 

void runDiagnostics()
{
    log("--- module census ---");
    std::vector<std::string> found;
    dl_iterate_phdr(phdrCensusCb, &found);

    log("--- interface probes ---");
    ModuleInfo modules[] = {
        {"client.so", nullptr, nullptr, 0},
        {"engine.so", nullptr, nullptr, 0},
        {"materialsystem.so", nullptr, nullptr, 0},
        {"vguimatsurface.so", nullptr, nullptr, 0},
        {"vgui2.so", nullptr, nullptr, 0},
        {"inputsystem.so", nullptr, nullptr, 0},
        {"vstdlib.so", nullptr, nullptr, 0},
        {"studiorender.so", nullptr, nullptr, 0},
    };

    
    
    
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        log("maps unreadable - probes skipped");
        return;
    }

    char lineBuf[512];
    while (fgets(lineBuf, sizeof(lineBuf), maps)) {
        
        
        
        
        const char *p = lineBuf;
        for (int field = 0; field < 5; ++field) {
            while (*p && !isspace(static_cast<unsigned char>(*p)))
                ++p;
            while (*p && isspace(static_cast<unsigned char>(*p)))
                ++p;
        }
        char path[400];
        size_t len = 0;
        while (p[len] && p[len] != '\n' && len + 1 < sizeof(path)) {
            path[len] = p[len];
            ++len;
        }
        path[len] = '\0';
        if (len == 0)
            continue;
        for (ModuleInfo &m : modules) {
            if (m.handle || !modulePathMatches(path, m.name))
                continue;
            m.handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
            if (!m.handle) {
                log("handle: %-24s NOLOAD failed: %s", m.name, dlerror());
                continue;
            }
            m.createInterface = dlsym(m.handle, "CreateInterface");
            if (!m.createInterface) {
                log("handle: %-24s no CreateInterface export", m.name);
                continue;
            }
            log("handle: %-24s CreateInterface @ %p", m.name, m.createInterface);
        }
    }
    fclose(maps);

    using CreateInterfaceFn = void *(*)(const char *, int *);
    for (ModuleInfo &m : modules) {
        if (!m.createInterface)
            continue;
        auto create = reinterpret_cast<CreateInterfaceFn>(m.createInterface);
        for (const char *version : kInterfaceCandidates) {
            int result = 0;
            void *iface = create(version, &result);
            if (iface) {
                log("iface: %-24s %-32s -> %p", m.name, version, iface);
                ++m.resolved;
            }
        }
        log("iface: %-24s resolved %d/%zu candidates", m.name, m.resolved,
            sizeof(kInterfaceCandidates) / sizeof(char *));
    }

    log("--- diagnostics done ---");
}

} 
