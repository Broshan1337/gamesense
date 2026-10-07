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




















namespace session_bind
{

constexpr std::size_t kTrailerSize = 64;




constexpr unsigned char kMagicObf[6] = {0x14, 0x09, 0x12, 0x18, 0x6A, 0x68};
constexpr unsigned char kMagicPad = 0x5A;




inline unsigned char loaderComm[17] = {};





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



[[nodiscard]] inline NS_OBF_FLATTEN int findOwnMemfdFd() noexcept
{
    void* dir = LinuxPlatformApi::openDir("/proc/self/fd");
    if (!dir)
        return -1;
    int found = -1;
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
        const int fd = LinuxPlatformApi::open(fdPath, 0 );
        if (fd >= 0) {
            found = fd;
            break;
        }
    }
    LinuxPlatformApi::closeDir(dir);
    return found;
}



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

    
    
    
    unsigned char subkey[32];
    for (int i = 0; i < 32; ++i)
        subkey[i] = static_cast<unsigned char>(trailer[32 + i] ^ k[i]
            ^ static_cast<unsigned char>((pid >> ((i & 7) * 8)) & 0xff)
            ^ static_cast<unsigned char>(i * 0x9D));
    pattern_vault::armRuntimeLock(subkey);
    return true;
}








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
    
    
    return sessionState.load(std::memory_order_acquire) == 1;
}





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
        
        
        verifyOnce(onLoaderGone);
        return;
    }
    
    
    
    const auto loaderPid = sessionLoaderPid.load(std::memory_order_relaxed);
    char path[32];
    std::snprintf(path, sizeof(path), "/proc/%lld/comm", static_cast<long long>(loaderPid));
    const int fd = LinuxPlatformApi::open(path, 0 );
    char comm[64] = {0};
    bool alive = false;
    if (fd >= 0) {
        const auto n = LinuxPlatformApi::pread(fd, comm, sizeof(comm) - 1, 0);
        LinuxPlatformApi::close(fd);
        if (n > 0) {
            comm[n] = '\0';
            
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
    
    if (++misses >= 20) {
        sessionState.store(2, std::memory_order_release);
        gui_log::write("[session] loader session gone (pid %lld) - releasing module",
            static_cast<long long>(loaderPid));
        onLoaderGone();
    }
}

} 
