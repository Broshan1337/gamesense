#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>












namespace vulkan_resolution_chain
{




inline constexpr std::string_view kIterationPattern =
    "\x48\x8D\x35" "????" "\x48\x89\xDF" "\x48\x89\x05" "????" "\xFF\x15" "????";
constexpr std::size_t kIterationLength = 23;
constexpr std::size_t kLeaDispOffset = 3;    
constexpr std::size_t kStoreDispOffset = 13; 
constexpr std::size_t kStoreNextInstructionOffset = 17; 
constexpr std::size_t kMaxNameLength = 48;

[[nodiscard]] inline std::int32_t readDisp32(const std::byte* at) noexcept
{
    std::int32_t disp = 0;
    std::memcpy(&disp, at, sizeof(disp));
    return disp;
}



inline void readName(const std::byte* match, char (&name)[kMaxNameLength]) noexcept
{
    const auto* str = reinterpret_cast<const char*>(match + 7 + readDisp32(match + kLeaDispOffset));
    std::size_t i = 0;
    while (i + 1 < sizeof(name) && str[i] != '\0') {
        name[i] = str[i];
        ++i;
    }
    name[i] = '\0';
}



[[nodiscard]] inline volatile std::uint64_t* storeSlot(const std::byte* match) noexcept
{
    return reinterpret_cast<volatile std::uint64_t*>(const_cast<std::byte*>(match) + kStoreNextInstructionOffset + readDisp32(match + kStoreDispOffset));
}

}
