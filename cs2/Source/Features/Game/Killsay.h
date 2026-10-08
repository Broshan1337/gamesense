#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Game/KillsayConfigVariables.h>
#include <GameClient/EngineClientPointer.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/PlayerSlotLookup.h>
#include <HookContext/HookContextMacros.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/Random.h>
#include <Utils/RetAddrSpoofer.h>
#include <Utils/VerifyConsole.h>












template <typename HookContext>
class Killsay {
public:
    explicit Killsay(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event || !GET_CONFIG_VAR(KillsayEnabled))
            return;

        if (!game_events::is(event, "player_death"))
            return;

        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        const auto victimSlot = game_events::entityForKey(event, "userid");
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();
        if (!lookup.isValidSlot(victimSlot))
            return;

        char phrase[160];
        if (!pickPhrase(phrase))
            return;

        char substituted[192];
        substituteName(phrase, lookup.nameBySlot(victimSlot), substituted);
        sayThroughConsole(substituted);
    }

private:
    
    
    [[nodiscard]] bool pickPhrase(char (&outPhrase)[160]) const noexcept
    {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (!directoryPath)
            return hintUnavailable();

        char path[512];
        {
            const auto* dir = reinterpret_cast<const char*>(directoryPath.get());
            std::size_t length = 0;
            while (dir[length] != '\0' && length + 1 < sizeof(path) - sizeof("/killsay.txt"))
                ++length;
            std::memcpy(path, dir, length);
            std::memcpy(path + length, "/killsay.txt", sizeof("/killsay.txt"));
        }

        const int fd = LinuxPlatformApi::open(path, 0 );
        if (fd < 0)
            return hintUnavailable();

        char fileBuffer[4096];
        const auto readBytes = LinuxPlatformApi::pread(fd, fileBuffer, sizeof(fileBuffer), 0);
        LinuxPlatformApi::close(fd);
        if (readBytes <= 0)
            return hintUnavailable();

        
        std::size_t lineCount = 0;
        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(readBytes)) {
            const auto lineLength = lineLengthAt(fileBuffer, readBytes, offset);
            if (isEnabledLine(fileBuffer + offset, lineLength))
                ++lineCount;
            offset += lineLength + 1;
        }
        if (lineCount == 0)
            return hintUnavailable();

        auto chosen = static_cast<std::size_t>(Random::floating(0.0f, 1.0f) * static_cast<float>(lineCount));
        if (chosen >= lineCount)
            chosen = lineCount - 1;

        
        std::size_t enabledIndex = 0;
        offset = 0;
        while (offset < static_cast<std::size_t>(readBytes)) {
            const auto lineLength = lineLengthAt(fileBuffer, readBytes, offset);
            if (isEnabledLine(fileBuffer + offset, lineLength)) {
                if (enabledIndex == chosen) {
                    const auto copyLength = lineLength < sizeof(outPhrase) - 1 ? lineLength : sizeof(outPhrase) - 1;
                    std::memcpy(outPhrase, fileBuffer + offset, copyLength);
                    outPhrase[copyLength] = '\0';
                    return true;
                }
                ++enabledIndex;
            }
            offset += lineLength + 1;
        }
        return false;
    }

    [[nodiscard]] static std::size_t lineLengthAt(const char* buffer, std::size_t totalBytes, std::size_t offset) noexcept
    {
        std::size_t length = 0;
        while (offset + length < totalBytes && buffer[offset + length] != '\n')
            ++length;
        
        if (length > 0 && buffer[offset + length - 1] == '\r')
            --length;
        return length;
    }

    [[nodiscard]] static bool isEnabledLine(const char* line, std::size_t length) noexcept
    {
        if (length == 0)
            return false;
        if (line[0] == '#')
            return false;
        return true;
    }

    
    
    static void substituteName(const char* phrase, const char* victimName, char (&out)[192]) noexcept
    {
        constexpr std::string_view nameToken{"{name}"};
        const auto victimLength = std::strlen(victimName);

        std::size_t writeIndex = 0;
        for (std::size_t i = 0; phrase[i] != '\0';) {
            if (phrase[i] == '{' && std::strncmp(phrase + i, nameToken.data(), nameToken.size()) == 0) {
                if (writeIndex + victimLength >= sizeof(out) - 1)
                    break;
                std::memcpy(out + writeIndex, victimName, victimLength);
                writeIndex += victimLength;
                i += nameToken.size();
                continue;
            }
            if (writeIndex >= sizeof(out) - 2)
                break;
            out[writeIndex++] = phrase[i++];
        }
        out[writeIndex] = '\0';
    }

    [[nodiscard]] static bool hintUnavailable() noexcept
    {
        VerifyConsole::write(30.0f, "killsay", "killsay.txt missing or empty - add one line per phrase to the config dir ({name} = victim)");
        return false;
    }

    
    
    
    
    void sayThroughConsole(const char* text) const noexcept
    {
        const EngineClientPointer engineClient{};
        auto* const engine = engineClient.get();
        if (!engine)
            return;
        const auto vtable = *reinterpret_cast<void* const* const*>(engine);
        if (!vtable)
            return;
        const auto executeCommand = vtable[cs2::IEngineClient::kExecuteClientCommandVtableSlot];
        if (!executeCommand)
            return;

        char command[264]{"say "};
        std::size_t writeIndex = 4;
        for (std::size_t i = 0; text[i] != '\0' && writeIndex < sizeof(command) - 1 && i < 130; ++i) {
            const auto c = text[i];
            if (c == '"' || c == ';' || static_cast<unsigned char>(c) < 0x20)
                continue;
            command[writeIndex++] = c;
        }
        command[writeIndex] = '\0';

        RetAddrSpoofer::spoof(reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand))(engine, 0, command, 1);
    }

    HookContext& hookContext;
};
