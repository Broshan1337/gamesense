#include "VkPresentHook.h"

#include "Tf2Log.h"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace ns_tf2 {

namespace {

















constexpr uint64_t kWrapperMagic = 0x10ADED040410ADEDULL;
constexpr size_t kPresentSlotOffset = 0x690;
constexpr size_t kMaxPatches = 32;
constexpr size_t kMaxScanRangeBytes = 64ull * 1024 * 1024 * 1024;

struct ExecRange {
    uintptr_t start;
    uintptr_t end;
};
ExecRange g_execRanges[4096];
int g_execRangeCount = 0;

std::atomic<uintptr_t> g_trampolineFn{0};
std::atomic<uint64_t> g_presentCount{0};
uint64_t g_loggedPresents = 0;
std::atomic<bool> g_rescanStop{false};

using QueuePresentFn = int (*)(void *queue, const void *presentInfo);

int hookedQueuePresent(void *queue, const void *presentInfo)
{
    const uint64_t n = g_presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
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
        log("[vk] present #%llu queue=%p swaps=%u first=%p", (unsigned long long)n, queue,
            swapchainCount, firstSwapchain);
        if (g_loggedPresents == 1)
            log("[vk] PRESENT HOOK ALIVE");
    } else if (n % 1200 == 0) {
        log("[vk] heartbeat: %llu presents", (unsigned long long)n);
    }
    auto orig = reinterpret_cast<QueuePresentFn>(g_trampolineFn.load(std::memory_order_relaxed));
    return orig(queue, presentInfo);
}

bool inAnyExec(uintptr_t value)
{
    for (int i = 0; i < g_execRangeCount; ++i)
        if (value >= g_execRanges[i].start && value < g_execRanges[i].end)
            return true;
    return false;
}







bool looksLikeDispatchTable(int memFd, uintptr_t wrapperAddr)
{
    constexpr int kWindowLow = -5;  
    constexpr int kWindowHigh = +9; 
    constexpr int kMinExecPtrs = 5;
    int execPtrs = 0;
    for (int i = kWindowLow; i < kWindowHigh; ++i) {
        if (i == 0)
            continue; 
        uintptr_t value = 0;
        const uintptr_t addr = wrapperAddr + kPresentSlotOffset + uintptr_t(i * 8);
        if (::pread(memFd, &value, 8, off_t(addr)) != 8)
            continue;
        if (inAnyExec(value))
            ++execPtrs;
    }
    return execPtrs >= kMinExecPtrs;
}

struct SlotCandidate {
    uintptr_t address;
    uintptr_t fn;
};





int scanRange(int memFd, uintptr_t start, uintptr_t end, SlotCandidate *candidates,
              int &candidateCount, int maxCandidates)
{
    constexpr size_t kChunk = 1024 * 1024;
    uint8_t *buf = static_cast<uint8_t *>(::malloc(kChunk));
    if (!buf)
        return 0;

    int hits = 0;
    for (uintptr_t chunk = start; chunk < end; chunk += kChunk) {
        const size_t want = size_t((chunk + kChunk <= end) ? kChunk : (end - chunk));
        const ssize_t got = ::pread(memFd, buf, want, off_t(chunk));
        if (got <= 0)
            break; 
        const uintptr_t chunkEnd = chunk + size_t(got);
        for (uintptr_t cursor = chunk; cursor + sizeof(uint64_t) <= chunkEnd; cursor += 8) {
            uint64_t value = 0;
            std::memcpy(&value, buf + (cursor - chunk), 8);
            if (value != kWrapperMagic)
                continue;
            if (cursor + kPresentSlotOffset + sizeof(uint64_t) > end)
                continue; 
            if (candidateCount >= maxCandidates)
                break;
            uintptr_t fn = 0;
            if (::pread(memFd, &fn, 8, off_t(cursor + kPresentSlotOffset)) != 8)
                continue; 
            candidates[candidateCount].address = cursor;
            candidates[candidateCount].fn = fn;
            ++candidateCount;
            ++hits;
        }
        if (candidateCount >= maxCandidates)
            break;
    }
    ::free(buf);
    return hits;
}




uintptr_t findChainPresent()
{
    const int memFd = ::open("/proc/self/mem", O_RDONLY);
    if (memFd < 0) {
        log("[vk] /proc/self/mem unavailable - chain scan aborted");
        return 0;
    }

    SlotCandidate candidates[kMaxPatches];
    int candidateCount = 0;
    size_t scannedBytes = 0;

    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        ::close(memFd);
        log("[vk] maps unreadable - chain scan aborted");
        return 0;
    }

    char lineBuf[512];
    
    
    g_execRangeCount = 0;
    while (fgets(lineBuf, sizeof(lineBuf), maps)) {
        uintptr_t start = 0, end = 0;
        char perms[8] = {};
        int consumed = 0;
        if (sscanf(lineBuf, "%lx-%lx %7s%n", &start, &end, perms, &consumed) != 3)
            continue;
        
        if (std::strchr(perms, 'x')
            && g_execRangeCount < int(sizeof(g_execRanges) / sizeof(g_execRanges[0]))) {
            g_execRanges[g_execRangeCount].start = start;
            g_execRanges[g_execRangeCount].end = end;
            ++g_execRangeCount;
        } else if (std::strchr(perms, 'x')) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                log("[vk] WARN: exec range table full - slot validation degraded");
            }
        }
        if (candidateCount >= int(kMaxPatches))
            continue;
        if (!std::strchr(perms, 'r') || !std::strchr(perms, 'w'))
            continue;
        
        const char *p = lineBuf + consumed;
        for (int f = 0; f < 3; ++f) {
            while (*p && !isspace(static_cast<unsigned char>(*p)))
                ++p;
            while (*p && isspace(static_cast<unsigned char>(*p)))
                ++p;
        }
        if (*p == '/') {
            
            const char *base = std::strrchr(p, '/');
            base = base ? base + 1 : p;
            if (std::strncmp(base, "libvulkan.so", 12) != 0)
                continue;
        } else if (std::strncmp(p, "[stack", 6) == 0) {
            
            
            continue;
        }
        if (end - start > kMaxScanRangeBytes)
            continue;
        const int hits = scanRange(memFd, start, end, candidates, candidateCount, int(kMaxPatches));
        scannedBytes += (end - start);
        if (hits)
            log("[vk] range %lx-%lx: %d hits", start, end, hits);
    }
    fclose(maps);
    ::close(memFd);

    log("[vk] chain scan: %zu MB scanned, %d exec ranges, %d magic objects", scannedBytes / (1024 * 1024),
        g_execRangeCount, candidateCount);

    
    uintptr_t majorityFn = 0;
    int majorityCount = 0;
    for (int i = 0; i < candidateCount; ++i) {
        if (!inAnyExec(candidates[i].fn))
            continue;
        if (!looksLikeDispatchTable(memFd, candidates[i].address)) {
            log("[vk] magic @ %p: slot=%p exec-pointing but no dispatch cluster - garbage",
                (void *)candidates[i].address, (void *)candidates[i].fn);
            continue;
        }
        int count = 0;
        for (int j = 0; j < candidateCount; ++j)
            if (candidates[j].fn == candidates[i].fn)
                ++count;
        if (count > majorityCount) {
            majorityCount = count;
            majorityFn = candidates[i].fn;
        }
    }
    if (!majorityFn) {
        log("[vk] no credible dispatch slot found - nothing hooked");
        for (int i = 0; i < candidateCount; ++i)
            log("[vk]   object @ %p slot=%p", (void *)candidates[i].address, (void *)candidates[i].fn);
        return 0;
    }
    log("[vk] dispatch-chain present = %p (majority %d/%d)", (void *)majorityFn, majorityCount,
        candidateCount);
    return majorityFn;
}
















constexpr uint8_t kChainPrologue[12] = {
    0x41, 0x57,             
    0x41, 0x56,             
    0x49, 0x89, 0xf6,       
    0x41, 0x55,             
    0x48, 0x8d, 0x35,       
};

bool installInlineHook(uintptr_t target)
{
    if (g_trampolineFn.load()) {
        log("[vk] inline hook already installed - target %p not hooked again", (void *)target);
        return true;
    }
    auto fn = reinterpret_cast<uint8_t *>(target);

    
    int memFd = ::open("/proc/self/mem", O_RDONLY);
    if (memFd < 0) {
        log("[vk] /proc/self/mem unavailable - inline hook aborted");
        return false;
    }
    uint8_t live[12] = {};
    if (::pread(memFd, live, 12, off_t(target)) != 12) {
        log("[vk] cannot read chain fn prologue - aborted");
        ::close(memFd);
        return false;
    }
    ::close(memFd);
    if (std::memcmp(live, kChainPrologue, sizeof(kChainPrologue)) != 0) {
        log("[vk] CHAIN PROLOGUE MISMATCH - no patch applied");
        char hex[3 * 13] = {};
        for (size_t i = 0; i < 12; ++i)
            snprintf(hex + 3 * i, 4, "%02x ", live[i]);
        log("[vk] live bytes: %s", hex);
        return false;
    }
    const int32_t origDisp = int32_t(live[12]) | (int32_t(live[13]) << 8) | (int32_t(live[14]) << 16)
        | (int32_t(live[15]) << 24);
    const uintptr_t leaTarget = target + 0x10 + uint32_t(origDisp);

    
    void *tramp = mmap(reinterpret_cast<void *>(target & ~uintptr_t(0x3FFFFFFF)), 4096,
                       PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED)
        tramp = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) {
        log("[vk] trampoline mmap failed");
        return false;
    }
    uint8_t *c = static_cast<uint8_t *>(tramp);
    size_t o = 0;
    
    const uint8_t head[9] = {0x41, 0x57, 0x41, 0x56, 0x49, 0x89, 0xf6, 0x41, 0x55};
    std::memcpy(c + o, head, 9);
    o += 9;
    
    const uintptr_t leaRip = reinterpret_cast<uintptr_t>(c) + o + 7;
    const int32_t newDisp = int32_t(uint64_t(leaTarget) - leaRip);
    if (leaTarget > leaRip + 0x7FFFFFFFull || leaRip > leaTarget + 0x80000000ull) {
        log("[vk] relocated lea displacement out of int32 range - aborted");
        munmap(tramp, 4096);
        return false;
    }
    c[o++] = 0x48;
    c[o++] = 0x8d;
    c[o++] = 0x35;
    std::memcpy(c + o, &newDisp, 4);
    o += 4;
    
    const uintptr_t resume = target + 0x10;
    c[o++] = 0x48;
    c[o++] = 0xB8;
    std::memcpy(c + o, &resume, 8);
    o += 8;
    c[o++] = 0xFF;
    c[o++] = 0xE0;

    if (mprotect(c, 4096, PROT_READ | PROT_EXEC) != 0) {
        log("[vk] trampoline mprotect(R-X) failed");
        munmap(tramp, 4096);
        return false;
    }
    __builtin___clear_cache(reinterpret_cast<char *>(c), reinterpret_cast<char *>(c) + o);
    g_trampolineFn.store(reinterpret_cast<uintptr_t>(c), std::memory_order_relaxed);

    
    const long pageSize = sysconf(_SC_PAGESIZE);
    uint8_t *pageStart = reinterpret_cast<uint8_t *>(target & ~uintptr_t(pageSize - 1));
    if (mprotect(pageStart, 8192, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        log("[vk] mprotect(chain fn, RWX) failed - errno %d", errno);
        g_trampolineFn.store(0);
        munmap(tramp, 4096);
        return false;
    }
    uint8_t patch[12];
    patch[0] = 0x48;
    patch[1] = 0xB8;
    const uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&hookedQueuePresent);
    std::memcpy(patch + 2, &hookAddr, 8);
    patch[10] = 0xFF;
    patch[11] = 0xE0;
    std::memcpy(fn, patch, sizeof(patch));
    if (mprotect(pageStart, 8192, PROT_READ | PROT_EXEC) != 0)
        log("[vk] WARN: chain fn page left W|X (restore mprotect failed)");
    __builtin___clear_cache(reinterpret_cast<char *>(fn), reinterpret_cast<char *>(fn) + sizeof(patch));

    log("[vk] inline hook installed: chain present %p -> hook, trampoline %p (%zu bytes)", fn,
        (void *)c, o);
    return true;
}

void rescanLoop()
{
    int pass = 0;
    while (!g_rescanStop.load(std::memory_order_relaxed)) {
        for (int i = 0; i < 100 && !g_rescanStop.load(std::memory_order_relaxed); ++i)
            usleep(100 * 1000); 
        if (g_rescanStop.load(std::memory_order_relaxed))
            return;
        ++pass;
        const uintptr_t chainFn = findChainPresent();
        if (!chainFn)
            continue;
        if (installInlineHook(chainFn))
            return; 
        log("[vk] rescan pass %d: found chain fn but hook failed", pass);
    }
}

} 

int installVkPresentSlotHook()
{
    const uintptr_t chainFn = findChainPresent();
    if (chainFn && installInlineHook(chainFn))
        return 1;
    
    
    g_rescanStop.store(false, std::memory_order_relaxed);
    std::thread(rescanLoop).detach();
    log("[vk] rescan thread running (10s interval, until hooked)");
    return 0;
}

void shutdownVkPresentSlotHook()
{
    g_rescanStop.store(true, std::memory_order_relaxed);
}

} 
