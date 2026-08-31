#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// Decoder for the Vulkan function-pointer resolution chain the CS2 renderer uses to fill its
// cache table (librendersystemvulkan.so). See Hooks/Graphics/VulkanHook.h for the full story.
//
// Every iteration is 23 bytes:
//   48 8D 35 disp32   lea  rsi, [rip + disp]   -> function name string (in .rodata)
//   48 89 DF          mov  rdi, rbx
//   48 89 05 disp32   mov  qword [rip + disp], rax -> stores the PREVIOUS iteration's result
//   FF 15 disp32      call qword [rip + disp]  -> vkGetDeviceProcAddr
//
// Because the store lags one iteration behind its own name, the cache slot for function N is
// the store displacement of iteration N+1.
namespace vulkan_resolution_chain
{

// Compiled pattern of one chain iteration as a raw byte string with '?' wildcard bytes
// (pass together with kPatternStringWildcard). Spaced form for reference:
//   "48 8D 35 ? ? ? ? 48 89 DF 48 89 05 ? ? ? ? FF 15 ? ? ? ?"
inline constexpr std::string_view kIterationPattern =
    "\x48\x8D\x35" "????" "\x48\x89\xDF" "\x48\x89\x05" "????" "\xFF\x15" "????";
constexpr std::size_t kIterationLength = 23;
constexpr std::size_t kLeaDispOffset = 3;    // name string displacement
constexpr std::size_t kStoreDispOffset = 13; // cache slot displacement
constexpr std::size_t kStoreNextInstructionOffset = 17; // rip base of the store (offset right after its disp32)
constexpr std::size_t kMaxNameLength = 48;

[[nodiscard]] inline std::int32_t readDisp32(const std::byte* at) noexcept
{
    std::int32_t disp = 0;
    std::memcpy(&disp, at, sizeof(disp));
    return disp;
}

// Decodes a chain iteration's name string. `match` points at the iteration start; the name
// lives at `match + 7 + leaDisp` (in the module's .rodata, mapped at runtime).
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

// The cache slot this iteration's store writes (a runtime address in the renderer's .bss).
// The rip base is the instruction AFTER the disp32 (offset 17), not the end of the iteration.
[[nodiscard]] inline volatile std::uint64_t* storeSlot(const std::byte* match) noexcept
{
    return reinterpret_cast<volatile std::uint64_t*>(const_cast<std::byte*>(match) + kStoreNextInstructionOffset + readDisp32(match + kStoreDispOffset));
}

}
