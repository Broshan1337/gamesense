#include "Tf2Diagnostics.h"
#include "Tf2Log.h"
#include "SessionBindKey.h"
#include "Vulkan/VkPresentHook.h"

#include <csignal>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

namespace ns_tf2 {

// ---- session binding (see cs2/Source/Utils/SessionBind.h for the full rationale) ----
//
// The loader stamps every module it injects with a 64-byte trailer after the ELF image:
// "NSHB01" | pad[2] | u64 loaderPid | session[16] | proof[32] = session XOR key.
// A module re-injected standalone from a dump has no trailer -> fails closed (inert).
// TF2 keeps the module mapped but installs NO hooks (no self-unload machinery here; the
// loader can still dlclose it normally).
constexpr unsigned kTrailerSize = 64;

bool verifyInjectionTrailer(long long* loaderPidOut)
{
    unsigned char key[32];
    for (int i = 0; i < 32; ++i)
        key[i] = static_cast<unsigned char>(session_bind_key::kKeyMasked[i] ^ session_bind_key::kKeyMask[i]);

    DIR* dir = opendir("/proc/self/fd");
    if (!dir)
        return false;
    int fd = -1;
    char fdPath[64];
    while (dirent* entry = readdir(dir)) {
        bool numeric = entry->d_name[0] != '\0';
        for (const char* p = entry->d_name; *p; ++p) {
            if (*p < '0' || *p > '9') { numeric = false; break; }
        }
        if (!numeric)
            continue;
        std::snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%s", entry->d_name);
        char target[128];
        const auto len = readlink(fdPath, target, sizeof(target) - 1);
        if (len <= 0)
            continue;
        target[len] = '\0';
        if (std::strncmp(target, "/memfd:libMangoHud.so", 21) != 0)
            continue;
        fd = open(fdPath, O_RDONLY);
        if (fd >= 0)
            break;
    }
    closedir(dir);
    if (fd < 0)
        return false;

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < static_cast<off_t>(kTrailerSize)) {
        close(fd);
        return false;
    }
    unsigned char trailer[kTrailerSize];
    if (pread(fd, trailer, kTrailerSize, size - kTrailerSize) != kTrailerSize) {
        close(fd);
        return false;
    }
    close(fd);

    // "NSHB01" XOR-obfuscated (see cs2/Source/Utils/SessionBind.h)
    static const unsigned char kMagicObf[6] = {0x14, 0x09, 0x12, 0x18, 0x6A, 0x6B};
    unsigned char magic[6];
    for (int i = 0; i < 6; ++i)
        magic[i] = static_cast<unsigned char>(kMagicObf[i] ^ 0x5A);
    if (std::memcmp(trailer, magic, 6) != 0)
        return false;
    long long pid = 0;
    for (int i = 7; i >= 0; --i)
        pid = (pid << 8) | trailer[8 + i];
    for (int i = 0; i < 32; ++i) {
        if (trailer[32 + i] != static_cast<unsigned char>(trailer[16 + (i % 16)] ^ key[i]))
            return false;
    }
    if (loaderPidOut)
        *loaderPidOut = pid;
    return true;
}


// The install phase probes/patches live game state from a constructor running inside
// dlopen. Two live SIGSEGVs (2026-09-14) killed the game because a faulting constructor
// fails the dlopen AND the injector's fallback dlopen crashes the same way. The guard
// turns any install-phase fault into "module inert" instead of a dead game.
sigjmp_buf g_installJmp;
void installFaultHandler(int, siginfo_t *, void *)
{
    siglongjmp(g_installJmp, 1);
}

bool runGuarded(void (*fn)(), const char *name)
{
    struct sigaction sa = {};
    struct sigaction old = {};
    sa.sa_sigaction = installFaultHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &old);
    sigaction(SIGBUS, &sa, nullptr);

    bool ok = true;
    if (sigsetjmp(g_installJmp, 1) == 0) {
        fn();
    } else {
        log("install: %s FAULTED - module continues inert", name);
        ok = false;
    }

    sigaction(SIGSEGV, &old, nullptr);
    sigaction(SIGBUS, &old, nullptr);
    return ok;
}

void installSteps()
{
    installVkPresentHook();     // iteration 1: public export inline hook (observer)
    installVkPresentSlotHook(); // iteration 2: queue-wrapper dispatch-slot patch
}

void initModule()
{
    logInit();
    log("init: begin (render-hook iteration 2b: guarded pread slot scan)");
    long long loaderPid = 0;
    if (!verifyInjectionTrailer(&loaderPid)) {
        // Fail-closed: no valid injection trailer = not injected by our loader (a dumped
        // module re-injected standalone). Stay mapped but inert.
        log("init: no valid session trailer - module stays INERT");
        return;
    }
    log("init: session bound to loader pid %lld", loaderPid);
    runDiagnostics();
    runGuarded(installSteps, "vk-present-install");
    log("init: done");
}

void shutdownModule()
{
    shutdownVkPresentSlotHook();
    log("unload: begin");
    logShutdown();
}

} // namespace ns_tf2

// The loader's gdb fallback dlcloses the module; keeping the constructor/destructor pair
// symmetric means even an early unload session stays clean.
__attribute__((constructor)) void ns_tf2_module_entry()
{
    ns_tf2::initModule();
}

__attribute__((destructor)) void ns_tf2_module_exit()
{
    ns_tf2::shutdownModule();
}
