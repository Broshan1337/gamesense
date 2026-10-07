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



constexpr auto kConfigFileBufferSize{16384};

}
