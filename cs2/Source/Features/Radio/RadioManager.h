#pragma once

#include <atomic>
#include <cerrno>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Constants/TeamNumberConstants.h>
#include <CS2/Constants/DllNames.h>
#include <Features/Radio/RadioConfigVariables.h>
#include <Features/Radio/RadioNowPlayingParser.h>
#include <Features/Radio/RadioStationParser.h>
#include <GameClient/Bind.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>
#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <Utils/NsPaths.h>
#include <Utils/NsStr.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/CrashLogger.h>
#include <Utils/StringBuilder.h>
#include <Utils/VerifyConsole.h>



















extern "C" char** environ;

template <typename HookContext>
class RadioManager {
public:
    explicit RadioManager(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    static constexpr int kMaxResults = 40;

    

    void startBrowseLocal() const noexcept
    {
        beginFetch("https://opml.radiotime.com/Browse.ashx?c=local&render=json&formats=mp3,aac");
    }

    
    
    void startSearch(std::string_view query) const noexcept
    {
        if (query.empty()) {
            startBrowseLocal();
            return;
        }
        StringBuilderStorage<512> storage;
        auto builder = storage.builder();
        builder.put("https://opml.radiotime.com/Search.ashx?query=", query, "&render=json&formats=mp3,aac");
        beginFetch(builder.cstring());
    }

    
    
    void poll() const noexcept
    {
        bool fetchProcessDone = false;
        if (fetchPid > 0) {
            int status;
            if (::waitpid(fetchPid, &status, WNOHANG) == fetchPid) {
                fetchPid = 0;
                fetchProcessDone = true; 
            }
        }

        if (!fetchPending)
            return;

        resolveRadioPaths();
        const int fd = ::open(resultsPath, O_RDONLY);
        if (fd < 0) {
            
            
            if (fetchProcessDone)
                fetchPending = false;
            return;
        }

        std::size_t total = 0;
        for (;;) {
            const ssize_t bytes = ::read(fd, fetchBuffer + total, sizeof(fetchBuffer) - 1 - total);
            if (bytes <= 0)
                break;
            total += static_cast<std::size_t>(bytes);
            if (total >= sizeof(fetchBuffer) - 1)
                break;
        }
        ::close(fd);
        ::unlink(resultsPath);
        fetchBuffer[total] = '\0';
        fetchPending = false;

        resultCount = RadioStationParser::parse(fetchBuffer, total, results, kMaxResults, resultsHeader, sizeof(resultsHeader));
        resultsDirty = true;
    }

    [[nodiscard]] int stationCount() const noexcept { return resultCount; }
    [[nodiscard]] const RadioStation& station(int index) const noexcept { return results[index]; }
    [[nodiscard]] const char* header() const noexcept { return resultsHeader; }

    
    [[nodiscard]] bool fetching() const noexcept { return fetchPending; }

    
    [[nodiscard]] const char* lastPlayed() const noexcept { return lastPlayedId; }

    
    
    [[nodiscard]] const char* lastPlayedName() const noexcept { return lastPlayedNameBuf; }

    

    
    
    
    
    
    
    
    void updateNowPlaying() const noexcept
    {
        const int volume = static_cast<int>(GET_CONFIG_VAR(radio_vars::Volume));
        if (currentPid > 0 && volume != appliedVolume && ++volumeApplyCounter >= 9) {
            volumeApplyCounter = 0;
            appliedVolume = volume;
            writeVolumeScriptOnce();
            StringBuilderStorage<320> storage;
            auto builder = storage.builder();
            builder.put("exec python3 ", volScriptPath, ' ', radioSocketPath, ' ', volume);
            if (volPid > 0)
                ::waitpid(volPid, nullptr, WNOHANG);
            volPid = spawnHostShell(builder.cstring());
        }

        // Poll by elapsed time so the widget still works with low FPS or a closed menu.
        timespec timestamp{};
        if (::clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
            return;
        const double now = static_cast<double>(timestamp.tv_sec) + timestamp.tv_nsec / 1.0e9;
        if (now < nextNowPlayingPoll)
            return;
        nextNowPlayingPoll = now + 0.5;
        resolveRadioPaths();

        
        if (mprisPid > 0) {
            const auto reaped = ::waitpid(mprisPid, nullptr, WNOHANG);
            if (reaped == mprisPid || (reaped < 0 && errno == ECHILD))
                mprisPid = 0;
        }
        if (volPid > 0 && ::waitpid(volPid, nullptr, WNOHANG) == volPid)
            volPid = 0;

        if (isPlaying()) {
            static_cast<void>(readFileInto(nowPlayingTrackBuf, sizeof(nowPlayingTrackBuf), metaFilePath));
            // The radio itself can be the active MPRIS player. Keep probing metadata.
        }

        
        if (!GET_CONFIG_VAR(radio_vars::ShowMediaPlayers)) {
            clearMpris();
            return;
        }
        if (mprisPid == 0) {
            StringBuilderStorage<2048> storage;
            auto builder = storage.builder();
            builder.put("export PATH=\"$HOME/.nix-profile/bin:/etc/profiles/per-user/$(id -un)/bin:/run/current-system/sw/bin:/usr/bin:/bin:$PATH\"; ",
                        "export XDG_RUNTIME_DIR=\"${XDG_RUNTIME_DIR:-/run/user/$(id -u)}\"; ",
                        "export DBUS_SESSION_BUS_ADDRESS=\"${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}\"; ",
                        "timeout 2s playerctl --all-players metadata --format '{{playerName}}", '\x1f', "{{title}}", '\x1f',
                        "{{artist}}", '\x1f', "{{status}}", '\x1f', "{{mpris:artUrl}}", "' > ");
            appendShellPath(builder, mprisPartPath);
            builder.put(" 2>/dev/null && mv -f ");
            appendShellPath(builder, mprisPartPath);
            builder.put(' ');
            appendShellPath(builder, mprisFilePath);
            builder.put(" || rm -f ");
            appendShellPath(builder, mprisFilePath);
            mprisPid = spawnHostShell(builder.cstring());
        }
        char fileBuffer[8192];
        if (!readFileInto(fileBuffer, sizeof(fileBuffer), mprisFilePath)) {
            clearMpris();
            return;
        }
        MprisNowPlaying playing{};
        if (RadioNowPlayingParser::parseMprisOutput(fileBuffer, playing)) {
            copyText(mprisPlayerBuf, playing.player, static_cast<int>(sizeof(mprisPlayerBuf)));
            copyText(mprisTitleBuf, playing.title, static_cast<int>(sizeof(mprisTitleBuf)));
            copyText(mprisArtistBuf, playing.artist, static_cast<int>(sizeof(mprisArtistBuf)));
            mprisPaused = playing.paused;
            copyText(mprisArtworkBuf, playing.artwork, sizeof(mprisArtworkBuf));
            updateArtwork();
        } else {
            clearMpris();
        }
    }

    [[nodiscard]] const char* nowPlayingTrack() const noexcept { return nowPlayingTrackBuf; }
    [[nodiscard]] const char* mprisPlayerName() const noexcept { return mprisPlayerBuf; }
    [[nodiscard]] const char* mprisTrack() const noexcept { return mprisTitleBuf; }
    [[nodiscard]] const char* mprisArtist() const noexcept { return mprisArtistBuf; }
    [[nodiscard]] const char* mprisArtwork() const noexcept { return mprisArtworkBuf; }
    [[nodiscard]] const char* mprisArtworkFile() const noexcept
    {
        return mprisArtworkBuf[0] && std::strcmp(mprisArtworkBuf, readyArtworkUrl) == 0 ? artworkFilePath : nullptr;
    }
    [[nodiscard]] bool mprisIsPaused() const noexcept { return mprisPaused; }

    
    
    [[nodiscard]] bool consumeDirty() const noexcept
    {
        if (!resultsDirty)
            return false;
        resultsDirty = false;
        return true;
    }

    

    void playResult(int index) const noexcept
    {
        if (index < 0 || index >= resultCount)
            return;
        copyId(lastPlayedId, results[index].id);
        copyText(lastPlayedNameBuf, results[index].text, sizeof(lastPlayedNameBuf));
        pushRecent(results[index].id, results[index].text);
        playId(results[index].id);
    }

    void resume() const noexcept
    {
        if (lastPlayedId[0] != '\0')
            playId(lastPlayedId);
    }

    
    
    
    
    void stop() const noexcept
    {
        NS_DEC(kMarker, kMarkerEnc);
        NS_DEC(kLaunchClientPath, kLaunchClientPathEnc);
        char* const argv[] = {
            const_cast<char*>("steam-runtime-launch-client"),
            const_cast<char*>("--host"),
            const_cast<char*>("--"),
            const_cast<char*>("pkill"),
            const_cast<char*>("-f"),
            const_cast<char*>(kMarker.c_str()),
            nullptr,
        };
        pid_t pid{};
        if (::posix_spawn(&pid, kLaunchClientPath, nullptr, nullptr, argv, environ) == 0)
            ::waitpid(pid, nullptr, 0);

        if (currentPid > 0) {
            ::kill(currentPid, SIGKILL);
            ::waitpid(currentPid, nullptr, 0);
            currentPid = 0;
        }
        if (metaPid > 0) {
            ::kill(metaPid, SIGKILL);
            ::waitpid(metaPid, nullptr, 0);
            metaPid = 0;
        }
        nowPlayingTrackBuf[0] = '\0';
        resolveRadioPaths();
        ::unlink(metaFilePath);
    }

    

    static constexpr int kMaxFavorites = 16;
    static constexpr int kMaxRecent = 8;

    [[nodiscard]] int favoriteCount() const noexcept
    {
        loadFavoritesOnce();
        return favoriteStationCount;
    }

    [[nodiscard]] const char* favoriteId(int index) const noexcept { return favoriteStations[index].id; }
    [[nodiscard]] const char* favoriteName(int index) const noexcept { return favoriteStations[index].name; }

    [[nodiscard]] bool isFavorite(const char* id) const noexcept
    {
        loadFavoritesOnce();
        return favoriteIndexFor(id) >= 0;
    }

    
    void toggleFavorite(const char* id, const char* name) const noexcept
    {
        loadFavoritesOnce();
        const int existing = favoriteIndexFor(id);
        if (existing >= 0) {
            for (int i = existing; i < favoriteStationCount - 1; ++i)
                favoriteStations[i] = favoriteStations[i + 1];
            --favoriteStationCount;
        } else if (favoriteStationCount < kMaxFavorites) {
            copyId(favoriteStations[favoriteStationCount].id, id);
            copyText(favoriteStations[favoriteStationCount].name, name, sizeof(SavedStation::name));
            ++favoriteStationCount;
        } else {
            return; 
        }
        saveFavorites();
    }

    
    void playFavorite(int index) const noexcept
    {
        loadFavoritesOnce();
        if (index < 0 || index >= favoriteStationCount)
            return;
        copyId(lastPlayedId, favoriteStations[index].id);
        copyText(lastPlayedNameBuf, favoriteStations[index].name, sizeof(lastPlayedNameBuf));
        pushRecent(favoriteStations[index].id, favoriteStations[index].name);
        playId(favoriteStations[index].id);
    }

    
    void playRecent(int index) const noexcept
    {
        if (index < 0 || index >= recentStationCount)
            return;
        copyId(lastPlayedId, recentStations[index].id);
        copyText(lastPlayedNameBuf, recentStations[index].name, sizeof(lastPlayedNameBuf));
        playId(recentStations[index].id);
    }

    [[nodiscard]] int recentCount() const noexcept { return recentStationCount; }
    [[nodiscard]] const char* recentId(int index) const noexcept { return recentStations[index].id; }
    [[nodiscard]] const char* recentName(int index) const noexcept { return recentStations[index].name; }

    void onUnload() const noexcept
    {
        stop();
        micBroadcastHardOff();
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool isPlaying() const noexcept
    {
        if (currentPid <= 0)
            return false;
        int status;
        if (::waitpid(currentPid, &status, WNOHANG) == currentPid) {
            currentPid = 0; 
            return false;
        }
        return true;
    }

    void updateMicBroadcast() const noexcept
    {
        
        
        CrashLogger::reassert();
        writeBroadcastScriptOnce();
        const bool want = isPlaying() && GET_CONFIG_VAR(radio_vars::MicBroadcast);
        const bool stationChanged = micBroadcastActive && std::strcmp(micBroadcastStation, lastPlayedId) != 0;
        if (want == micBroadcastActive && !stationChanged) {
            
            
            
            
            
            if (micBroadcastActive && ++voiceKeyReassertCounter >= 128) {
                voiceKeyReassertCounter = 0;
                synthVoiceKey(true, true);
                resolveRadioPaths();
                char keepaliveCommand[288];
                if (std::snprintf(keepaliveCommand, sizeof(keepaliveCommand), "exec sh %s keepalive", micScriptPath) > 0)
                    static_cast<void>(spawnHostShell(keepaliveCommand));
                
                
                
                if (const auto peak = hookContext.template make<CvarSystem>().readFloatConVar("voice_vox_current_peak")) {
                    if (*peak > 0.0f) {
                        capturePeakZeroStreak = 0;
                        capturePeakAnomalyLogged = false;
                    } else if (++capturePeakZeroStreak >= 3 && !capturePeakAnomalyLogged) {
                        capturePeakAnomalyLogged = true;
                        gui_log::write("broadcast: voice capture sees NO signal (peak 0) - the capture-stream move did not stick; check 'pactl list source-outputs' for the cs2 stream");
                    }
                }
            }
            return;
        }
        resolveRadioPaths();
        StringBuilderStorage<384> storage;
        auto builder = storage.builder();
        if (want) {
            builder.put("exec sh ", micScriptPath, " on ", lastPlayedId);
            copyId(micBroadcastStation, lastPlayedId);
            micBroadcastActive = true;
            armTransmission();
        } else {
            builder.put("exec sh ", micScriptPath, " off");
            micBroadcastStation[0] = '\0';
            micBroadcastActive = false;
            disarmTransmission();
        }
        releaseAutoVoiceBindIfIdle();
        static_cast<void>(spawnHostShell(builder.cstring()));
    }

    void micBroadcastHardOff() const noexcept
    {
        if (micBroadcastActive) {
            micBroadcastActive = false;
            micBroadcastStation[0] = '\0';
            disarmTransmission();
        }
        releaseAutoVoiceBindIfIdle();
    }


        
    
    
    
    void runFrameStageNotify() const noexcept
    {
        if (pendingModenable.exchange(false, std::memory_order_relaxed)) {
            auto&& executor = hookContext.template make<EngineCommandExecutor>();
            executor.execute("voice_modenable 1");
            
            
            
            
            
            
            
            
            
            if (!micAlwaysSampled) {
                micAlwaysSampled = true;
                executor.execute("voice_always_sample_mic 1");
                gui_log::write("radio: voice_always_sample_mic 1 - mic capture runs permanently so routed clips always have a stream to move");
            }
        }
        const int action = pendingBindAction.exchange(0, std::memory_order_relaxed);
        if (action == 1) {
            StringBuilderStorage<32> storage;
            auto builder = storage.builder();
            builder.put("bind ", kAutoVoiceKeyName, " +voicerecord");
            hookContext.template make<EngineCommandExecutor>().execute(builder.cstring());
        } else if (action == 2) {
            StringBuilderStorage<32> storage;
            auto builder = storage.builder();
            builder.put("unbind ", kAutoVoiceKeyName);
            hookContext.template make<EngineCommandExecutor>().execute(builder.cstring());
        }
    }

private:
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    static constexpr int kAutoVoiceScancode = 66; 
    static constexpr const char* kAutoVoiceKeyName = "f9";

    void ensureAutoVoiceBind() const noexcept
    {
        if (autoVoiceEngaged)
            return;
        autoVoiceEngaged = true;
        
        
        
        
        pendingBindAction.store(1, std::memory_order_relaxed);
        if (!autoVoiceHinted) {
            autoVoiceHinted = true;
            gui_log::write("broadcast: no keyboard voice key set - auto-bound F9 to +voicerecord for hands-free transmit (unbound on stop)");
        }
    }

    void releaseAutoVoiceBindIfIdle() const noexcept
    {
        if (!autoVoiceEngaged)
            return;
        if (micBroadcastActive)
            return;
        autoVoiceEngaged = false;
        if (synthedKey == kAutoVoiceScancode) {
            
            const LinuxDynamicLibrary sdl{cs2::SDL_DLL};
            if (const auto getState = sdl.getFunctionAddress("SDL_GetKeyboardState").as<sdl3::SDL_GetKeyboardState*>())
                const_cast<std::uint8_t*>(getState(nullptr))[kAutoVoiceScancode] = 0;
            synthedKey = 0;
            voiceKeySynthed = false;
        }
        pendingBindAction.store(2, std::memory_order_relaxed); 
    }

    void synthVoiceKey(bool down, bool force = false) const noexcept
    {
        const int configured = GET_CONFIG_VAR(radio_vars::VoiceKeyBind);
        int key = 0;
        if (configured > Bind::kOff && configured <= Bind::kMaxScancode) {
            key = configured; 
        } else {
            if (configured > Bind::kMaxScancode && !mouseKeyHinted) {
                mouseKeyHinted = true;
                gui_log::write("broadcast: mouse voice keys can't drive PTT - using auto-key F9 instead");
            }
            ensureAutoVoiceBind();
            key = kAutoVoiceScancode;
        }
        if (!force && down == voiceKeySynthed && key == synthedKey)
            return;

        const LinuxDynamicLibrary sdl{cs2::SDL_DLL};
        if (!sdl)
            return;
        if (const auto getState = sdl.getFunctionAddress("SDL_GetKeyboardState").as<sdl3::SDL_GetKeyboardState*>()) {
            auto* state = const_cast<std::uint8_t*>(getState(nullptr));
            if (synthedKey != 0 && synthedKey != key)
                state[synthedKey] = 0; 
            state[key] = down ? 1 : 0;
            synthedKey = down ? key : 0;
        }
        voiceKeySynthed = down;
    }

    
    
    
    void armTransmission() const noexcept
    {
        pendingModenable.store(true, std::memory_order_relaxed);
        synthVoiceKey(true);
    }

    void disarmTransmission() const noexcept
    {
        synthVoiceKey(false);
    }


    
    
    
    
    
    static void writeBroadcastScriptOnce() noexcept
    {
        if (broadcastScriptWritten)
            return;
        broadcastScriptWritten = true;
        resolveRadioPaths();

        
        
        
        
        
        
        
        
        
        static constexpr char kScript[] =
            "#!/bin/sh\n"
            "# mic broadcast: feed the virtual mic and move the game's capture to it.\n"
            "FF=/tmp/ns_mic_radio.pcm\n"
            "PIDF=/tmp/ns_mic_radio.ffpid\n"
            "ORIG=/tmp/ns_mic_orig.txt\n"
            "MOD=ns_mic_radio\n"
            "cs2_out() {\n"
            "\tpactl list source-outputs | awk '\n"
            "\t\t/^Source Output #/ { o=$3; gsub(/[#]/,\"\",o); gsub(/:/,\"\",o) }\n"
            "\t\t/^[[:space:]]*Source: / { s=$2 }\n"
            "\t\t/application.name = \"cs2\"/ || /application.process.binary = \"cs2\"/ { print o, s; exit }'\n"
            "}\n"
            "try_move() {\n"
            "\trec=$(cs2_out)\n"
            "\tout=${rec%% *}\n"
            "\tsrc=${rec##* }\n"
            "\t[ -n \"$out\" ] || return 0\n"
            "\tif [ ! -f \"$ORIG\" ]; then\n"
            "\t\tcase \"$src\" in\n"
            "\t\t$MOD) return 0 ;;\n"
            "\t\t*) echo \"$src\" > \"$ORIG\" ;;\n"
            "\t\tesac\n"
            "\tfi\n"
            "\tpactl move-source-output \"$out\" \"$MOD\" 2>/dev/null\n"
            "}\n"
            "case \"$1\" in\n"
            "on)\n"
            "\t[ -p \"$FF\" ] || mkfifo \"$FF\"\n"
            "\tpactl list short modules | grep -q ns_mic_radio || \\\n"
            "\t\tpactl load-module module-pipe-source source_name=ns_mic_radio file=\"$FF\" format=s16le rate=48000 channels=1\n"
            "\tif [ ! -f \"$PIDF\" ] || ! kill -0 \"$(cat $PIDF)\" 2>/dev/null; then\n"
            "\t\tU=$(curl -s --max-time 15 \"https://opml.radiotime.com/Tune.ashx?id=$2\" | grep -m1 -E \"^https?://\")\n"
            "\t\tif [ -n \"$U\" ]; then\n"
            "\t\t\tnohup ffmpeg -nostdin -loglevel quiet -i \"$U\" -f s16le -ar 48000 -ac 1 \"$FF\" >/dev/null 2>&1 &\n"
            "\t\t\techo $! > \"$PIDF\"\n"
            "\t\tfi\n"
            "\tfi\n"
            "\ttry_move\n"
            "\t;;\n"
            "keepalive)\n"
            "\ttry_move\n"
            "\t;;\n"
            "off)\n"
            "\tif [ -f \"$PIDF\" ]; then kill \"$(cat $PIDF)\" 2>/dev/null; rm -f \"$PIDF\"; fi\n"
            "\trec=$(cs2_out)\n"
            "\tout=${rec%% *}\n"
            "\tsrc=${rec##* }\n"
            "\tif [ -f \"$ORIG\" ] && [ -n \"$out\" ] && [ \"$src\" = \"$MOD\" ]; then\n"
            "\t\tpactl move-source-output \"$out\" \"$(cat $ORIG)\" 2>/dev/null\n"
            "\tfi\n"
            "\trm -f \"$ORIG\"\n"
            "\t;;\n"
            "hardoff)\n"
            "\t$0 off\n"
            "\tm=$(pactl list short modules | awk '/ns_mic_radio/{print $1; exit}')\n"
            "\t[ -n \"$m\" ] && pactl unload-module \"$m\" >/dev/null 2>&1\n"
            "\trm -f \"$FF\"\n"
            "\t;;\n"
            "esac\n";

        const int fd = ::open(micScriptPath, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0)
            return;
        constexpr std::size_t length = sizeof(kScript) - 1;
        std::size_t written = 0;
        while (written < length) {
            const ssize_t chunk = ::write(fd, kScript + written, length - written);
            if (chunk <= 0)
                break;
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
    }

    
    
    
    
    
    
    
    
    void playId(const char* id) const noexcept
    {
        stop();

        NS_DEC(kMarkerPlay, kMarkerEnc);
        writeMetaProbeOnce();
        StringBuilderStorage<512> probeStorage;
        auto probeBuilder = probeStorage.builder();
        probeBuilder.put("exec python3 ", metaScriptPath, ' ', id, ' ', kMarkerPlay.c_str(), ' ', metaFilePath);
        if (metaPid > 0) {
            ::waitpid(metaPid, nullptr, WNOHANG);
            metaPid = 0;
        }
        metaPid = spawnHostShell(probeBuilder.cstring());

        resolveRadioPaths();
        ::unlink(radioSocketPath); 
        const int volume = static_cast<int>(GET_CONFIG_VAR(radio_vars::Volume));
        appliedVolume = volume; 

        StringBuilderStorage<768> storage;
        auto builder = storage.builder();
        builder.put("U=$(curl -s --max-time 15 'https://opml.radiotime.com/Tune.ashx?id=", id,
                    "' | grep -m1 -E '^https?://'); [ -n \"$U\" ] && { rm -f ", radioSocketPath, "; "
                    "if command -v mpv >/dev/null 2>&1; then "
                    "exec mpv --config=no --no-video --really-quiet --no-terminal --input-ipc-server=", radioSocketPath,
                    " --title=", kMarkerPlay.c_str(), " --volume=", volume, " \"$U\"; "
                    "else exec ffplay -nodisp -autoexit -loglevel quiet -window_title ", kMarkerPlay.c_str(),
                    " -volume ", volume, " \"$U\"; fi; }");

        const pid_t pid = spawnHostShell(builder.cstring());
        if (pid > 0)
            currentPid = pid;
    }

    
    
    
    
    static void writeMetaProbeOnce() noexcept
    {
        if (metaScriptWritten)
            return;
        metaScriptWritten = true;
        resolveRadioPaths();

        static constexpr char kScript[] =
            "#!/usr/bin/env python3\n"
            "# Radio now-playing burst probe (written by the game module): resolve the station's\n"
            "# stream URL from TuneIn, connect ONCE with Icy-MetaData, read only the first metadata\n"
            "# block (~2 KB - the title rides the top of the stream), write it out, disconnect.\n"
            "# Repeats once a minute while the player lives: ~130 KB/hour, vs ~56 MB/hour for a\n"
            "# second persistent stream connection. Self-terminates with the player (pgrep of the\n"
            "# playback marker, which this script also carries in argv so stop()'s pkill finds it).\n"
            "import os, re, socket, ssl, subprocess, sys, time\n"
            "station_id, marker, out_path = sys.argv[1], sys.argv[2], sys.argv[3]\n"
            "deadline = time.time() + 4 * 3600\n"
            "current = None\n"
            "def player_alive():\n"
            "    for pat in (\"mpv.*\" + marker, \"ffplay.*\" + marker):\n"
            "        try:\n"
            "            if subprocess.run([\"pgrep\", \"-f\", pat], stdout=subprocess.DEVNULL,\n"
            "                              stderr=subprocess.DEVNULL).returncode == 0:\n"
            "                return True\n"
            "        except OSError:\n"
            "            return True  # cannot check - assume alive; the hard cap still bounds this\n"
            "    return False\n"
            "def fetch_title():\n"
            "    try:\n"
            "        import urllib.request\n"
            "        page = urllib.request.urlopen(\"https://opml.radiotime.com/Tune.ashx?id=\" + station_id,\n"
            "                                      timeout=12).read(4096).decode(\"utf-8\", \"replace\")\n"
            "    except Exception:\n"
            "        return None\n"
            "    m = re.search(r\"https?://\\S+\", page)\n"
            "    if not m:\n"
            "        return None\n"
            "    url = m.group(0)\n"
            "    try:\n"
            "        scheme, _, rest = url.partition(\"://\")\n"
            "        host, _, path = rest.partition(\"/\")\n"
            "        raw = socket.create_connection((host, 443 if scheme == \"https\" else 80), timeout=10)\n"
            "        if scheme == \"https\":\n"
            "            raw = ssl.create_default_context().wrap_socket(raw, server_hostname=host)\n"
            "        raw.sendall((\"GET /%s HTTP/1.1\\r\\nHost: %s\\r\\nIcy-MetaData: 1\\r\\n\"\n"
            "                     \"Connection: close\\r\\nUser-Agent: Mozilla/5.0\\r\\n\\r\\n\" % (path, host)).encode())\n"
            "        buf = b\"\"\n"
            "        while b\"\\r\\n\\r\\n\" not in buf:\n"
            "            chunk = raw.recv(4096)\n"
            "            if not chunk:\n"
            "                return None\n"
            "            buf += chunk\n"
            "        head, buf = buf.split(b\"\\r\\n\\r\\n\", 1)\n"
            "        metaint = 0\n"
            "        for line in head.decode(\"latin1\").split(\"\\r\\n\"):\n"
            "            if line.lower().startswith(\"icy-metaint:\"):\n"
            "                try:\n"
            "                    metaint = int(line.split(\":\", 1)[1].strip())\n"
            "                except ValueError:\n"
            "                    metaint = 0\n"
            "        if not metaint:\n"
            "            return None  # station publishes no in-band titles - stay station-only\n"
            "        for _ in range(96):  # the title is near the top; a handful of blocks is plenty\n"
            "            while len(buf) < metaint + 1:\n"
            "                chunk = raw.recv(65536)\n"
            "                if not chunk:\n"
            "                    return None\n"
            "                buf += chunk\n"
            "            buf = buf[metaint:]\n"
            "            ln = buf[0]\n"
            "            buf = buf[1:]\n"
            "            if ln:\n"
            "                while len(buf) < ln * 16:\n"
            "                    chunk = raw.recv(65536)\n"
            "                    if not chunk:\n"
            "                        return None\n"
            "                    buf += chunk\n"
            "                block, buf = buf[:ln * 16], buf[ln * 16:]\n"
            "                t = re.search(r\"StreamTitle='([^']*)'\", block.decode(\"latin1\"))\n"
            "                if t and t.group(1):\n"
            "                    return t.group(1)\n"
            "        return None\n"
            "    except Exception:\n"
            "        return None\n"
            "def store(title):\n"
            "    global current\n"
            "    if title == current:\n"
            "        return\n"
            "    current = title\n"
            "    try:\n"
            "        tmp = out_path + \".part\"\n"
            "        with open(tmp, \"w\") as f:\n"
            "            f.write(title)\n"
            "        os.replace(tmp, out_path)\n"
            "    except OSError:\n"
            "        pass\n"
            "time.sleep(15)  # playback startup grace (Tune resolve + stream connect)\n"
            "while time.time() < deadline:\n"
            "    if not player_alive():\n"
            "        break\n"
            "    t = fetch_title()\n"
            "    if t:\n"
            "        store(t)\n"
            "    time.sleep(60)\n";

        writeScriptFile(metaScriptPath, kScript, sizeof(kScript) - 1);
    }

    
    
    
    static void writeVolumeScriptOnce() noexcept
    {
        if (volScriptWritten)
            return;
        volScriptWritten = true;
        resolveRadioPaths();

        static constexpr char kScript[] =
            "#!/usr/bin/env python3\n"
            "# Radio live-volume one-shot (written by the game module): push one set_property\n"
            "# into mpv's IPC socket. A failed connect means ffplay fallback or a dead player -\n"
            "# volume then applies at the next play.\n"
            "import json, socket, sys\n"
            "sock_path, volume = sys.argv[1], float(sys.argv[2])\n"
            "try:\n"
            "    s = socket.socket(socket.AF_UNIX)\n"
            "    s.settimeout(2)\n"
            "    s.connect(sock_path)\n"
            "    s.send((json.dumps({\"command\": [\"set_property\", \"volume\", volume]}) + \"\\n\").encode())\n"
            "    s.close()\n"
            "except OSError:\n"
            "    pass\n";

        writeScriptFile(volScriptPath, kScript, sizeof(kScript) - 1);
    }

    static void writeScriptFile(const char* path, const char* data, std::size_t length) noexcept
    {
        const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0)
            return;
        std::size_t written = 0;
        while (written < length) {
            const ssize_t chunk = ::write(fd, data + written, length - written);
            if (chunk <= 0)
                break;
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
    }

    
    
    [[nodiscard]] static bool readFileInto(char* buffer, std::size_t cap, const char* path) noexcept
    {
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0)
            return false;
        const auto readBytes = ::pread(fd, buffer, cap - 1, 0);
        ::close(fd);
        if (readBytes <= 0)
            return false;
        buffer[readBytes] = '\0';
        return true;
    }

    static void clearMpris() noexcept
    {
        mprisPlayerBuf[0] = '\0';
        mprisTitleBuf[0] = '\0';
        mprisArtistBuf[0] = '\0';
        mprisArtworkBuf[0] = '\0';
        mprisPaused = false;
    }

    
    void beginFetch(const char* url) const noexcept
    {
        resolveRadioPaths();
        ::unlink(resultsPath);

        StringBuilderStorage<1024> storage;
        auto builder = storage.builder();
        builder.put("curl -s --max-time 20 '", url, "' -o ", resultsPartPath, " && mv -f ", resultsPartPath, ' ', resultsPath);

        
        if (fetchPid > 0) {
            ::waitpid(fetchPid, nullptr, WNOHANG);
            fetchPid = 0;
        }
        fetchPid = spawnHostShell(builder.cstring());
        fetchPending = true;
    }

    [[nodiscard]] static pid_t spawnHostShell(const char* script) noexcept
    {
        NS_DEC(kLaunchClientPath, kLaunchClientPathEnc);
        char* const argv[] = {
            const_cast<char*>("steam-runtime-launch-client"),
            const_cast<char*>("--host"),
            const_cast<char*>("--"),
            const_cast<char*>("sh"),
            const_cast<char*>("-c"),
            const_cast<char*>(script),
            nullptr,
        };
        pid_t pid{};
        if (::posix_spawn(&pid, kLaunchClientPath, nullptr, nullptr, argv, environ) == 0)
            return pid;
        // Native launches do not always include the Steam pressure-vessel helper.
        if (::access(kLaunchClientPath, X_OK) != 0) {
            char* const nativeArgv[] = {const_cast<char*>("sh"), const_cast<char*>("-c"), const_cast<char*>(script), nullptr};
            if (::posix_spawnp(&pid, "sh", nullptr, nullptr, nativeArgv, environ) == 0)
                return pid;
        }
        return 0;
    }

    static void updateArtwork() noexcept
    {
        if (artworkPid > 0) {
            int status{};
            const auto reaped = ::waitpid(artworkPid, &status, WNOHANG);
            if (reaped == artworkPid) {
                artworkPid = 0;
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                    copyText(readyArtworkUrl, requestedArtworkUrl, sizeof(readyArtworkUrl));
            } else if (reaped < 0 && errno == ECHILD) {
                artworkPid = 0;
            }
        }
        if (artworkPid > 0 || !mprisArtworkBuf[0] || std::strcmp(mprisArtworkBuf, requestedArtworkUrl) == 0)
            return;
        readyArtworkUrl[0] = '\0';
        copyText(requestedArtworkUrl, mprisArtworkBuf, sizeof(requestedArtworkUrl));
        StringBuilderStorage<2048> storage;
        auto builder = storage.builder();
        builder.put("curl -fsSL --max-time 3 --max-filesize 2097152 --proto '=http,https,file' --proto-redir '=http,https' --url ");
        appendShellPath(builder, requestedArtworkUrl);
        builder.put(" -o ");
        appendShellPath(builder, artworkPartPath);
        builder.put(" && mv -f ");
        appendShellPath(builder, artworkPartPath);
        builder.put(' ');
        appendShellPath(builder, artworkFilePath);
        artworkPid = spawnHostShell(builder.cstring());
    }

    static void appendShellPath(auto& builder, const char* path) noexcept
    {
        builder.put('\'');
        for (; *path; ++path) {
            if (*path == '\'')
                builder.put("'\\''");
            else
                builder.put(*path);
        }
        builder.put('\'');
    }

    static void copyId(char* dst, const char* src) noexcept
    {
        int i = 0;
        for (; src[i] != '\0' && i < static_cast<int>(sizeof(RadioStation::id)) - 1; ++i)
            dst[i] = src[i];
        dst[i] = '\0';
    }

    static void copyText(char* dst, const char* src, int cap) noexcept
    {
        int i = 0;
        for (; src[i] != '\0' && i < cap - 1; ++i)
            dst[i] = src[i];
        dst[i] = '\0';
    }

    

    struct SavedStation {
        char id[sizeof(RadioStation::id)]{};
        char name[sizeof(RadioStation::text)]{};
    };

    [[nodiscard]] int favoriteIndexFor(const char* id) const noexcept
    {
        for (int i = 0; i < favoriteStationCount; ++i) {
            if (std::strcmp(favoriteStations[i].id, id) == 0)
                return i;
        }
        return -1;
    }

    [[nodiscard]] bool favoritesFilePath(char (&path)[512]) const noexcept
    {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (!directoryPath)
            return false;
        const auto* dir = reinterpret_cast<const char*>(directoryPath.get());
        std::size_t length = 0;
        while (dir[length] != '\0' && length + 1 < sizeof(path) - sizeof("/radio_favorites.txt"))
            ++length;
        std::memcpy(path, dir, length);
        std::memcpy(path + length, "/radio_favorites.txt", sizeof("/radio_favorites.txt"));
        return true;
    }

    void loadFavoritesOnce() const noexcept
    {
        if (favoriteStationsLoaded)
            return;
        favoriteStationsLoaded = true;

        char path[512];
        if (!favoritesFilePath(path))
            return;
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0)
            return; 

        char fileBuffer[4096];
        const auto readBytes = ::pread(fd, fileBuffer, sizeof(fileBuffer) - 1, 0);
        ::close(fd);
        if (readBytes <= 0)
            return;
        fileBuffer[readBytes] = '\0';

        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(readBytes) && favoriteStationCount < kMaxFavorites) {
            std::size_t lineLength = 0;
            while (fileBuffer[offset + lineLength] != '\0' && fileBuffer[offset + lineLength] != '\n')
                ++lineLength;
            const auto next = offset + lineLength + 1;

            
            if (lineLength > 0 && fileBuffer[offset] != '#') {
                std::size_t idLength = 0;
                while (idLength < lineLength && fileBuffer[offset + idLength] != ' ')
                    ++idLength;
                std::size_t nameStart = idLength;
                while (nameStart < lineLength && fileBuffer[offset + nameStart] == ' ')
                    ++nameStart;
                if (idLength > 0 && idLength < sizeof(SavedStation::id) && nameStart < lineLength) {
                    auto& slot = favoriteStations[favoriteStationCount];
                    std::memcpy(slot.id, fileBuffer + offset, idLength);
                    slot.id[idLength] = '\0';
                    copyText(slot.name, fileBuffer + offset + nameStart, static_cast<int>(sizeof(SavedStation::name)));
                    ++favoriteStationCount;
                }
            }
            offset = next;
        }
    }

    void saveFavorites() const noexcept
    {
        char path[512];
        if (!favoritesFilePath(path))
            return;
        StringBuilderStorage<64 * 128> storage;
        auto builder = storage.builder();
        for (int i = 0; i < favoriteStationCount; ++i)
            builder.put(favoriteStations[i].id, ' ', favoriteStations[i].name, '\n');

        const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd < 0)
            return;
        const char* data = builder.cstring();
        std::size_t remaining = std::strlen(data);
        std::size_t written = 0;
        while (written < remaining) {
            const auto chunk = ::write(fd, data + written, remaining - written);
            if (chunk <= 0)
                break;
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
    }

    
    void pushRecent(const char* id, const char* name) const noexcept
    {
        int existing = -1;
        for (int i = 0; i < recentStationCount; ++i) {
            if (std::strcmp(recentStations[i].id, id) == 0) {
                existing = i;
                break;
            }
        }
        if (existing >= 0) {
            for (int i = existing; i > 0; --i)
                recentStations[i] = recentStations[i - 1];
        } else if (recentStationCount < kMaxRecent) {
            for (int i = recentStationCount; i > 0; --i)
                recentStations[i] = recentStations[i - 1];
            ++recentStationCount;
        } else {
            for (int i = kMaxRecent - 1; i > 0; --i)
                recentStations[i] = recentStations[i - 1];
        }
        copyId(recentStations[0].id, id);
        copyText(recentStations[0].name, name, sizeof(SavedStation::name));
    }

    
    static constexpr ns_str::Encrypted<sizeof("/usr/bin/steam-runtime-launch-client")> kLaunchClientPathEnc{"/usr/bin/steam-runtime-launch-client"};
    static constexpr ns_str::Encrypted<sizeof("osiris-radio")> kMarkerEnc{"osiris-radio"};

    
    
    
    
    inline static char resultsPath[192];
    inline static char resultsPartPath[192];
    inline static char micScriptPath[192];
    inline static char metaScriptPath[192];
    inline static char metaFilePath[192];
    inline static char volScriptPath[192];
    inline static char artworkFilePath[192], artworkPartPath[192];
    inline static pid_t artworkPid{};
    inline static char requestedArtworkUrl[512]{}, readyArtworkUrl[512]{};
    inline static char mprisArtworkBuf[512]{};
    inline static char mprisFilePath[192];
    inline static char mprisPartPath[192];
    inline static char radioSocketPath[192];
    inline static bool radioPathsResolved = false;

    static void resolveRadioPaths() noexcept
    {
        if (radioPathsResolved)
            return;
        radioPathsResolved = true;
        static_cast<void>(ns_paths::join(resultsPath, sizeof(resultsPath), "osiris-radio-results.json"));
        static_cast<void>(ns_paths::join(resultsPartPath, sizeof(resultsPartPath), "osiris-radio-results.json.part"));
        static_cast<void>(ns_paths::join(micScriptPath, sizeof(micScriptPath), "ns_mic_radio.sh"));
        static_cast<void>(ns_paths::join(metaScriptPath, sizeof(metaScriptPath), "ns_radio_meta.py"));
        static_cast<void>(ns_paths::join(metaFilePath, sizeof(metaFilePath), "osiris-radio-meta.txt"));
        static_cast<void>(ns_paths::join(volScriptPath, sizeof(volScriptPath), "ns_radio_vol.py"));
        static_cast<void>(ns_paths::join(artworkFilePath, sizeof(artworkFilePath), "osiris-mpris-art"));
        static_cast<void>(ns_paths::join(artworkPartPath, sizeof(artworkPartPath), "osiris-mpris-art.part"));
        static_cast<void>(ns_paths::join(mprisFilePath, sizeof(mprisFilePath), "osiris-mpris.txt"));
        static_cast<void>(ns_paths::join(mprisPartPath, sizeof(mprisPartPath), "osiris-mpris.txt.part"));
        static_cast<void>(ns_paths::join(radioSocketPath, sizeof(radioSocketPath), "osiris-radio.sock"));
    }

    
    inline static pid_t currentPid{0};
    inline static pid_t fetchPid{0};
    inline static bool fetchPending{false};
    inline static bool resultsDirty{false};
    inline static int resultCount{0};
    inline static RadioStation results[kMaxResults]{};
    inline static char resultsHeader[128]{};
    inline static char lastPlayedId[sizeof(RadioStation::id)]{};
    inline static char lastPlayedNameBuf[sizeof(RadioStation::text)]{};
    
    
    inline static pid_t metaPid{0};
    inline static pid_t mprisPid{0};
    inline static pid_t volPid{0};
    inline static char nowPlayingTrackBuf[128]{};
    inline static char mprisPlayerBuf[48]{};
    inline static char mprisTitleBuf[128]{};
    inline static char mprisArtistBuf[128]{};
    inline static bool mprisPaused{false};
    inline static double nextNowPlayingPoll{0.0};
    inline static int volumeApplyCounter{0};
    inline static int appliedVolume{-1};
    inline static bool metaScriptWritten{false};
    inline static bool volScriptWritten{false};
    inline static bool micBroadcastActive{false};
    inline static char micBroadcastStation[sizeof(RadioStation::id)]{};
    inline static bool broadcastScriptWritten{false};
    inline static bool voiceKeySynthed{false};
    inline static int synthedKey{0};
    inline static bool autoVoiceEngaged{false};
    inline static bool autoVoiceHinted{false};
        
    
    inline static std::atomic<int> pendingBindAction{0};
    inline static std::atomic<bool> pendingModenable{false};
    inline static bool micAlwaysSampled{false};
        inline static bool mouseKeyHinted{false};
    inline static int voiceKeyReassertCounter{0};
    inline static int capturePeakZeroStreak{0};
    inline static bool capturePeakAnomalyLogged{false};
    inline static bool airhornRouted{false};
    inline static int airhornStatusCheckFrames{0};
    inline static int airhornFramesLeft{0};
    inline static int airhornCooldownFrames{0};
    inline static int airhornKeepaliveCounter{0};
    inline static bool airhornHinted{false};
    inline static char fetchBuffer[192 * 1024]{};
    inline static SavedStation favoriteStations[kMaxFavorites]{};
    inline static int favoriteStationCount{0};
    inline static bool favoriteStationsLoaded{false};
    inline static SavedStation recentStations[kMaxRecent]{};
    inline static int recentStationCount{0};

    HookContext& hookContext;
};
