#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace security::integrity
{

struct ModuleInfo
{
    std::uintptr_t module_base{0};
    std::uint32_t crc32{0};
    bool valid{false};
};

ModuleInfo* get(std::uintptr_t module_base);
void add(std::uintptr_t module_base, std::uint32_t crc32);
bool initialize();
void shutdown();

int open_spoofed_file(const char* path, int flags, mode_t mode);
ssize_t read_spoofed_file(int fd, void* buf, std::size_t count);
bool should_spoof_fd(int fd);
void close_spoofed_file(int fd);
bool is_ready() noexcept;

} // namespace security::integrity