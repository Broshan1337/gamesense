#pragma once

#include <string_view>
#include <Platform/Macros/PlatformSpecific.h>
#include <Utils/NsStr.h>

namespace build
{

constexpr auto MEMORY_CAPACITY = 1'000'000;




#if IS_LINUX()
inline constexpr ns_str::Encrypted<sizeof("OsirisCS2")> kOsirisDirNameEnc{"OsirisCS2"};
#endif



constexpr std::basic_string_view kOsirisDirectoryName{WIN64_LINUX(L"OsirisCS2", "")};
constexpr std::basic_string_view kConfigDirectoryName{WIN64_LINUX(L"configs", "configs")};



// Save/load scratch buffer. Must fit a FULL config file: the per-weapon rage
// configs (RageGroups, 32 weapons x 17 keys on top of everything else) pushed
// the saved document past the old 16 KiB on 2026-10-10.
constexpr auto kConfigFileBufferSize{65536};

}
