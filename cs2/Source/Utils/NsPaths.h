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





















namespace ns_paths
{


inline constexpr std::size_t kMaxPath = 320;



inline char rootPath[kMaxPath] = "/tmp";



inline pthread_once_t initControl = PTHREAD_ONCE_INIT;

[[nodiscard]] inline std::size_t length(const char* s) noexcept
{
    return std::strlen(s);
}

inline void initOnceBody() noexcept
{
    const auto home = LinuxPlatformApi::getenv("HOME");
    if (!home || home[0] == '\0')
        return;   

    
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

    
    
    if (::mkdir(rootPath, 0777) != 0 && errno != EEXIST) {
        rootPath[0] = '/';
        rootPath[1] = 't';
        rootPath[2] = 'm';
        rootPath[3] = 'p';
        rootPath[4] = '\0';
        return;
    }

    
    char logsDir[kMaxPath];
    const auto rootLength = length(rootPath);
    if (rootLength + 6 <= sizeof(logsDir)) {
        std::memcpy(logsDir, rootPath, rootLength);
        std::memcpy(logsDir + rootLength, "/logs", 6);
        ::mkdir(logsDir, 0777);   
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




[[nodiscard]] inline bool isFallbackRoot() noexcept
{
    return rootPath[0] == '/' && rootPath[1] == 't' && rootPath[2] == 'm' && rootPath[3] == 'p' && rootPath[4] == '\0';
}

[[nodiscard]] inline const char* joinLog(char* out, std::size_t outSize, const char* name) noexcept
{
    if (isFallbackRoot())
        return join(out, outSize, name);
    
    const auto rootLen = length(rootPath);
    const auto nameLen = length(name);
    if (rootLen + 6 + nameLen + 1 > outSize)
        return nullptr;
    std::memcpy(out, rootPath, rootLen);
    std::memcpy(out + rootLen, "/logs/", 6);
    std::memcpy(out + rootLen + 6, name, nameLen + 1);
    return out;
}



template <typename... Args>
[[nodiscard]] inline const char* joinFormat(char* out, std::size_t outSize, const char* relativePrefix, const char* suffixFormat, Args... args) noexcept
{
    if (!join(out, outSize, relativePrefix))
        return nullptr;
    
    const auto offset = rootLength() + 1 + length(relativePrefix);
    std::snprintf(out + offset, outSize - offset, suffixFormat, args...);
    return out;
}

} 
