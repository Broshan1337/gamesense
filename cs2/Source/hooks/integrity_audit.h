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
// Untrack a spoofed fd WITHOUT closing it: for the fclose(FILE*) path, where the
// libc fclose already closed the underlying fd. Using close_spoofed_file there
// would close() an already-closed number that may have been reused.
void forget_spoofed_fd(int fd) noexcept;
bool is_ready() noexcept;
// Exported so the fopen hook can route cheat-path opens to the spoofed backing
// without duplicating the path-matching logic.
bool is_cheat_path(const char* path) noexcept;

} // namespace security::integrity