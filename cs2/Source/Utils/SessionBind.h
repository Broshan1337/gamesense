#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/SessionBindKey.h>
#include <Utils/ObfAnnotations.h>
#include <MemorySearch/PatternVault.h>

#include <atomic>
#include <cstdlib>

// Session binding (module side). The loader stamps every module it injects with a 64-byte
// trailer appended AFTER the ELF image in the memfd (see Loader/src/SessionTrailer.h for
// the format and the threat model):
//
//   "NSHB02" | pad[2] | u64 loaderPid | loaderComm[16] | proof[32] = loaderComm XOR key
//
//  - verifySession(): called ONCE on the present thread early in the first frame. Scans
//    /proc/self/fd for our memfd, reads the trailer off its end, verifies magic + proof,
//    and keeps the stamped loader comm for the liveness tick. FAIL-CLOSED: no trailer /
//    bad proof = the module was not injected by our loader (standalone re-injection of a
//    dumped module) -> caller releases the module.
//  - tickLoaderLiveness(): present-thread poll. If the loader pid dies, the module is an
//    orphan (someone kept it alive without the loader) -> after the grace window it
//    releases itself via the same teardown path as the menu's Unload button. The check
//    compares /proc/<pid>/comm against the comm the LOADER stamped at injection time -
//    never against a hardcoded name, so the loader binary can be renamed freely (the v1
//    hardcoded "Loader" silently released the module on any renamed binary).
//
// All failures are logged once via gui.log (anomaly-only contract) before releasing.
namespace session_bind
{

constexpr std::size_t kTrailerSize = 64;

// "NSHB02" stored XOR-obfuscated (pad 0x5A) so the magic is not a contiguous literal in
// the binary (it would otherwise be an 8-byte movabs immediate - the same leak class the
// string vaults close). Reassembled on the stack at verify time.
constexpr unsigned char kMagicObf[6] = {0x14, 0x09, 0x12, 0x18, 0x6A, 0x68};
constexpr unsigned char kMagicPad = 0x5A;

// The loader comm stamped in the verified trailer, copied out by verifySession and compared
// against the live /proc/<loaderPid>/comm by the liveness tick. NUL-terminated within 17
// bytes; zeros until a successful verify.
inline unsigned char loaderComm[17] = {};

// Shared session state (idempotent verify: the first-thread verify may run on the SDL thread
// BEFORE the pattern scan - the pattern vault's runtime lock must be armed before the pools
// unseal - and the present-thread tick then only does liveness).
//   0 = not yet verified, 1 = verified (vault armed), 2 = released (fail-closed)
inline std::atomic<int> sessionState{0};
inline std::atomic<std::int64_t> sessionLoaderPid{0};
inline std::atomic<bool> memfdFdsClosed{false};

[[nodiscard]] inline const unsigned char* key() noexcept
{
    static unsigned char k[32];
    for (int i = 0; i < 32; ++i)
        k[i] = static_cast<unsigned char>(session_bind_key::kKeyMasked[i] ^ session_bind_key::kKeyMask[i]);
    return k;
}

// Finds the fd of our own memfd ("/memfd:<name> (deleted)" entries in /proc/self/fd).
// Returns an OPEN fd (caller closes), or -1.
[[nodiscard]] inline NS_OBF_FLATTEN int findOwnMemfdFd() noexcept
{
    void* dir = LinuxPlatformApi::openDir("/proc/self/fd");
    if (!dir)
        return -1;
    int found = -1;
    while (const char* name = LinuxPlatformApi::readDir(dir)) {
        // numeric entries only
        bool numeric = name[0] != '\0';
        for (const char* p = name; *p; ++p) {
            if (*p < '0' || *p > '9') {
                numeric = false;
                break;
            }
        }
        if (!numeric)
            continue;
        char fdPath[64];
        char linkTarget[128];
        const int written = std::snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%s", name);
        if (written <= 0 || written >= static_cast<int>(sizeof(fdPath)))
            continue;
        const auto len = LinuxPlatformApi::readLink(fdPath, linkTarget, sizeof(linkTarget) - 1);
        if (len <= 0)
            continue;
        linkTarget[len] = '\0';
        if (std::strncmp(linkTarget, "/memfd:libMangoHud.so", 21) != 0)
            continue;
        const int fd = LinuxPlatformApi::open(fdPath, 0 /* O_RDONLY */);
        if (fd >= 0) {
            found = fd;
            break;
        }
    }
    LinuxPlatformApi::closeDir(dir);
    return found;
}

// Reads + verifies the trailer. On success returns true, fills loaderPid and stores the
// stamped loader comm (loaderComm, NUL-terminated within 16 bytes) for the liveness tick.
[[nodiscard]] inline NS_OBF_FLATTEN NS_OBF_ICALL bool verifySession(std::int64_t* loaderPid) noexcept
{
    const int fd = findOwnMemfdFd();
    if (fd < 0)
        return false;
    struct ::stat st {};
    if (LinuxPlatformApi::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(kTrailerSize)) {
        LinuxPlatformApi::close(fd);
        return false;
    }
    unsigned char trailer[kTrailerSize];
    if (LinuxPlatformApi::pread(fd, trailer, kTrailerSize, st.st_size - kTrailerSize) != kTrailerSize) {
        LinuxPlatformApi::close(fd);
        return false;
    }
    LinuxPlatformApi::close(fd);

    unsigned char magic[6];
    for (int i = 0; i < 6; ++i)
        magic[i] = static_cast<unsigned char>(kMagicObf[i] ^ kMagicPad);
    if (std::memcmp(trailer, magic, 6) != 0)
        return false;

    std::int64_t pid = 0;
    for (int i = 7; i >= 0; --i)
        pid = (pid << 8) | trailer[8 + i];

    // The proof also mixes the TARGET pid the loader stamped (hardening 2026-09-24): a
    // memfd file copied out of a running session (cp /proc/<pid>/fd/<n>) and re-injected
    // into a different process carries the OLD trailer - the pid mix makes its proof wrong,
    // so the copy is bricked even if the dumpable fd link is missed. The loader stamps
    // proof = comm ^ key ^ targetPid; the module recomputes with its own getpid().
    const auto ownPid = static_cast<unsigned long long>(LinuxPlatformApi::processId());
    const unsigned char* k = key();
    for (int i = 0; i < 32; ++i) {
        const auto pidByte = static_cast<unsigned char>((ownPid >> ((i & 7) * 8)) & 0xff);
        if (static_cast<unsigned char>(trailer[32 + i])
            != static_cast<unsigned char>(trailer[16 + (i % 16)] ^ k[i] ^ pidByte))
            return false;
    }
    if (loaderPid)
        *loaderPid = pid;
    std::memcpy(loaderComm, trailer + 16, sizeof(loaderComm) - 1);
    loaderComm[sizeof(loaderComm) - 1] = '\0';

    // Derive the pattern-vault runtime key from verified trailer material (hardening
    // 2026-09-24): exists at runtime only, never in the module binary. Arms the vault so
    // the pattern pools can unseal (fail-closed until this runs - see PatternVault.h).
    unsigned char subkey[32];
    for (int i = 0; i < 32; ++i)
        subkey[i] = static_cast<unsigned char>(trailer[32 + i] ^ k[i]
            ^ static_cast<unsigned char>((pid >> ((i & 7) * 8)) & 0xff)
            ^ static_cast<unsigned char>(i * 0x9D));
    pattern_vault::armRuntimeLock(subkey);
    return true;
}

// Closes every leaked memfd fd in OUR process (the injector leaves the /memfd:libMangoHud.so
// fd open in the target after dlopen - one per injection round). The open link is a one-
// command dump vector: cp /proc/<pid>/fd/<n> copied the ENTIRE module file INCLUDING the
// session trailer, which is exactly what a standalone re-injection needs (hardening
// 2026-09-24). Called ONCE, right after the trailer verified - the fd is not needed again
// (the module stays mapped; only the file link disappears). The leaked-fd "poisoned state"
// observations in older notes were downstream of this leak.
inline NS_OBF_FLATTEN void closeLeakedMemfdFds() noexcept
{
    bool expected = false;
    if (!memfdFdsClosed.compare_exchange_strong(expected, true))
        return;
    void* dir = LinuxPlatformApi::openDir("/proc/self/fd");
    if (!dir)
        return;
    int closedCount = 0;
    while (const char* name = LinuxPlatformApi::readDir(dir)) {
        bool numeric = name[0] != '\0';
        for (const char* p = name; *p; ++p) {
            if (*p < '0' || *p > '9') {
                numeric = false;
                break;
            }
        }
        if (!numeric)
            continue;
        char fdPath[64];
        char linkTarget[128];
        const int written = std::snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%s", name);
        if (written <= 0 || written >= static_cast<int>(sizeof(fdPath)))
            continue;
        const auto len = LinuxPlatformApi::readLink(fdPath, linkTarget, sizeof(linkTarget) - 1);
        if (len <= 0)
            continue;
        linkTarget[len] = '\0';
        if (std::strncmp(linkTarget, "/memfd:libMangoHud.so", 21) != 0)
            continue;
        const int fd = static_cast<int>(std::atoi(name));
        if (fd >= 0) {
            LinuxPlatformApi::close(fd);
            ++closedCount;
        }
    }
    LinuxPlatformApi::closeDir(dir);
    if (closedCount > 0)
        gui_log::write("[session] closed %d leaked memfd fd(s) - the dumpable file link is gone", closedCount);
}

// Idempotent verify: safe from any thread (SDL init thread + present thread race on the
// same atomic state). Runs the trailer check, arms the pattern vault on success, closes the
// leaked memfd fds. Returns true when the session is verified.
[[nodiscard]] inline NS_OBF_FLATTEN NS_OBF_ICALL bool verifyOnce(void (*onLoaderGone)()) noexcept
{
    int expected = 0;
    if (sessionState.compare_exchange_strong(expected, -1, std::memory_order_acq_rel)) {
        std::int64_t pid = 0;
        if (verifySession(&pid)) {
            sessionLoaderPid.store(pid, std::memory_order_release);
            sessionState.store(1, std::memory_order_release);
            closeLeakedMemfdFds();
        } else {
            sessionState.store(2, std::memory_order_release);
            gui_log::write("[session] no valid injection trailer - module not injected by our loader, releasing");
            if (onLoaderGone)
                onLoaderGone();
        }
    }
    // -1 = another thread is mid-verify: treat as not-yet-verified (false), the racing
    // caller's own state transition completes it.
    return sessionState.load(std::memory_order_acquire) == 1;
}

// Present-thread liveness poll. Uses the shared session state (the initial verify may have
// already run on the SDL init thread - see verifyOnce); internally throttled: the liveness
// poll runs on the same ~15 s cadence as the self-integrity watchdog. Call from ONE thread
// only (the same thread that owns the unload request path).
inline NS_OBF_FLATTEN void presentTick(void (*onLoaderGone)()) noexcept
{
    static std::uint64_t lastTickNs = 0;
    static int misses = 0;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const std::uint64_t nowNs = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL
        + static_cast<std::uint64_t>(ts.tv_nsec);
    if (lastTickNs != 0 && nowNs - lastTickNs < 15'000'000'000ULL)
        return;
    lastTickNs = nowNs;

    const int state = sessionState.load(std::memory_order_acquire);
    if (state == 2)
        return;
    if (state != 1) {
        // 0 = not yet verified, -1 = another thread is mid-verify: drive/await the verify,
        // never run the liveness branch with an unset loader pid.
        verifyOnce(onLoaderGone);
        return;
    }
    // verified: liveness (throttled to the ~15 s cadence above). The comm the loader
    // stamped at injection time is the reference - a renamed/repackaged loader binary still
    // matches because the stamp was taken from the RUNNING loader's own /proc/self/comm.
    const auto loaderPid = sessionLoaderPid.load(std::memory_order_relaxed);
    char path[32];
    std::snprintf(path, sizeof(path), "/proc/%lld/comm", static_cast<long long>(loaderPid));
    const int fd = LinuxPlatformApi::open(path, 0 /* O_RDONLY */);
    char comm[64] = {0};
    bool alive = false;
    if (fd >= 0) {
        const auto n = LinuxPlatformApi::pread(fd, comm, sizeof(comm) - 1, 0);
        LinuxPlatformApi::close(fd);
        if (n > 0) {
            comm[n] = '\0';
            // strip trailing newline
            for (char* p = comm; *p; ++p) {
                if (*p == '\n') {
                    *p = '\0';
                    break;
                }
            }
            alive = std::strcmp(comm, reinterpret_cast<const char*>(loaderComm)) == 0;
        }
    }
    if (alive) {
        misses = 0;
        return;
    }
    // 20 misses x ~15 s = ~5 min grace (covers a loader restart between sessions)
    if (++misses >= 20) {
        sessionState.store(2, std::memory_order_release);
        gui_log::write("[session] loader session gone (pid %lld) - releasing module",
            static_cast<long long>(loaderPid));
        onLoaderGone();
    }
}

} // namespace session_bind
