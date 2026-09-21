#pragma once

#include <string_view>
#include <Platform/Macros/PlatformSpecific.h>
#include <Utils/NsStr.h>

namespace build
{

constexpr auto MEMORY_CAPACITY = 1'000'000;

// The config directory name is encrypted (NsStr.h) on Linux; decrypt it at use
// time (GlobalContext/OsirisDirectoryPath.h). The Windows build keeps the plain
// literal - this codebase ships Linux-only.
#if IS_LINUX()
inline constexpr ns_str::Encrypted<sizeof("OsirisCS2")> kOsirisDirNameEnc{"OsirisCS2"};
#endif
// Windows keeps the plain literal; on Linux the narrow value is empty (the
// encrypted kOsirisDirNameEnc above is the Linux source of truth) so the
// plaintext never reaches .rodata.
constexpr std::basic_string_view kOsirisDirectoryName{WIN64_LINUX(L"OsirisCS2", "")};
constexpr std::basic_string_view kConfigDirectoryName{WIN64_LINUX(L"configs", "configs")};
// Must comfortably hold the FULL serialized config: the save path silently truncates at this
// many bytes (and a truncated file then half-loads on the next injection, defaulting everything
// after the cut). Grew past 4096 when the FrameworkCS2 feature batch landed - it now needs ~5 KB.
constexpr auto kConfigFileBufferSize{16384};

}
