#pragma once

#include <cstddef>
#include <cstdint>
#include <elf.h>
#include <link.h>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/ObfAnnotations.h>
















namespace self_integrity
{

struct TextRange {
    std::uintptr_t start{0};
    std::size_t size{0};
};

[[nodiscard]] inline NS_OBF_FLATTEN std::uint64_t fnv1a(const std::byte* data, std::size_t size) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[i]));
        hash *= 0x100000001b3ULL;
    }
    return hash;
}




[[nodiscard]] inline NS_OBF_FLATTEN TextRange ownTextRange(const void* landmark) noexcept
{
    struct Ctx {
        std::uintptr_t landmarkAddr;
        TextRange range;
    } ctx{reinterpret_cast<std::uintptr_t>(landmark), {}};

    dl_iterate_phdr([](struct dl_phdr_info* info, std::size_t, void* data) noexcept -> int {
        auto* c = static_cast<Ctx*>(data);
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const auto& phdr = info->dlpi_phdr[i];
            if (phdr.p_type != PT_LOAD)
                continue;
            const std::uintptr_t start = info->dlpi_addr + phdr.p_vaddr;
            const std::uintptr_t end = start + phdr.p_memsz;
            if (c->landmarkAddr < start || c->landmarkAddr >= end)
                continue;
            
            for (int j = 0; j < info->dlpi_phnum; ++j) {
                const auto& p = info->dlpi_phdr[j];
                if (p.p_type == PT_LOAD && (p.p_flags & PF_X) != 0) {
                    c->range.start = info->dlpi_addr + p.p_vaddr;
                    c->range.size = p.p_memsz;
                    return 1;
                }
            }
        }
        return 0;
    }, &ctx);

    return ctx.range;
}

[[nodiscard]] inline NS_OBF_FLATTEN int readTracerPid() noexcept
{
    const int fd = LinuxPlatformApi::open("/proc/self/status", 0 );
    if (fd < 0)
        return -1;
    char buf[2048];
    const auto n = LinuxPlatformApi::pread(fd, buf, sizeof(buf) - 1, 0);
    LinuxPlatformApi::close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    const char* line = buf;
    while (line && *line) {
        if (const char* nl = static_cast<const char*>(__builtin_strchr(line, '\n')))
            *const_cast<char*>(nl) = '\0';
        if (__builtin_strncmp(line, "TracerPid:", 10) == 0) {
            int pid = 0;
            for (const char* p = line + 10; *p >= '0' && *p <= '9'; ++p)
                pid = pid * 10 + (*p - '0');
            return pid;
        }
        line += __builtin_strlen(line) + 1;
    }
    return -1;
}



inline NS_OBF_FLATTEN void tick() noexcept
{
    static std::uint64_t baselineHash{0};
    static std::uint64_t lastTickNs{0};
    static bool textAnomalyLogged{false};
    static bool tracerAnomalyLogged{false};

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const std::uint64_t nowNs = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL
        + static_cast<std::uint64_t>(ts.tv_nsec);
    if (lastTickNs != 0 && nowNs - lastTickNs < 15'000'000'000ULL)
        return;
    lastTickNs = nowNs;

    const TextRange range = ownTextRange(reinterpret_cast<const void*>(&self_integrity::tick));
    if (range.start == 0)
        return;
    const std::uint64_t hash = fnv1a(reinterpret_cast<const std::byte*>(range.start), range.size);

    if (baselineHash == 0) {
        baselineHash = hash;
        
        
        
        
        
        
        
        {
            char path[192];
            if (ns_paths::joinFormat(path, sizeof(path), "ns_module_integrity", "_%d",
                    LinuxPlatformApi::processId())) {
                if (const int fd = LinuxPlatformApi::open(path, 0x41 , 0600); fd >= 0) {
                    char line[160];
                    const int len = std::snprintf(line, sizeof(line),
                        "NSINT01 %d %lx %zx %llx\n", LinuxPlatformApi::processId(),
                        static_cast<unsigned long>(range.start), range.size,
                        static_cast<unsigned long long>(hash));
                    if (len > 0)
                        LinuxPlatformApi::write(fd, line, static_cast<std::size_t>(len));
                    LinuxPlatformApi::close(fd);
                }
            }
        }
    } else if (hash != baselineHash && !textAnomalyLogged) {
        textAnomalyLogged = true;
        gui_log::write("[tamper] own .text checksum drift (baseline %llx, now %llx, range %lx+%zx)",
            static_cast<unsigned long long>(baselineHash), static_cast<unsigned long long>(hash),
            static_cast<unsigned long>(range.start), range.size);
    }

    const int tracerPid = readTracerPid();
    if (tracerPid > 0 && !tracerAnomalyLogged) {
        tracerAnomalyLogged = true;
        gui_log::write("[tamper] a tracer is attached (pid %d)", tracerPid);
    }
}

} 
