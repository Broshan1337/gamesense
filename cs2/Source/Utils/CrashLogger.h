#pragma once

#include <cstdint>
#include <cstring>

#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/CrashGuard.h>
#include <Utils/NsPaths.h>
#include <Utils/NsStr.h>



























namespace CrashLogger
{
    struct ModuleInfo {
        std::uintptr_t base{0};
        std::uintptr_t extent{0};   
        const char* name{"?"};
    };

    
    inline ModuleInfo clientModule{0, 0, "libclient"};
    inline ModuleInfo ourModule{0, 0, "libMangoHud"};

    [[nodiscard]] inline std::size_t strLen(const char* s) noexcept
    {
        std::size_t n = 0;
        while (s[n])
            ++n;
        return n;
    }

    
    
    
    
    
    
    
    
    
    inline constexpr std::size_t kMaxModules = 4096;
    struct MappedModule {
        std::uintptr_t base{0};
        std::uintptr_t end{0};
        char name[80]{};
    };
    inline MappedModule modules[kMaxModules];
    inline std::size_t moduleCount{0};

    
    
    
    
    
    inline constexpr std::size_t kTraceCapacity = 64;
    inline volatile std::uint64_t traceRing[kTraceCapacity]{};
    inline std::uint32_t traceWriteIndex{0};

    inline void trace(std::uint64_t code) noexcept
    {
        traceRing[traceWriteIndex & (kTraceCapacity - 1)] = code;
        ++traceWriteIndex;
    }

    
    
    
    inline char crashFilePrefix[160] = "/tmp/gamesense_crash_";
    inline std::size_t crashFilePrefixLength = 21;

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
        
        
        if (lineMatches(pathStart, pathLength, "/libclient.so"))
            updateSpecialModule(clientModule, low, high);
        else if (pathLength >= 21 && std::memcmp(pathStart, "/memfd:libMangoHud.so", 21) == 0)
            updateSpecialModule(ourModule, low, high);
        else if (lineMatches(pathStart, pathLength, "libMangoHud.so"))
            updateSpecialModule(ourModule, low, high);

        
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
                module.end = high; 
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

        
        std::size_t pathStart = 0;
        for (std::size_t i = lineLength; i-- > 0;) {
            if (lineStart[i] == ' ') {
                pathStart = i + 1;
                break;
            }
        }
        if (pathStart >= lineLength)
            return;   
        noteMapping(low, high, lineStart + pathStart, lineLength - pathStart);
    }

    
    
    inline void scanMappedModules() noexcept
    {
        const int fd = LinuxPlatformApi::open("/proc/self/maps", 0 );
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
    
    
    inline void handleSignal(int signalNumber, siginfo_t* info, void* ucontextVoid) noexcept
    {
        
        
        crash_guard::bounceIfArmed();

        const std::uintptr_t pc = contextInstructionPointer(ucontextVoid);

        
        
        
        char path[192];
        {
            char* w = path;
            std::memcpy(w, crashFilePrefix, crashFilePrefixLength);
            w += crashFilePrefixLength;
            w = appendDecimal(w, LinuxPlatformApi::processId());
            *w++ = '_';
            w = appendDecimal(w, LinuxPlatformApi::threadId());
            std::memcpy(w, ".txt", 5);
        }
        const int fd = LinuxPlatformApi::open(path,  0x41 | 01000);
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

        
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(signalNumber, &sa, nullptr);
        raise(signalNumber);
        _exit(128 + signalNumber);   
    }
    }

    inline void install() noexcept
    {
        
        
        ns_paths::init();
        if (ns_paths::joinLog(crashFilePrefix, sizeof(crashFilePrefix), "gamesense_crash_"))
            crashFilePrefixLength = ns_paths::length(crashFilePrefix);

        scanMappedModules();
        
        
        
        
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
        
        
        
        
        
        for (const int signalNumber : {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP, SIGSYS})
            sigaction(signalNumber, &sa, nullptr);
    }

    
    
    
    
    
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
