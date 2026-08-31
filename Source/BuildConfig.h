#pragma once

#include <string_view>
#include <Platform/Macros/PlatformSpecific.h>

namespace build
{

constexpr auto MEMORY_CAPACITY = 1'000'000;

constexpr std::basic_string_view kOsirisDirectoryName{WIN64_LINUX(L"OsirisCS2", "OsirisCS2")};
constexpr std::basic_string_view kConfigDirectoryName{WIN64_LINUX(L"configs", "configs")};
// Must comfortably hold the FULL serialized config: the save path silently truncates at this
// many bytes (and a truncated file then half-loads on the next injection, defaulting everything
// after the cut). Grew past 4096 when the FrameworkCS2 feature batch landed - it now needs ~5 KB.
constexpr auto kConfigFileBufferSize{16384};

}
