#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <CS2/Classes/IEngineClient.h>
#include <GameClient/EngineClientPointer.h>
#include <Utils/RetAddrSpoofer.h>

// TEMPORARY in-game verification harness for the untested combat block (backtrack/lagcomp,
// force-shot, max-accuracy gate, extrapolation, autowall). Prints throttled one-line events into
// the game's OWN developer console - open it with ~ and watch the [tags], no host-side files.
//
// Output path is the proven HitLog mechanism: ExecuteClientCommand("echo ...") through CEngineClient
// (libengine2.so vtable slot kExecuteClientCommandVtableSlot). The command is QUEUED by the engine
// (drains next frame pass), which is why every write() is throttled per-tag instead of being called
// on every tick. All texts are built by us here - never network/user input - so nothing can inject a
// second command through the buffer's newline separator.
//
// Tags seen in the console during verification:
//   [lagcomp]   per-second record-capture summary (runs while Backtrack or Extrapolate is on)
//   [extrap]    an extrapolated FUTURE record was produced (lead ticks + predicted offset)
//   [bt-shot]   rage shot redirected onto a PAST (backtrack) record + the tick stamped into input_history
//   [exp-shot]  rage shot redirected onto an EXTRAPOLATED (future) record + stamped tick
//   [forceshot] force-shot PRESS/HOLD decisions while a target is in FOV
//   [maxacc]    triggerbot max-accuracy gate PASS/HOLD (with MaxAccuracyOnly on)
//   [autowall]  triggerbot visibility: clear LOS, or wall thickness vs limit FIRE/HOLD
// Out at namespace scope: its default member initializer can't be used by an inline-static member
// of VerifyConsole while the class itself is still incomplete.
// NOTE: no default member initializer (last{-1.0e9} was removed): with one, clang emits a
// dynamic initializer + __cxa_guard_* for the inline-static array - undefined under -nostdlib
// (GCC constant-folded it; clang does not). Safe: `last` is only read for entries that were
// written first (the loop runs over stampCount, and stamp creation sets tag+last together).
struct VerifyLogTagStamp {
    const char* tag;
    double last;
};

class VerifyConsole {
public:
    // minIntervalSeconds <= 0 means "log every call" (use only for genuinely rare events).
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
            // A newline would split the buffer into a second command - flatten anything unexpected.
            *out++ = (*t == '\n') ? ' ' : *t;
        }
        *out = '\0';

        RetAddrSpoofer::spoof(reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand))(engine, 0, command, 1);
    }

private:
    // Returns false when this tag printed within its interval. Tag pointers are compile-time
    // literals, so address comparison identifies tags.
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
        return true; // over the table size: print unthrottled rather than drop verification data
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
