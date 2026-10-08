#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <CS2/Classes/IEngineClient.h>
#include <GameClient/EngineClientPointer.h>
#include <Utils/RetAddrSpoofer.h>

























struct VerifyLogTagStamp {
    const char* tag;
    double last;
};

class VerifyConsole {
public:
    
    static void write(float minIntervalSeconds, const char* tag, const char* fmt, ...) noexcept
    {
        if (!throttled(minIntervalSeconds, tag))
            return;

        char text[128];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);

        const EngineClientPointer engineClient{};
        if (!engineClient)
            return;
        auto* const engine = engineClient.get();
        const auto vtable = *reinterpret_cast<void* const* const*>(engine);
        if (!vtable)
            return;
        const auto executeCommand = vtable[cs2::IEngineClient::kExecuteClientCommandVtableSlot];
        if (!executeCommand)
            return;

        char command[176]{"echo "};
        auto* out = command + 5;
        *out++ = '[';
        for (const auto* t = tag; *t && out < command + sizeof(command) - 1; )
            *out++ = *t++;
        *out++ = ']';
        *out++ = ' ';
        for (const auto* t = text; *t && out < command + sizeof(command) - 1; ++t) {
            
            *out++ = (*t == '\n') ? ' ' : *t;
        }
        *out = '\0';

        RetAddrSpoofer::spoof(reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand))(engine, 0, command, 1);
    }

private:
    
    
    [[nodiscard]] static bool throttled(float minIntervalSeconds, const char* tag) noexcept
    {
        if (minIntervalSeconds <= 0.0f)
            return true;

        const double now = monotonicSeconds();
        for (int i = 0; i < stampCount; ++i) {
            if (stamps[i].tag == tag) {
                if (now - stamps[i].last < static_cast<double>(minIntervalSeconds))
                    return false;
                stamps[i].last = now;
                return true;
            }
        }
        if (stampCount < kMaxStamps) {
            stamps[stampCount].tag = tag;
            stamps[stampCount].last = now;
            ++stampCount;
        }
        return true; 
    }

    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
    }

    static constexpr int kMaxStamps = 16;

    inline static VerifyLogTagStamp stamps[kMaxStamps];
    inline static int stampCount = 0;
};
