#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <BuildConfig.h>
#include <Platform/Linux/LinuxPlatformApi.h>

// Writable exchange root for every runtime file the module shares with host-side
// processes: the loader's unload request + integrity reports, the spawn-helper scripts
// and payloads (Discord RPC relay, steam persona, mic broadcast, lua http slots), and
// the diagnostics (gui log, crash dumps, unload log).
//
// This used to be /tmp: the Steam runtime container shared /tmp with the host, which the
// whole spawn-bridge design relied on ("steam-runtime-launch-client --host" runs helpers
// outside the container, and they read module-written files by absolute path). The
// 2026-10-04 Steam client update stopped that sharing - helpers got ENOENT on every
// module-written file and every diagnostic write vanished (that session left zero traces:
// no gui log, no crash dumps, no persona/RPC scripts).
//
// $HOME is the replacement: the container bind-mounts it read-write (the config system
// already treats $HOME/OsirisCS2 as its writable home - see OsirisDirectoryPath.h), so
// both sides of the bridge resolve the SAME absolute directory. The directory name reuses
// the config root's encrypted literal so the plaintext never reaches .rodata.
//
// Signal-safety: init() runs in normal context (CrashLogger::install is the earliest
// caller). Signal handlers must NOT call into this header at runtime - they use buffers
// resolved beforehand (see CrashLogger's install-time prefix copy).
namespace ns_paths
{

// Generous: $HOME can be long, and call sites format "<root>/<name>" after it.
inline constexpr std::size_t kMaxPath = 320;

// Preinitialized to the legacy fallback so root() is never a dangling/empty string even
// if init() has not run yet (init() overwrites it with the home-based root on success).
inline char rootPath[kMaxPath] = "/tmp";

// pthread_once, not std::call_once: the module links freestanding (-nostdlib - no
// libstdc++ helpers like __once_proxy).
inline pthread_once_t initControl = PTHREAD_ONCE_INIT;

[[nodiscard]] inline std::size_t length(const char* s) noexcept
{
    return std::strlen(s);
}

inline void initOnceBody() noexcept
{
    const auto home = LinuxPlatformApi::getenv("HOME");
    if (!home || home[0] == '\0')
        return;   // keep the /tmp fallback

    // <home>/<dirName> - dirName is the same encrypted literal the config root uses.
#if IS_LINUX()
    char dirNameBuf[::build::kOsirisDirNameEnc.decrypted_size()];
    ::build::kOsirisDirNameEnc.decrypt(dirNameBuf);
    const char* dirName = dirNameBuf;
#else
    const char* dirName = "OsirisCS2";
#endif

    const auto homeLength = length(home);
    const auto dirNameLength = length(dirName);
    if (homeLength + 1 + dirNameLength + 1 > sizeof(rootPath))
        return;

    std::memcpy(rootPath, home, homeLength);
    rootPath[homeLength] = '/';
    std::memcpy(rootPath + homeLength + 1, dirName, dirNameLength + 1);

    // Best-effort creation (the config system uses 0777 for the same tree). EEXIST is
    // fine; anything else (unwritable home, etc.) keeps the /tmp fallback from before.
    if (::mkdir(rootPath, 0777) != 0 && errno != EEXIST) {
        rootPath[0] = '/';
        rootPath[1] = 't';
        rootPath[2] = 'm';
        rootPath[3] = 'p';
        rootPath[4] = '\0';
        return;
    }

    // Diagnostics subdirectory (crash dumps, gui log, unload log live here).
    char logsDir[kMaxPath];
    const auto rootLength = length(rootPath);
    if (rootLength + 6 <= sizeof(logsDir)) {
        std::memcpy(logsDir, rootPath, rootLength);
        std::memcpy(logsDir + rootLength, "/logs", 6);
        ::mkdir(logsDir, 0777);   // EEXIST fine; log writes tolerate a missing dir
    }
}

inline void init() noexcept
{
    pthread_once(&initControl, &initOnceBody);
}

[[nodiscard]] inline const char* root() noexcept
{
    init();
    return rootPath;
}

[[nodiscard]] inline std::size_t rootLength() noexcept
{
    return length(root());
}

// Formats "<root>/<relative>" into out. Returns out, or nullptr when outSize is too
// small (callers treat that as "skip the write", never as a fatal error).
[[nodiscard]] inline const char* join(char* out, std::size_t outSize, const char* relative) noexcept
{
    const char* rootDir = root();
    const auto rootLen = length(rootDir);
    const auto relLen = length(relative);
    if (rootLen + 1 + relLen + 1 > outSize)
        return nullptr;
    std::memcpy(out, rootDir, rootLen);
    out[rootLen] = '/';
    std::memcpy(out + rootLen + 1, relative, relLen + 1);
    return out;
}

// join() for diagnostic files (crash dumps, gui log, unload log). On the home-based
// root these live in a "logs" subdirectory; on the /tmp fallback they stay flat so the
// legacy layout keeps working when home is unusable.
[[nodiscard]] inline bool isFallbackRoot() noexcept
{
    return rootPath[0] == '/' && rootPath[1] == 't' && rootPath[2] == 'm' && rootPath[3] == 'p' && rootPath[4] == '\0';
}

[[nodiscard]] inline const char* joinLog(char* out, std::size_t outSize, const char* name) noexcept
{
    if (isFallbackRoot())
        return join(out, outSize, name);
    // "<root>/logs/<name>"
    const auto rootLen = length(rootPath);
    const auto nameLen = length(name);
    if (rootLen + 6 + nameLen + 1 > outSize)
        return nullptr;
    std::memcpy(out, rootPath, rootLen);
    std::memcpy(out + rootLen, "/logs/", 6);
    std::memcpy(out + rootLen + 6, name, nameLen + 1);
    return out;
}

// join() for paths with a formatted suffix (e.g. "ns_module_integrity" + "_%d"):
// "<root>/<relativePrefix><formatted suffix>".
template <typename... Args>
[[nodiscard]] inline const char* joinFormat(char* out, std::size_t outSize, const char* relativePrefix, const char* suffixFormat, Args... args) noexcept
{
    if (!join(out, outSize, relativePrefix))
        return nullptr;
    // skip past "<root>/" AND the prefix itself - the suffix must not clobber it
    const auto offset = rootLength() + 1 + length(relativePrefix);
    std::snprintf(out + offset, outSize - offset, suffixFormat, args...);
    return out;
}

} // namespace ns_paths
