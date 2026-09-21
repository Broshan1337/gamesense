#pragma once

#include <cstdint>
#include <cstring>

#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/CrashGuard.h>
#include <Utils/NsStr.h>

// Crash diagnostic logger (Linux). A SIGSEGV/SIGBUS inside a hook callback otherwise just kills
// the game with a coredump that then needs a gdb session to decode. The handler writes the
// faulting PC and fault address resolved to module+offset (any mapped module, not just ours -
// the crash that motivated the module table sat in the Vulkan ICD), plus the breadcrumb trace
// ring, to /tmp/gamesense_crash_<pid>_<tid>.txt. Feeding the two numbers into
// scratchpad/crash_diag.py re-derives the crash site offline.
//
// Covered signals: SEGV/BUS/ILL (memory faults) plus ABRT/FPE/TRAP/SYS. The
// abort family matters because abort-path deaths (heap corruption aborts,
// terminate() under -fno-exceptions, seccomp traps) otherwise bypass this
// logger entirely: Breakpad still owns those signals and its minidump goes
// wherever Steam points it, leaving no local trace. Taking them over means
// aborts now die with our log + a core dump instead of a minidump -
// deliberate tradeoff, local diagnostics win for this project.
//
// Deliberately minimal inside the handler: open/write/close through the raw
// platform API, no allocations, no symbol lookups, no printf-family calls -
// signal-safety rules forbid everything else. The handler ends by restoring
// the default disposition and re-raising so the normal crash path still runs;
// it never returns through the interrupted frame.
//
// Known gap (not fixed here): SA_ONSTACK is set but no alt stack is
// registered (sigaltstack is per-thread; registering only the installing
// thread's would give a false sense of coverage), so a genuine stack
// overflow still kills the handler with the stack.
namespace CrashLogger
{
    struct ModuleInfo {
        std::uintptr_t base{0};
        std::uintptr_t extent{0};   // conservative: base of last mapping until end of its range
        const char* name{"?"};
    };

    // Filled once by scanMappedModules() during install (NOT in signal context).
    inline ModuleInfo clientModule{0, 0, "libclient"};
    inline ModuleInfo ourModule{0, 0, "libMangoHud"};

    [[nodiscard]] inline std::size_t strLen(const char* s) noexcept
    {
        std::size_t n = 0;
        while (s[n])
            ++n;
        return n;
    }

    // Every mapped file, recorded at install time so the handler can resolve ANY pc/fault
    // address to "basename+offset" (driver ICDs, libc, the game's own modules - the crash that
    // motivated this printed a raw pc because it sat in the Vulkan ICD, a module the two
    // hardcoded entries below never knew about). Merged per basename across the many mapping
    // lines one file produces.
    // 4096: a Steam-runtime CS2 maps several hundred files, and the old 160 overflowed - the
    // 2026-08-29 device-lost crash then dumped a raw pc because its module (high address,
    // recorded late) never made the table. 512 overflowed AGAIN with the 2026-09-06 inject
    // crash (pc in our own late-mapped memfd DSO printed raw). 4096 x 96B = ~393KB of BSS.
    inline constexpr std::size_t kMaxModules = 4096;
    struct MappedModule {
        std::uintptr_t base{0};
        std::uintptr_t end{0};
        char name[80]{};
    };
    inline MappedModule modules[kMaxModules];
    inline std::size_t moduleCount{0};

    // Diagnostic breadcrumbs. Features record small event codes at each step of risky paths
    // (single 64-bit stores on the game thread); the signal handler dumps the tail of the ring
    // with everything else, so a crash reports the exact last step instead of a PC to guess at.
    // Deliberately plain (non-atomic) stores: writer and reader are the same thread, and torn
    // reads are acceptable for diagnostics.
    inline constexpr std::size_t kTraceCapacity = 64;
    inline volatile std::uint64_t traceRing[kTraceCapacity]{};
    inline std::uint32_t traceWriteIndex{0};

    inline void trace(std::uint64_t code) noexcept
    {
        traceRing[traceWriteIndex & (kTraceCapacity - 1)] = code;
        ++traceWriteIndex;
    }

    inline void writeAll(int fd, const char* data, std::size_t length) noexcept
    {
        while (length > 0) {
            const auto written = LinuxPlatformApi::write(fd, data, static_cast<unsigned>(length));
            if (written <= 0)
                return;
            data += written;
            length -= static_cast<std::size_t>(written);
        }
    }

    inline void appendString(int fd, const char* text) noexcept
    {
        writeAll(fd, text, strLen(text));
    }

    inline void appendHex(int fd, std::uint64_t value) noexcept
    {
        char buffer[18] = "0x";
        int i = 2;
        if (value == 0)
            buffer[i++] = '0';
        char digits[16];
        int digitCount = 0;
        while (value != 0) {
            const auto digit = value & 0xFu;
            digits[digitCount++] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
            value >>= 4;
        }
        while (digitCount > 0 && i < static_cast<int>(sizeof(buffer)))
            buffer[i++] = digits[--digitCount];
        writeAll(fd, buffer, static_cast<std::size_t>(i));
    }

    // Prints an address as "module+offset" when it falls inside any recorded module, else raw hex.
    inline void describeAddress(int fd, std::uintptr_t address) noexcept
    {
        for (std::size_t i = 0; i < moduleCount; ++i) {
            const auto& module = modules[i];
            if (address >= module.base && address < module.end) {
                appendString(fd, module.name);
                appendString(fd, "+");
                appendHex(fd, address - module.base);
                return;
            }
        }
        for (const auto& module : {ourModule, clientModule}) {
            if (module.base && address >= module.base && address < module.base + module.extent) {
                appendString(fd, module.name);
                appendString(fd, "+");
                appendHex(fd, address - module.base);
                return;
            }
        }
        appendHex(fd, address);
    }

    [[nodiscard]] inline bool lineMatches(const char* lineStart, std::size_t lineLength, const char* needle) noexcept
    {
        const auto needleLength = strLen(needle);
        if (lineLength < needleLength)
            return false;
        return std::memcmp(lineStart + lineLength - needleLength, needle, needleLength) == 0;
    }

    inline void updateSpecialModule(ModuleInfo& module, std::uintptr_t low, std::uintptr_t high) noexcept
    {
        if (module.base == 0 || low < module.base)
            module.base = low;
        if (high > module.base + module.extent)
            module.extent = high - module.base;
    }

    inline void noteMapping(std::uintptr_t low, std::uintptr_t high, const char* pathStart, std::size_t pathLength) noexcept
    {
        if (low == 0 || high == 0 || low >= high)
            return;
        // Memfd-loaded modules end in " (deleted)" (e.g. "/memfd:libMangoHud.so (deleted)"),
        // so the suffix match below misses our own DSO - match it by prefix instead.
        if (lineMatches(pathStart, pathLength, "/libclient.so"))
            updateSpecialModule(clientModule, low, high);
        else if (pathLength >= 21 && std::memcmp(pathStart, "/memfd:libMangoHud.so", 21) == 0)
            updateSpecialModule(ourModule, low, high);
        else if (lineMatches(pathStart, pathLength, "libMangoHud.so"))
            updateSpecialModule(ourModule, low, high);

        // Generic table: basename = text after the last '/'.
        std::size_t nameStart = 0;
        for (std::size_t i = pathLength; i-- > 0;) {
            if (pathStart[i] == '/') {
                nameStart = i + 1;
                break;
            }
        }
        const char* name = pathStart + nameStart;
        const std::size_t nameLength = pathLength - nameStart;
        if (nameLength == 0 || nameLength >= sizeof(modules[0].name))
            return;

        for (std::size_t i = 0; i < moduleCount; ++i) {
            auto& module = modules[i];
            if (module.end == low && std::memcmp(module.name, name, nameLength) == 0 && module.name[nameLength] == 0) {
                module.end = high; // continuation of a file already recorded
                return;
            }
        }
        if (moduleCount < kMaxModules) {
            auto& module = modules[moduleCount++];
            module.base = low;
            module.end = high;
            std::memcpy(module.name, name, nameLength);
            module.name[nameLength] = 0;
        }
    }

    [[nodiscard]] inline std::uintptr_t parseHex(const char* begin, std::size_t length) noexcept
    {
        std::uintptr_t value = 0;
        for (std::size_t i = 0; i < length; ++i) {
            const char c = static_cast<char>(begin[i] | 32);
            unsigned digit;
            if (begin[i] >= '0' && begin[i] <= '9')
                digit = begin[i] - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else
                return 0;
            value = value * 16 + digit;
        }
        return value;
    }

    inline void processMapLine(const char* lineStart, std::size_t lineLength) noexcept
    {
        // Format: low-high perms offset dev inode path
        std::size_t dash = 0;
        while (dash < lineLength && lineStart[dash] != '-')
            ++dash;
        if (dash == 0 || dash + 1 >= lineLength)
            return;
        const auto low = parseHex(lineStart, dash);
        std::size_t spaceAfterHigh = dash + 1;
        while (spaceAfterHigh < lineLength && lineStart[spaceAfterHigh] != ' ')
            ++spaceAfterHigh;
        if (spaceAfterHigh >= lineLength)
            return;
        const auto high = parseHex(lineStart + dash + 1, spaceAfterHigh - dash - 1);

        // Path starts after the LAST space (perms/offset/dev/inode contain none).
        std::size_t pathStart = 0;
        for (std::size_t i = lineLength; i-- > 0;) {
            if (lineStart[i] == ' ') {
                pathStart = i + 1;
                break;
            }
        }
        if (pathStart >= lineLength)
            return;   // anonymous mapping
        noteMapping(low, high, lineStart + pathStart, lineLength - pathStart);
    }

    // One-time pass over /proc/self/maps recording the lowest base and highest end per module of
    // interest. Runs at injection time only, never inside the signal handler.
    inline void scanMappedModules() noexcept
    {
        const int fd = LinuxPlatformApi::open("/proc/self/maps", 0 /* O_RDONLY */);
        if (fd < 0)
            return;

        constexpr std::size_t kMaxCarry = 1024;
        char chunk[4096];
        char carry[kMaxCarry];
        std::size_t carryLength = 0;
        off_t position = 0;

        while (true) {
            const auto got = LinuxPlatformApi::pread(fd, chunk, sizeof(chunk), position);
            if (got <= 0)
                break;
            position += static_cast<off_t>(got);

            std::size_t lineBegin = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(got); ++i) {
                if (chunk[i] != '\n')
                    continue;
                if (carryLength > 0) {
                    // Line continued across chunk boundaries: finish it in the carry buffer.
                    const auto room = kMaxCarry - carryLength;
                    const auto part = (i - lineBegin) < room ? (i - lineBegin) : room;
                    std::memcpy(carry + carryLength, chunk + lineBegin, part);
                    processMapLine(carry, carryLength + part);
                    carryLength = 0;
                } else {
                    processMapLine(chunk + lineBegin, i - lineBegin);
                }
                lineBegin = i + 1;
            }

            carryLength = 0;
            const std::size_t remaining = static_cast<std::size_t>(got) - lineBegin;
            if (remaining > 0 && remaining < kMaxCarry) {
                std::memcpy(carry, chunk + lineBegin, remaining);
                carryLength = remaining;
            }
        }
        LinuxPlatformApi::close(fd);
    }

    // Appends a decimal integer (signal-safety: no printf).
    [[nodiscard]] inline char* appendDecimal(char* out, int value) noexcept
    {
        if (value < 0) {
            *out++ = '-';
            value = -value;
        }
        char digits[12];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count > 0)
            *out++ = digits[--count];
        return out;
    }

    [[nodiscard]] inline std::uintptr_t contextInstructionPointer(void* ucontextVoid) noexcept
    {
#if defined(__x86_64__)
        auto* ucontext = static_cast<ucontext_t*>(ucontextVoid);
        if (!ucontext)
            return 0;
        return static_cast<std::uintptr_t>(ucontext->uc_mcontext.gregs[REG_RIP]);
#else
        return 0;
#endif
    }

    extern "C" {
    // inline: multiple TUs include this header now (GUI.cpp, VulkanHook.cpp breadcrumb their
    // present-path stages); inline keeps the definition merged instead of multiply defined.
    inline void handleSignal(int signalNumber, siginfo_t* info, void* ucontextVoid) noexcept
    {
        // guarded-call bounce BEFORE any report work: a thread that armed a CrashGuard
        // expected a possible fault (network-transition windows) and handles the bounce itself
        crash_guard::bounceIfArmed();

        const std::uintptr_t pc = contextInstructionPointer(ucontextVoid);

        // Per-THREAD file: two threads can fault near-simultaneously (seen 2026-09-06: two
        // physics-event spew threads) and a shared path interleaved their writes line-by-line,
        // producing a self-contradictory report (crash A's pc + crash B's registers).
        char path[64];
        {
            NS_STR(crashPrefix, "/tmp/gamesense_crash_");
            char* w = path;
            std::memcpy(w, crashPrefix, 21);
            w += 21;
            w = appendDecimal(w, LinuxPlatformApi::processId());
            *w++ = '_';
            w = appendDecimal(w, LinuxPlatformApi::threadId());
            std::memcpy(w, ".txt", 5);
        }
        const int fd = LinuxPlatformApi::open(path, /* O_WRONLY|O_CREAT|O_TRUNC */ 0x41 | 01000);
        if (fd >= 0) {
            NS_STR(brandCrash, "Neversnooze crash: signal ");
            appendString(fd, brandCrash);
            appendHex(fd, static_cast<std::uint32_t>(signalNumber));
            appendString(fd, " (si_code ");
            appendHex(fd, info ? static_cast<std::uint32_t>(info->si_code) : 0);
            appendString(fd, ")");
            appendString(fd, "\npc = ");
            describeAddress(fd, pc);
            appendString(fd, "\nfault address = ");
            describeAddress(fd, info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0);
            // raw values + the two module bases that matter most: the table can still miss a
            // module (mapped after the install scan, or the table overflowed) and these make
            // the crash site computable by hand either way.
            appendString(fd, "\npc raw = ");
            appendHex(fd, pc);
            appendString(fd, "\nfault raw = ");
            appendHex(fd, info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0);
            appendString(fd, "\nclient base = ");
            appendHex(fd, clientModule.base);
            appendString(fd, "\nour base = ");
            appendHex(fd, ourModule.base);
            appendString(fd, "\nmodules recorded = ");
            appendHex(fd, moduleCount);

            // General-purpose registers (resolved like pc) - with the disassembly of the faulting
            // site this names exactly WHICH pointer was null and where it came from.
            if (auto* ucontext = static_cast<ucontext_t*>(ucontextVoid)) {
                static constexpr const char* kRegNames[] = {"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rdi", "rsi", "rbp", "rbx", "rdx", "rax", "rcx", "rsp"};
                for (int reg = 0; reg <= REG_RSP; ++reg) {
                    appendString(fd, "\n");
                    appendString(fd, kRegNames[reg]);
                    appendString(fd, " = ");
                    describeAddress(fd, static_cast<std::uintptr_t>(ucontext->uc_mcontext.gregs[reg]));
                }
                appendString(fd, "\nrip = ");
                describeAddress(fd, static_cast<std::uintptr_t>(ucontext->uc_mcontext.gregs[REG_RIP]));

                // The faulting instruction BYTES (up to 16, clamped inside the containing
                // module). Decisive for in-module faults: symbol names can be ICF-folded or
                // stale (the 2026-09-19 map-change SI_KERNEL crash burned a session on a
                // folded "loadVariable" name), and for #GP/SI_KERNEL faults the reported RIP
                // is a RETURN address, not the faulting instruction - the bytes are the only
                // ground truth. Reading is safe: pc was attributed into a mapped module range.
                {
                    std::uintptr_t codeEnd = 0;
                    for (std::size_t i = 0; i < moduleCount; ++i) {
                        if (pc >= modules[i].base && pc < modules[i].end) {
                            codeEnd = modules[i].end;
                            break;
                        }
                    }
                    if (!codeEnd) {
                        for (const auto& module : {ourModule, clientModule}) {
                            if (module.base && pc >= module.base && pc < module.base + module.extent) {
                                codeEnd = module.base + module.extent;
                                break;
                            }
                        }
                    }
                    if (codeEnd) {
                        const auto* code = reinterpret_cast<const unsigned char*>(pc);
                        appendString(fd, "\npc bytes =");
                        for (int i = 0; i < 16 && reinterpret_cast<std::uintptr_t>(code + i) < codeEnd; ++i) {
                            appendString(fd, " ");
                            appendHex(fd, code[i]);
                        }
                    }
                }

                // Poor-man's stack walk: the return-address chain lives on the stack; dumping the
                // top of it resolved to module+offset usually shows who called into the faulting
                // code (our hook frames would show as libMangoHud+... entries between game frames).
                appendString(fd, "\nstack:");
                const auto* sp = reinterpret_cast<std::uintptr_t*>(ucontext->uc_mcontext.gregs[REG_RSP]);
                for (int word = 0; word < 96; ++word) {
                    if (word % 8 == 0) {
                        appendString(fd, "\n");
                        appendHex(fd, reinterpret_cast<std::uintptr_t>(sp + word));
                        appendString(fd, ":");
                    }
                    appendString(fd, " ");
                    describeAddress(fd, sp[word]);
                }
                appendString(fd, "\n");
            }

            const std::uint32_t written = traceWriteIndex;
            const std::uint32_t shown = written < kTraceCapacity ? written : 32;
            appendString(fd, "\ntrace:");
            for (std::uint32_t i = written - shown; i < written; ++i) {
                appendString(fd, " ");
                appendHex(fd, traceRing[i & (kTraceCapacity - 1)]);
            }
            appendString(fd, "\n");
            LinuxPlatformApi::close(fd);
        }

        // Default disposition + re-raise: chain into the normal crash path instead of returning.
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(signalNumber, &sa, nullptr);
        raise(signalNumber);
        _exit(128 + signalNumber);   // effectively unreachable; keeps the flow self-evident
    }
    }

    inline void install() noexcept
    {
        scanMappedModules();
        // Name-agnostic self-resolution: the generic table merged by basename can miss the
        // memfd-loaded DSO (its maps line ends in " (deleted)", and OTHER deleted files merge
        // into the same basename entry). Find whichever recorded module contains one of our
        // own function addresses - that IS our module, whatever it is called in maps.
        const auto selfAddress = reinterpret_cast<std::uintptr_t>(&install);
        for (std::size_t i = 0; i < moduleCount; ++i) {
            if (modules[i].base <= selfAddress && selfAddress < modules[i].end) {
                ourModule.base = modules[i].base;
                ourModule.extent = modules[i].end - modules[i].base;
                break;
            }
        }
        struct sigaction sa{};
        sa.sa_sigaction = &handleSignal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        // SEGV/BUS/ILL: memory faults. ABRT: abort()/terminate()/asserts (ours
        // or the engine's - previously Breakpad-only, locally silent). FPE:
        // arithmetic faults. TRAP: stray breakpoints. SYS: seccomp denials.
        // SIGPIPE/XCPU/XFSZ deliberately excluded (semantically noisy, and the
        // game may rely on default/ignored dispositions for them).
        for (const int signalNumber : {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP, SIGSYS})
            sigaction(signalNumber, &sa, nullptr);
    }

    // Re-asserts our handlers for signals the game has since replaced with its own. The
    // engine/breakpad installs crash handlers lazily per subsystem - the 2026-09-13 airhorn
    // crashes died with NO gamesense_crash report because our SIGABRT handler (installed at
    // init) had been overwritten by the time the abort fired. Call this periodically (present
    // thread); a sigaction query is a cheap syscall and this only rewrites drifted slots.
    inline void reassert() noexcept
    {
        struct sigaction current{};
        struct sigaction ours{};
        ours.sa_sigaction = &handleSignal;
        ours.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&ours.sa_mask);
        for (const int signalNumber : {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP, SIGSYS}) {
            if (sigaction(signalNumber, nullptr, &current) != 0)
                continue;
            if (current.sa_sigaction != ours.sa_sigaction || current.sa_flags != ours.sa_flags)
                sigaction(signalNumber, &ours, nullptr);
        }
    }
}
