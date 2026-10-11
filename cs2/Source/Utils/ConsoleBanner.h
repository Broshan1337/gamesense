#pragma once

#include <CS2/Classes/IEngineClient.h>
#include <GameClient/EngineClientPointer.h>
#include <Utils/NsStr.h>
#include <Utils/RetAddrSpoofer.h>

// Console banner (gamesneeze res/console.png style): the NS logo printed
// into the game console once at the first rendered frame, ahead of the
// [status] lines. Font = figlet "big" (pure block letters - the first cut
// used "slant" whose diagonal strokes read as garbage in the console's
// monospace face; 2026-10-13 "scuffed" report). Every line rides NS_STR -
// the brand must never appear in the binary as plaintext (strings check
// stays clean; the vault kills both .rodata and movbas leaks).
namespace console_banner
{

inline void printOnce() noexcept
{
    static bool printed = false;
    if (printed)
        return;
    printed = true;

    NS_STR(line1, " _   _ ________      ________ _____   _____ _   _  ____   ____ ____________ ");
    NS_STR(line2, "| \\ | |  ____\\ \\    / /  ____|  __ \\ / ____| \\ | |/ __ \\ / __ \\___  /  ____|");
    NS_STR(line3, "|  \\| | |__   \\ \\  / /| |__  | |__) | (___ |  \\| | |  | | |  | | / /| |__   ");
    NS_STR(line4, "| . ` |  __|   \\ \\/ / |  __| |  _  / \\___ \\| . ` | |  | | |  | |/ / |  __|  ");
    NS_STR(line5, "| |\\  | |____   \\  /  | |____| | \\ \\ ____) | |\\  | |__| | |__| / /__| |____ ");
    NS_STR(line6, "|_| \\_|______|   \\/   |______|_|  \\_\\_____/|_| \\_|\\____/ \\____/_____|______|");

#ifdef NDEBUG
    NS_STR(buildLine, "neversnooze :: release build");
#else
    NS_STR(buildLine, "neversnooze :: debug build");
#endif

    const char* const lines[] = {line1, line2, line3, line4, line5, line6, "", buildLine};

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

    for (const char* line : lines) {
        char command[128]{"echo "};
        auto* out = command + 5;
        for (const auto* t = line; *t && out < command + sizeof(command) - 1; ++t)
            *out++ = *t;
        *out = '\0';
        RetAddrSpoofer::spoof(reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand))(engine, 0, command, 1);
    }
}

}