#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/SessionBindKey.h>
#include <Utils/ObfAnnotations.h>

// Session binding (module side). The loader stamps every module it injects with a 64-byte
// trailer appended AFTER the ELF image in the memfd (see Loader/src/SessionTrailer.h for
// the format and the threat model):
//
//   "NSHB01" | pad[2] | u64 loaderPid | session[16] | proof[32] = session XOR key
//
//  - verifySession(): called ONCE on the present thread early in the first frame. Scans
//    /proc/self/fd for our memfd, reads the trailer off its end, verifies magic + proof.
//    FAIL-CLOSED: no trailer / bad proof = the module was not injected by our loader
//    (standalone re-injection of a dumped module) -> caller releases the module.
//  - tickLoaderLiveness(): present-thread poll. If the loader pid dies, the module is an
//    orphan (someone kept it alive without the loader) -> after the grace window it
//    releases itself via the same teardown path as the menu's Unload button.
//
// All failures are logged once via gui.log (anomaly-only contract) before releasing.
namespace session_bind
{

constexpr std::size_t kTrailerSize = 64;

// "NSHB01" stored XOR-obfuscated (pad 0x5A) so the magic is not a contiguous literal in
// the binary (it would otherwise be an 8-byte movabs immediate - the same leak class the
// string vaults close). Reassembled on the stack at verify time.
constexpr unsigned char kMagicObf[6] = {0x14, 0x09, 0x12, 0x18, 0x6A, 0x6B};
constexpr unsigned char kMagicPad = 0x5A;

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

// Reads + verifies the trailer. On success returns true and fills loaderPid.
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

    const unsigned char* k = key();
    for (int i = 0; i < 32; ++i) {
        if (static_cast<unsigned char>(trailer[32 + i]) != static_cast<unsigned char>(trailer[16 + (i % 16)] ^ k[i]))
            return false;
    }
    if (loaderPid)
        *loaderPid = pid;
    return true;
}

// Present-thread liveness poll. State machine kept in the caller's statics:
//   verified: trailer check passed (0 = not yet, 1 = ok, 2 = released)
//   loaderPid: from the trailer
//   misses: consecutive liveness failures
// Internally throttled: the initial verify runs on the first call, the liveness poll then
// runs on the same ~15 s cadence as the self-integrity watchdog. Call from ONE thread
// only (the same thread that owns the unload request path).
inline NS_OBF_FLATTEN void presentTick(bool& verified, std::int64_t& loaderPid, int& misses,
                        void (*onLoaderGone)()) noexcept
{
    static std::uint64_t lastTickNs = 0;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const std::uint64_t nowNs = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL
        + static_cast<std::uint64_t>(ts.tv_nsec);
    if (lastTickNs != 0 && nowNs - lastTickNs < 15'000'000'000ULL)
        return;
    lastTickNs = nowNs;

    if (verified == 2)
        return;
    if (verified == 0) {
        if (verifySession(&loaderPid)) {
            verified = 1;
            misses = 0;
        } else {
            verified = 2;
            gui_log::write("[session] no valid injection trailer - module not injected by our loader, releasing");
            onLoaderGone();
        }
        return;
    }
    // verified == 1: liveness (throttled to the ~15 s cadence above)
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
            alive = std::strcmp(comm, "Loader") == 0;
        }
    }
    if (alive) {
        misses = 0;
        return;
    }
    // 20 misses x ~15 s = ~5 min grace (covers a loader restart between sessions)
    if (++misses >= 20) {
        verified = 2;
        gui_log::write("[session] loader session gone (pid %lld) - releasing module",
            static_cast<long long>(loaderPid));
        onLoaderGone();
    }
}

} // namespace session_bind
