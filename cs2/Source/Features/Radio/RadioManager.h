#pragma once

#include <atomic>
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


// Web radio backed by the RadioTime / TuneIn OPML API - the same endpoints the reference implementation
// uses (Browse.ashx?c=local for the region's local stations, Search.ashx for queries, Tune.ashx to turn
// a station id into a playable stream URL). We don't decode audio ourselves: ffplay does, ON THE HOST,
// because CS2 runs inside the Steam Linux Runtime container (pressure-vessel) where ffplay does not exist
// and only /usr/bin:/bin are on PATH. Everything therefore runs through `steam-runtime-launch-client
// --host`, which executes on the host (host PATH, host libraries, host network, host audio proxied back
// in via PULSE_SERVER). curl also runs host-side. The results JSON lives in the writable
// exchange root (Utils/NsPaths.h - $HOME/OsirisCS2): since the 2026-10-04 Steam client update
// /tmp is no longer shared between the container and the host, so that is where host-side
// curl writes and the container-side game reads.
//
// Fetches are asynchronous and non-blocking: startBrowseLocal()/startSearch() fire a detached host
// `curl ... -o file.part && mv file.part file` (the rename makes the finished file appear atomically),
// and poll() - driven each GUI frame by RadioTab - reads and parses it once it lands. Playback is
// resolved entirely host-side in one shell command (curl Tune.ashx | grep first stream URL | exec
// ffplay), so there is no second round trip through the game.
extern "C" char** environ;

template <typename HookContext>
class RadioManager {
public:
    explicit RadioManager(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    static constexpr int kMaxResults = 40;

    // --- station list fetching (async) ---

    void startBrowseLocal() const noexcept
    {
        beginFetch("https://opml.radiotime.com/Browse.ashx?c=local&render=json&formats=mp3,aac");
    }

    // `query` is already URL-safe (the Panorama side percent-encodes it into a single token, so it never
    // contains spaces or quotes). An empty query falls back to the local browse.
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

    // Reads and parses the fetched station list if it has landed. Cheap when idle (a single failing
    // open() while the fetch is still in flight). Sets the dirty flag so the UI repaints the rows.
    void poll() const noexcept
    {
        bool fetchProcessDone = false;
        if (fetchPid > 0) {
            int status;
            if (::waitpid(fetchPid, &status, WNOHANG) == fetchPid) {
                fetchPid = 0;
                fetchProcessDone = true; // the shell (curl && mv) has finished; the file is final now
            }
        }

        if (!fetchPending)
            return;

        resolveRadioPaths();
        const int fd = ::open(resultsPath, O_RDONLY);
        if (fd < 0) {
            // Process exited and no result file landed: the fetch failed (offline, timeout) -
            // clear the pending flag so the UI's loading indicator does not stick forever.
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

    // True while a station-list fetch is in flight (UI shows a loading state).
    [[nodiscard]] bool fetching() const noexcept { return fetchPending; }

    // Id of the station passed to the most recent play (UI highlights the matching row).
    [[nodiscard]] const char* lastPlayed() const noexcept { return lastPlayedId; }

    // Display name of the station passed to the most recent play (for the now-playing row);
    // survives across result list changes.
    [[nodiscard]] const char* lastPlayedName() const noexcept { return lastPlayedNameBuf; }

    // Returns true (once) after new results have been parsed, so the UI only repaints when something
    // actually changed.
    [[nodiscard]] bool consumeDirty() const noexcept
    {
        if (!resultsDirty)
            return false;
        resultsDirty = false;
        return true;
    }

    // --- playback ---

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

    // Stops any current stream. Kills the host ffplay by its marker (the container-side launch-client
    // cannot reach it), then reaps our tracked launch-client. The pkill runs synchronously so a
    // following play cannot race it.
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
    }

    // --- favorites + recents ---

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

    // Adds/removes a station from the persisted favorites list and rewrites the file.
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
            return; // list full - nothing changes (and no pointless rewrite)
        }
        saveFavorites();
    }

    // Plays a persisted favorite directly (Tune.ashx resolves by id - no result list needed).
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

    // Same as playFavorite but for the session "recently played" ring.
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

    // --- mic broadcast (radio -> voice chat) ---
    //
    // While the menu toggle is on AND a station is playing, the game's microphone capture is
    // routed to a virtual source: the switch script (written once to ns_mic_radio.sh in the
    // exchange root,
    // executed ON THE HOST via spawnHostShell) creates a module-pipe-source, feeds it with a
    // second ffmpeg streaming the same station at s16le/48k mono, and `pactl
    // move-source-output`s the cs2 capture stream (matched by application.name = "cs2") to it.
    // When the radio stops or the toggle goes off, the capture is moved back to the remembered
    // original source and the user can talk normally. Nothing in the game is touched - this is
    // purely at the audio-server level, so it works regardless of which layer captures the mic.
    //
    // On top of the routing, the voice key is held for the whole broadcast: `+voicerecord` is
    // queued through the console when the broadcast engages and `-voicerecord` when it
    // disengages. The plus command latches the button down exactly like a physical key press,
    // so the routed radio audio is transmitted CONTINUOUSLY without the user holding anything
    // (CS2 only sends voice while the key is down). If the user is dead or between matches the
    // press just does nothing and broadcasting resumes automatically once it would work again.
    //
    // Called every frame from the present thread (renderGameOverlay) so the routing follows
    // play/stop transitions even while the user is on another menu tab.
    [[nodiscard]] bool isPlaying() const noexcept
    {
        if (currentPid <= 0)
            return false;
        int status;
        if (::waitpid(currentPid, &status, WNOHANG) == currentPid) {
            currentPid = 0; // ffplay exited (stream ended / -autoexit)
            return false;
        }
        return true;
    }

    void updateMicBroadcast() const noexcept
    {
        // keep OUR crash handlers installed: the game/breakpad re-installs its own lazily per
        // subsystem and would silently steal SIGABRT from us (the 2026-09-13 silent deaths).
        CrashLogger::reassert();
        writeBroadcastScriptOnce();
        const bool want = isPlaying() && GET_CONFIG_VAR(radio_vars::MicBroadcast);
        const bool stationChanged = micBroadcastActive && std::strcmp(micBroadcastStation, lastPlayedId) != 0;
        if (want == micBroadcastActive && !stationChanged) {
            // While broadcasting: re-assert the synthetic press (window focus changes and other
            // transitions clear SDL's key state), re-apply the capture-stream move (the stream
            // only exists while voice is engaged, so the first move may have had nothing to
            // move - and re-created streams land back on the default mic), and probe the
            // capture level. Same ~2s cadence for all three.
            if (micBroadcastActive && ++voiceKeyReassertCounter >= 128) {
                voiceKeyReassertCounter = 0;
                synthVoiceKey(true, true);
                resolveRadioPaths();
                char keepaliveCommand[288];
                if (std::snprintf(keepaliveCommand, sizeof(keepaliveCommand), "exec sh %s keepalive", micScriptPath) > 0)
                    static_cast<void>(spawnHostShell(keepaliveCommand));
                // Anomaly-only probe (gui log contract: silence = healthy): if the game's voice
                // capture reports no signal level for ~6s straight while the FIFO is being fed,
                // the routing failed - say so once instead of failing silently.
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


        // FrameStageNotify-6 consumer: the ONLY place this feature touches the engine console.
    // Called from the FSN hook (game thread, ticks in menus too). Drains the bind/unbind and
    // voice-enable requests queued by the present-thread updates. Single-slot last-wins is
    // correct: all three operations are idempotent.
    void runFrameStageNotify() const noexcept
    {
        if (pendingModenable.exchange(false, std::memory_order_relaxed)) {
            auto&& executor = hookContext.template make<EngineCommandExecutor>();
            executor.execute("voice_modenable 1");
            // THE SOUNDBOARD'S MIC-OPEN LEVER (the 2026-09-13 "no capture stream at all"
            // route report): CS2 only opens the mic capture stream while voice is engaged -
            // and if the user's voice chat is off/unengaged, nothing we synth creates a
            // capture for the virtual-source move to redirect. voice_always_sample_mic
            // forces the engine to sample the mic PERMANENTLY (capture stream always exists),
            // while +voicerecord keeps gating transmission (which we synth per-clip). One-way
            // per session: sampling is not transmitting, and the airhorn/radio depend on the
            // stream existing. (If the cvar were ever renamed the exec just warns in console
            // - the same harmless unknown-command shape as the playerchatwheel precedent.)
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
    // --- synthetic push-to-talk -------------------------------------------------------------
    //
    // The transmission story, after two failed experiments:
    //   1. `+voicerecord` latching - does NOT transmit: the push-to-talk gate polls the PHYSICAL
    //      bound key, a queued +command only lights the local indicator.
    //   2. `voice_vox` + threshold clamping - the gate kept closing (icon dropped on BOTH
    //      screens, i.e. CS2 stopped sending entirely).
    //   3. Queued SDL key events (SDL_PeepEvents ADDEVENT) - pushed events are DATA in the queue;
    //      they do NOT update SDL's keyboard-state array, so a state-polling gate never sees them.
    //      NEVER re-add: they also crashed inside libclient's gameui/bind path (null deref at
    //      libclient+0x1a8ad05, input-system frames on the stack).
    // (A `voice_device_override` redesign was considered; the live routing stays
    // move-source-output - it delivers, only the VOX gate was broken.)
    // What remains is the only lever that ever worked:
    //   SDL's keyboard-state array: SDL_GetKeyboardState returns SDL's OWN persistent per-
    //   scancode bytes - the same array the game's binds read (our Bind::isDown reads it too).
    //   Writing array[scancode] is exactly what SDL does for a real press: instant, no queue
    //   round-trip, no game-thread event processing.
    //
    // hands-free auto-key: the lever above only opens the gate for the scancode the ENGINE has
    // bound to +voicerecord, and the Voice Key row defaults to Off (silent no-transmit) while
    // mouse binds have no writable state array at all. So when no usable keyboard bind is
    // configured, the cheat binds a spare key itself (`bind f9 +voicerecord`, once per need)
    // and synths THAT - one Broadcast checkbox transmits with zero setup, mouse-voice users
    // included, and nobody's own binds are touched. The bind is released (`unbind f9`) as soon
    // as neither broadcast nor airhorn needs it; a crash mid-broadcast can leave it behind
    // (harmless spare PTT key - `unbind f9` in console removes it).
    static constexpr int kAutoVoiceScancode = 66; // F9, USB HID 0x42 - stable across SDL2/SDL3
    static constexpr const char* kAutoVoiceKeyName = "f9";

    void ensureAutoVoiceBind() const noexcept
    {
        if (autoVoiceEngaged)
            return;
        autoVoiceEngaged = true;
        // Queued for the frame thread (runFrameStageNotify), NOT executed here: the engine
        // command buffer is not thread-safe and this runs on the present thread. Draining on
        // FrameStageNotify-6 is ChatTools-proven (their say/name/color commands run there every
        // session, in menus too) and costs ~a frame.
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
            // Release the array slot directly: synthVoiceKey(false) would re-bind first.
            const LinuxDynamicLibrary sdl{cs2::SDL_DLL};
            if (const auto getState = sdl.getFunctionAddress("SDL_GetKeyboardState").as<sdl3::SDL_GetKeyboardState*>())
                const_cast<std::uint8_t*>(getState(nullptr))[kAutoVoiceScancode] = 0;
            synthedKey = 0;
            voiceKeySynthed = false;
        }
        pendingBindAction.store(2, std::memory_order_relaxed); // drained on the frame thread
    }

    void synthVoiceKey(bool down, bool force = false) const noexcept
    {
        const int configured = GET_CONFIG_VAR(radio_vars::VoiceKeyBind);
        int key = 0;
        if (configured > Bind::kOff && configured <= Bind::kMaxScancode) {
            key = configured; // manual override: the user's own keyboard voice key
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
                state[synthedKey] = 0; // key changed under us - release the stale slot
            state[key] = down ? 1 : 0;
            synthedKey = down ? key : 0;
        }
        voiceKeySynthed = down;
    }

    // Transmission arming: voice enabled + the capture stream routed to the virtual source
    // (the caller's script "on"/keepalive does the move) + synthetic key held. The console
    // command is queued for the frame thread (see runFrameStageNotify) - never executed here.
    void armTransmission() const noexcept
    {
        pendingModenable.store(true, std::memory_order_relaxed);
        synthVoiceKey(true);
    }

    void disarmTransmission() const noexcept
    {
        synthVoiceKey(false);
    }


    // The switch script must live on disk (too long for the spawnHostShell command buffer and
    // easier to keep idempotent as a standalone file). Written once per process into the
    // exchange root - the module writes it inside the container, the host-side sh executes it.
    // The script's own scratch files (FF/PIDF/ORIG) stay in /tmp: they are only touched
    // host-side, so they never cross the container boundary.
    static void writeBroadcastScriptOnce() noexcept
    {
        if (broadcastScriptWritten)
            return;
        broadcastScriptWritten = true;
        resolveRadioPaths();

        // $1 = on|off|hardoff, $2 = station id (on only). Ids are alphanumeric (TuneIn), so the
        // interpolation into the curl URL is safe.
        //
        // The move-source-output is back (round-1 routing DID deliver audio; only the VOX gate
        // was broken, and the keymap press now holds the gate). The capture stream may only
        // exist while voice is engaged, so "keepalive" re-applies the move every ~2s from the
        // game while broadcasting - also catching re-created streams landing on the default mic.
        // Matcher matches BOTH application.name and application.process.binary = "cs2" (the
        // container may set either).
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

    // Resolves a station id to a stream URL and plays it, entirely host-side: curl the Tune.ashx playlist,
    // take the first http(s) line, and exec ffplay on it. The id comes from TuneIn and is alphanumeric
    // (e.g. "s307738"), so interpolating it into the shell command is safe.
    void playId(const char* id) const noexcept
    {
        stop();

        StringBuilderStorage<512> storage;
        auto builder = storage.builder();
        NS_DEC(kMarkerPlay, kMarkerEnc);
        builder.put("U=$(curl -s --max-time 15 'https://opml.radiotime.com/Tune.ashx?id=", id,
                    "' | grep -m1 -E '^https?://'); [ -n \"$U\" ] && exec ffplay -nodisp -autoexit -loglevel quiet -window_title ",
                    kMarkerPlay.c_str(), " -volume ", static_cast<int>(GET_CONFIG_VAR(radio_vars::Volume)), " \"$U\"");

        const pid_t pid = spawnHostShell(builder.cstring());
        if (pid > 0)
            currentPid = pid;
    }

    // Deletes any stale result file, then fires a detached host curl that writes the new one atomically.
    void beginFetch(const char* url) const noexcept
    {
        resolveRadioPaths();
        ::unlink(resultsPath);

        StringBuilderStorage<1024> storage;
        auto builder = storage.builder();
        builder.put("curl -s --max-time 20 '", url, "' -o ", resultsPartPath, " && mv -f ", resultsPartPath, ' ', resultsPath);

        // Reap any previous fetch shell before we lose its pid.
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
        return 0;
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

    // --- favorites storage (<configDir>/radio_favorites.txt, "<id> <name>" per line) ---

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
            return; // no file yet - empty list is the correct state

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

            // "<id> <name>"; '#' comments and blank lines are skipped
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

    // Session-scoped "recently played" ring, most recent first, deduplicated by id.
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

    // Identity-bearing literals are kept encrypted (Utils/NsStr.h); decrypt to the stack at use.
    static constexpr ns_str::Encrypted<sizeof("/usr/bin/steam-runtime-launch-client")> kLaunchClientPathEnc{"/usr/bin/steam-runtime-launch-client"};
    static constexpr ns_str::Encrypted<sizeof("osiris-radio")> kMarkerEnc{"osiris-radio"};

    // The radio results JSON is written by a HOST-side curl (spawnHostShell) and read back from
    // inside the container, so it lives in the writable exchange root (Utils/NsPaths.h) - since
    // the 2026-10-04 Steam client update the two sides no longer share /tmp. Resolved once.
    inline static char resultsPath[192];
    inline static char resultsPartPath[192];
    inline static char micScriptPath[192];
    inline static bool radioPathsResolved = false;

    static void resolveRadioPaths() noexcept
    {
        if (radioPathsResolved)
            return;
        radioPathsResolved = true;
        static_cast<void>(ns_paths::join(resultsPath, sizeof(resultsPath), "osiris-radio-results.json"));
        static_cast<void>(ns_paths::join(resultsPartPath, sizeof(resultsPartPath), "osiris-radio-results.json.part"));
        static_cast<void>(ns_paths::join(micScriptPath, sizeof(micScriptPath), "ns_mic_radio.sh"));
    }

    // Feature objects are rebuilt per command, so all cross-call state is static.
    inline static pid_t currentPid{0};
    inline static pid_t fetchPid{0};
    inline static bool fetchPending{false};
    inline static bool resultsDirty{false};
    inline static int resultCount{0};
    inline static RadioStation results[kMaxResults]{};
    inline static char resultsHeader[128]{};
    inline static char lastPlayedId[sizeof(RadioStation::id)]{};
    inline static char lastPlayedNameBuf[sizeof(RadioStation::text)]{};
    inline static bool micBroadcastActive{false};
    inline static char micBroadcastStation[sizeof(RadioStation::id)]{};
    inline static bool broadcastScriptWritten{false};
    inline static bool voiceKeySynthed{false};
    inline static int synthedKey{0};
    inline static bool autoVoiceEngaged{false};
    inline static bool autoVoiceHinted{false};
        // Present-thread -> frame-thread console handoff (engine command buffer is not
    // thread-safe): 0 = none, 1 = `bind f9 +voicerecord`, 2 = `unbind f9`.
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
