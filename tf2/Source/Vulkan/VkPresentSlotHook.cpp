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

// ---------------------------------------------------------------------------
// Finding the present chain entry.
//
// Live findings (2026-09-14):
//  - The game never calls libvulkan's PUBLIC vkQueuePresentKHR (inline hook installed,
//    zero calls): shaderapivk/dxvk resolve via vkGetDeviceProcAddr at device creation,
//    pre-injection, and call the loader terminator directly.
//  - Loader queue-wrapper objects (magic 0x10ADED040410ADED, allocated in a ~2GB malloc
//    arena) hold the dispatch chain at +0x690: the next link is Steam's
//    steamoverlayvulkanlayer.so present function (an implicit Vulkan layer).
//  - The layer chain is on EVERY present path (table, terminator, cached pointer), so
//    inline-hooking the chain function intercepts everything - and it survives lazy
//    wrapper allocation / device recreation, because the hook is on the function, not
//    the table.
// ---------------------------------------------------------------------------

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
        // VkPresentInfoKHR: sType(4) pad(4) pNext(8) waitSemaphoreCount(4) pad(4)
        //                   pWaitSemaphores(8*wait) swapchainCount(4) pad(4) pSwapchains...
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

// A real loader dispatch slot sits in a CLUSTER of function pointers: the live dump
// (2026-09-14) shows +0x670..+0x6a8 holding 7 consecutive exec pointers (layer/loader/
// driver mix) with nulls elsewhere (unresolved optional functions). A ±24-slot density
// check fails on that layout - the cluster IS the signature. Stack/heap garbage never
// produces 5+ consecutive exec pointers aligned to this exact window (caught live: the
// host smoke test patched stack garbage and crashed).
bool looksLikeDispatchTable(int memFd, uintptr_t wrapperAddr)
{
    constexpr int kWindowLow = -5;  // +0x660
    constexpr int kWindowHigh = +9; // +0x6d8 exclusive
    constexpr int kMinExecPtrs = 5;
    int execPtrs = 0;
    for (int i = kWindowLow; i < kWindowHigh; ++i) {
        if (i == 0)
            continue; // our target slot, already validated
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

// Scans a virtual range via /proc/self/mem pread instead of direct dereference: the game's
// threads mmap/munmap continuously, so a mapping seen in /proc/self/maps can be gone by
// the time we read it (two live SIGSEGV crashes taught this the hard way). pread returns
// -EIO instead of faulting.
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
            break; // stale/unmapped range mid-scan - skip the rest of it
        const uintptr_t chunkEnd = chunk + size_t(got);
        for (uintptr_t cursor = chunk; cursor + sizeof(uint64_t) <= chunkEnd; cursor += 8) {
            uint64_t value = 0;
            std::memcpy(&value, buf + (cursor - chunk), 8);
            if (value != kWrapperMagic)
                continue;
            if (cursor + kPresentSlotOffset + sizeof(uint64_t) > end)
                continue; // slot would leave the range
            if (candidateCount >= maxCandidates)
                break;
            uintptr_t fn = 0;
            if (::pread(memFd, &fn, 8, off_t(cursor + kPresentSlotOffset)) != 8)
                continue; // slot raced away
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

// Finds the present dispatch-chain function: scan writable memory for loader wrapper
// objects, majority-vote the +0x690 slot among candidates that point into executable
// memory inside a real dispatch cluster. Returns 0 when nothing credible is found.
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
    // Exec ranges are re-collected EVERY pass (dlopen of late modules grows the set);
    // without the reset the array fills with duplicates and late ranges get dropped.
    g_execRangeCount = 0;
    while (fgets(lineBuf, sizeof(lineBuf), maps)) {
        uintptr_t start = 0, end = 0;
        char perms[8] = {};
        int consumed = 0;
        if (sscanf(lineBuf, "%lx-%lx %7s%n", &start, &end, perms, &consumed) != 3)
            continue;
        // Executable mappings: candidate slot values must point into one of these.
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
        // File-backed ranges carry a path after inode - wrappers live in anonymous heap.
        const char *p = lineBuf + consumed;
        for (int f = 0; f < 3; ++f) {
            while (*p && !isspace(static_cast<unsigned char>(*p)))
                ++p;
            while (*p && isspace(static_cast<unsigned char>(*p)))
                ++p;
        }
        if (*p == '/') {
            // File-backed: only libvulkan's own data sections are interesting.
            const char *base = std::strrchr(p, '/');
            base = base ? base + 1 : p;
            if (std::strncmp(base, "libvulkan.so", 12) != 0)
                continue;
        } else if (std::strncmp(p, "[stack", 6) == 0) {
            // NEVER scan the stacks (thread stacks are anonymous - the name check only
            // covers the main one; the density check below covers the rest).
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

    // Majority vote on the slot value among credible objects.
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

// ---------------------------------------------------------------------------
// Inline hook on the chain present function.
//
// Captured prologue (steamoverlayvulkanlayer.so, session 2026-09-14 20:17):
//   +0x00 41 57              push %r15
//   +0x02 41 56              push %r14
//   +0x04 49 89 f6           mov  %rsi,%r14
//   +0x07 41 55              push %r13
//   +0x09 48 8d 35 XX XX XX XX   lea <rip+disp32>,%rsi   <- only rip-relative op
//   (patch boundary = +0x0c, exactly 12 bytes)
//   +0x10 ...                (push r12/rbp/rbx, sub rsp, ...)  <- resume target
//
// Trampoline: re-emits pushes + mov, re-emits the lea with a displacement corrected for
// the trampoline's own address, then abs-jumps to target+0x10. Original arguments
// (rdi=queue, rsi=presentInfo) flow through untouched.
constexpr uint8_t kChainPrologue[12] = {
    0x41, 0x57,             // push %r15
    0x41, 0x56,             // push %r14
    0x49, 0x89, 0xf6,       // mov %rsi,%r14
    0x41, 0x55,             // push %r13
    0x48, 0x8d, 0x35,       // lea disp32(%rip),%rsi (opcode prefix - disp checked apart)
};

bool installInlineHook(uintptr_t target)
{
    if (g_trampolineFn.load()) {
        log("[vk] inline hook already installed - target %p not hooked again", (void *)target);
        return true;
    }
    auto fn = reinterpret_cast<uint8_t *>(target);

    // Pattern check (prologue may differ across layer updates - fail loud, patch nothing).
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

    // Trampoline near the target so the relocated lea's displacement stays in int32 range.
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
    // push r15; push r14; mov rsi,r14; push r13
    const uint8_t head[9] = {0x41, 0x57, 0x41, 0x56, 0x49, 0x89, 0xf6, 0x41, 0x55};
    std::memcpy(c + o, head, 9);
    o += 9;
    // relocated lea: rip after instruction = tramp + 9 + 7
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
    // jmp target+0x10
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

    // Patch: movabs %rax, hook; jmp *%rax (12 bytes, instruction-boundary aligned).
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
            usleep(100 * 1000); // 10s in 100ms slices
        if (g_rescanStop.load(std::memory_order_relaxed))
            return;
        ++pass;
        const uintptr_t chainFn = findChainPresent();
        if (!chainFn)
            continue;
        if (installInlineHook(chainFn))
            return; // hooked - the function-level hook covers all future wrappers
        log("[vk] rescan pass %d: found chain fn but hook failed", pass);
    }
}

} // namespace

int installVkPresentSlotHook()
{
    const uintptr_t chainFn = findChainPresent();
    if (chainFn && installInlineHook(chainFn))
        return 1;
    // Wrapper may not exist yet (lazy vkGetDeviceQueue) or the prologue may need a retry:
    // keep looking in the background until hooked.
    g_rescanStop.store(false, std::memory_order_relaxed);
    std::thread(rescanLoop).detach();
    log("[vk] rescan thread running (10s interval, until hooked)");
    return 0;
}

void shutdownVkPresentSlotHook()
{
    g_rescanStop.store(true, std::memory_order_relaxed);
}

} // namespace ns_tf2
