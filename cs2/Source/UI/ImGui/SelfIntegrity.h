#pragma once

#include <cstddef>
#include <cstdint>
#include <elf.h>
#include <link.h>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/ObfAnnotations.h>

// Self-integrity watchdog (present thread, every ~15 s - see GUI::render).
//
// Two tamper signals, both log-only (gui.log is anomaly-only: a healthy session never
// writes, so any line here IS the alert):
//  1. Own .text checksum drift. Baseline is established on the FIRST present frame - by
//     then every init-time patch (vtables, GOTs, cvar force-writes) has settled, and the
//     injector (memfd or gdb) has already detached: dlopen returned before any render.
//     A later drift means someone patched or breakpointed (0xCC) our code in memory.
//  2. A tracer attached to the process (TracerPid != 0) while running - someone debugging
//     the module live. A tracer present during module INIT (the injector itself) never
//     reaches this check because the present hook does not run until after it detached.
//
// Deliberately NOT a hard kill/exit: the module runs inside a live game process (see the
// map-transition crash history) and a defensive self-destruct is a far worse failure mode
// than a logged anomaly.
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

// Executable PT_LOAD range of the module that owns `landmark` (a pointer into our own
// code). dl_iterate_phdr here is OUR call - the VAC-hardening GOT hook filters only
// steamclient's calls, not ours, and our link_map node stays intact by design.
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
            // landmark module found: return its exec segment
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
    const int fd = LinuxPlatformApi::open("/proc/self/status", 0 /* O_RDONLY */);
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

// Call every frame from the present thread; internally throttled. Establishes the baseline
// on the first call, then re-checks.
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

} // namespace self_integrity
