#include "VkPresentHook.h"

#include "Tf2Log.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

namespace ns_tf2 {

namespace {

















constexpr uint8_t kPrologue[17] = {
    0xf3, 0x0f, 0x1e, 0xfa,                                    
    0x48, 0x85, 0xff,                                          
    0x74, 0x27,                                                
    0x48, 0x8b, 0x07,                                          
    0x48, 0x85, 0xc0,                                          
    0x74, 0x1f,                                                
};
constexpr uintptr_t kAbortTarget = 0x43a90;   
constexpr uintptr_t kTailCallTarget = 0x43a80; 

using QueuePresentFn = int (*)(void *queue, const void *presentInfo);

uint8_t *g_trampoline = nullptr;
uint64_t g_presentCount = 0;
uint64_t g_loggedPresents = 0;

int hookedQueuePresent(void *queue, const void *presentInfo)
{
    ++g_presentCount;
    if (g_loggedPresents < 12) {
        ++g_loggedPresents;
        
        
        uint32_t swapchainCount = 0;
        const void *firstSwapchain = nullptr;
        if (presentInfo) {
            const uint8_t *base = static_cast<const uint8_t *>(presentInfo);
            uint32_t waitCount = 0;
            std::memcpy(&waitCount, base + 16, 4);
            std::memcpy(&swapchainCount, base + 24 + waitCount * 8, 4);
            if (swapchainCount)
                std::memcpy(&firstSwapchain, base + 32 + waitCount * 8, 8);
        }
        log("[vk] present #%llu queue=%p swaps=%u first=%p", (unsigned long long)g_presentCount,
            queue, swapchainCount, firstSwapchain);
        if (g_loggedPresents == 1)
            log("[vk] PRESENT HOOK ALIVE");
    } else if (g_presentCount % 1200 == 0) {
        log("[vk] heartbeat: %llu presents", (unsigned long long)g_presentCount);
    }
    auto trampoline = reinterpret_cast<QueuePresentFn>(g_trampoline);
    return trampoline(queue, presentInfo);
}


void emitAbsJump(uint8_t *out, uintptr_t target)
{
    out[0] = 0x48;
    out[1] = 0xB8;
    std::memcpy(out + 2, &target, 8);
    out[10] = 0xFF;
    out[11] = 0xE0;
}

} 

bool installVkPresentHook()
{
    void *loader = dlsym(RTLD_DEFAULT, "vkQueuePresentKHR");
    if (!loader) {
        
        
        
        log("[vk] not in global scope - resolving via /proc/self/maps");
        if (FILE *maps = fopen("/proc/self/maps", "r")) {
            char lineBuf[512];
            while (fgets(lineBuf, sizeof(lineBuf), maps)) {
                char path[400] = {};
                const char *p = lineBuf;
                for (int field = 0; field < 5; ++field) {
                    while (*p && !isspace(static_cast<unsigned char>(*p)))
                        ++p;
                    while (*p && isspace(static_cast<unsigned char>(*p)))
                        ++p;
                }
                size_t len = 0;
                while (p[len] && p[len] != '\n' && len + 1 < sizeof(path)) {
                    path[len] = p[len];
                    ++len;
                }
                if (!len)
                    continue;
                
                
                const char *base = std::strrchr(path, '/');
                base = base ? base + 1 : path;
                if (std::strncmp(base, "libvulkan.so", 12) != 0)
                    continue;
                void *handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
                if (handle) {
                    loader = dlsym(handle, "vkQueuePresentKHR");
                    log("[vk] resolved via %s: %p", path, loader);
                }
                break;
            }
            fclose(maps);
        }
    }
    if (!loader) {
        log("[vk] vkQueuePresentKHR not found - loader not loaded yet?");
        return false;
    }
    auto fn = reinterpret_cast<uint8_t *>(loader);
    log("[vk] loader vkQueuePresentKHR @ %p", (void *)fn);

    if (std::memcmp(fn, kPrologue, sizeof(kPrologue)) != 0) {
        log("[vk] PROLOGUE MISMATCH - loader changed, hook refused (no patch applied)");
        char hex[3 * sizeof(kPrologue) + 1] = {};
        for (size_t i = 0; i < sizeof(kPrologue); ++i)
            snprintf(hex + 3 * i, 4, "%02x ", fn[i]);
        log("[vk] live bytes: %s", hex);
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(fn);

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    g_trampoline = static_cast<uint8_t *>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (g_trampoline == MAP_FAILED) {
        g_trampoline = nullptr;
        log("[vk] trampoline mmap failed");
        return false;
    }
    uint8_t *c = g_trampoline;
    size_t o = 0;
    const uint8_t endbr[4] = {0xf3, 0x0f, 0x1e, 0xfa};
    const uint8_t testRdi[3] = {0x48, 0x85, 0xff};
    const uint8_t jne14[2] = {0x75, 0x0E};
    const uint8_t movRaxRdi[3] = {0x48, 0x8b, 0x07};
    const uint8_t testRax[3] = {0x48, 0x85, 0xc0};
    const uint8_t magicMov[10] = {0x48, 0xba, 0xed, 0xad, 0x10, 0x04, 0x04, 0x10, 0xad, 0xed};
    const uint8_t cmpRaxRdx[3] = {0x48, 0x39, 0x10};

    std::memcpy(c + o, endbr, 4);      o += 4;
    std::memcpy(c + o, testRdi, 3);    o += 3;
    std::memcpy(c + o, jne14, 2);      o += 2;
    emitAbsJump(c + o, base + kAbortTarget); o += 14;
    std::memcpy(c + o, movRaxRdi, 3);  o += 3;
    std::memcpy(c + o, testRax, 3);    o += 3;
    std::memcpy(c + o, jne14, 2);      o += 2;
    emitAbsJump(c + o, base + kAbortTarget); o += 14;
    std::memcpy(c + o, magicMov, 10);  o += 10;
    std::memcpy(c + o, cmpRaxRdx, 3);  o += 3;
    std::memcpy(c + o, jne14, 2);      o += 2;
    emitAbsJump(c + o, base + kAbortTarget); o += 14;
    emitAbsJump(c + o, base + kTailCallTarget); o += 14;

    if (mprotect(c, 4096, PROT_READ | PROT_EXEC) != 0) {
        log("[vk] trampoline mprotect(R-X) failed");
        return false;
    }
    __builtin___clear_cache(reinterpret_cast<char *>(c), reinterpret_cast<char *>(c) + o);

    
    
    const long pageSize = sysconf(_SC_PAGESIZE);
    uint8_t *pageStart = reinterpret_cast<uint8_t *>(
        reinterpret_cast<uintptr_t>(fn) & ~static_cast<uintptr_t>(pageSize - 1));
    if (mprotect(pageStart, 8192, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        log("[vk] mprotect(loader .text, RWX) failed - errno %d", errno);
        return false;
    }
    uint8_t patch[17];
    patch[0] = 0x48;
    patch[1] = 0xB8;
    const uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&hookedQueuePresent);
    std::memcpy(patch + 2, &hookAddr, 8);
    patch[10] = 0xFF;
    patch[11] = 0xE0;
    for (size_t i = 12; i < sizeof(patch); ++i)
        patch[i] = 0x90;
    std::memcpy(fn, patch, sizeof(patch));
    if (mprotect(pageStart, 8192, PROT_READ | PROT_EXEC) != 0)
        log("[vk] WARN: loader .text left W|X (restore mprotect failed)");
    __builtin___clear_cache(reinterpret_cast<char *>(fn), reinterpret_cast<char *>(fn) + sizeof(patch));

    log("[vk] hook installed: vkQueuePresentKHR %p -> hook, trampoline %p (%zu bytes)", (void *)fn,
        (void *)g_trampoline, o);
    return true;
}

} 
