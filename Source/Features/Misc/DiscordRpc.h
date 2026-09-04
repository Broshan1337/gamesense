#pragma once

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include <Features/Misc/DiscordRpcConfigVariables.h>
#include <Features/Radio/RadioManager.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/StringBuilder.h>

// posix_spawn environment (unistd.h only declares it under feature macros - mirror RadioManager;
// must stay at global scope, a namespace-scope declaration would create a namespaced environ).
extern "C" char** environ;

// Discord Rich Presence (Misc tab). See DiscordRpcConfigVariables.h for the toggle.
//
// Architecture (mirrors the web radio's host-side IO):
//   * THIS feature (game process, present thread) gathers match data - team sizes, alive/dead
//     per side and the local player's team damage from the shared player-list snapshot, match
//     kind from the game_type/game_mode cvars - and renders the two user-editable template lines
//     into /tmp/ns_discord_rpc.json (atomic rename, content-gated).
//   * A HOST-SIDE python relay (/tmp/ns_discord_rpc.py, spawned once through
//     steam-runtime-launch-client) owns the actual connection to the Discord desktop client's
//     local IPC socket: it polls the JSON file, reconnects when Discord drops the link, manages
//     the "elapsed" timestamp itself (the game cannot produce a unix epoch) and clears the
//     presence on disable/unload. The game runs inside the Steam container where the Discord
//     socket is not guaranteed to be mounted - /tmp is shared with the host, which is exactly
//     how the radio and the mic broadcast already work.
//   * Discord local RPC wire format (legacy discord-rpc, still what the client serves): 8-byte
//     frame header (LE uint32 opcode, uint32 length) + JSON payload; opcodes 0=Frame, 1=Close,
//     2=Handshake, 3=Ping. Handshake {"v":1,"client_id":...}, then SET_ACTIVITY frames.
//
// Template placeholders (both lines): {mode} {t} {ct} {alive} {dead} {talive} {tdead}
// {ctalive} {ctdead} {tdmg} {myteam}. Unknown {names} are passed through literally.
// The two lines persist in <configDir>/discord_rpc.txt (sidecar pattern like killsay).
template <typename HookContext>
class DiscordRpc {
public:
    explicit DiscordRpc(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onUnload() noexcept
    {
        if (relaySpawned) {
            killRelay();
            relaySpawned = false;
            writeStateFile(nullptr, nullptr); // relay clears the presence before dying (or socket death does)
        }
    }

    // Present thread, every frame from renderGameOverlay. Cheap gates inside; file writes are
    // throttled to 1Hz and content-gated, so a quiet match costs one strcmp per frame.
    void update() noexcept
    {
        const bool enabled = GET_CONFIG_VAR(discord_rpc_vars::Enabled);
        if (!enabled) {
            if (relaySpawned) {
                killRelay();
                relaySpawned = false;
                writeStateFile(nullptr, nullptr);
            }
            return;
        }

        if (!relaySpawned) {
            writeRelayScriptOnce();
            relaySpawned = true; // the relay manages its own "elapsed" timestamp
        }

        if (ImGui::GetTime() - lastUpdate < 1.0f)
            return;
        lastUpdate = ImGui::GetTime();

        // Reap a dead relay (Discord host rebooted, python crashed) and respawn it.
        if (relayPid > 0) {
            int status;
            if (::waitpid(relayPid, &status, WNOHANG) == relayPid)
                relayPid = 0;
        }
        if (relayPid <= 0)
            spawnRelay();

        auto&& radio = hookContext.template make<RadioManager>();
        MatchState match = gatherMatchState();
        writeStateFile(&match, radio.isPlaying() ? radio.lastPlayedName() : nullptr);
    }

    // --- menu editor support ------------------------------------------------------------

    // The Misc rows edit these in place (loaded from the sidecar on first access) and write the
    // file back through saveTemplates() when an edit deactivates.
    [[nodiscard]] char* detailsBuffer() noexcept
    {
        loadTemplatesOnce();
        return detailsTemplate;
    }

    [[nodiscard]] char* stateBuffer() noexcept
    {
        loadTemplatesOnce();
        return stateTemplate;
    }

    void saveTemplates() noexcept
    {
        loadTemplatesOnce();
        char path[512];
        if (!templatesFilePath(path))
            return;
        const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd < 0)
            return;
        char line[224];
        for (const char* text : {static_cast<const char*>(detailsTemplate), static_cast<const char*>(stateTemplate)}) {
            const int length = std::snprintf(line, sizeof(line), "%s\n", text);
            if (length <= 0)
                continue;
            std::size_t written = 0;
            while (written < static_cast<std::size_t>(length)) {
                const auto chunk = ::write(fd, line + written, static_cast<std::size_t>(length) - written);
                if (chunk <= 0)
                    break;
                written += static_cast<std::size_t>(chunk);
            }
        }
        ::close(fd);
    }

private:
    static constexpr const char* kStatePath = "/tmp/ns_discord_rpc.json";
    static constexpr const char* kStatePartPath = "/tmp/ns_discord_rpc.json.part";
    static constexpr const char* kRelayScriptPath = "/tmp/ns_discord_rpc.py";
    static constexpr const char* kLaunchClientPath = "/usr/bin/steam-runtime-launch-client";
    static constexpr const char* kLargeImageKey = "ns2";
    static constexpr const char* kLargeImageText = "Neversneeze";
    static constexpr const char* kSmallImageKey = "cs2";
    static constexpr const char* kSmallImageText = "nonprime.club";
    static constexpr const char* kDefaultDetails = "{mode} | {t}v{ct} | alive {alive} | dead {dead} | team dmg {tdmg}";
    static constexpr const char* kDefaultState = "Tapping Windows NNs";

    struct MatchState {
        int tCount = 0;
        int ctCount = 0;
        int tAlive = 0;
        int ctAlive = 0;
        int tDead = 0;
        int ctDead = 0;
        int teamDamage = 0;
        bool inMatch = false;
        const char* mode = "Match";
    };

    // game_type/game_mode table (client-side mirrored cvars; read at 1Hz, never written).
    // 0/0 Casual, 0/1 Competitive, 0/2 Wingman, 0/3 Premier, 1/0 Arms Race, 1/2 Deathmatch -
    // anything else falls back to "Match".
    [[nodiscard]] const char* modeName(int gameType, int gameMode) const noexcept
    {
        if (gameType == 0) {
            if (gameMode == 0)
                return "Casual";
            if (gameMode == 1)
                return "Competitive";
            if (gameMode == 2)
                return "Wingman";
            if (gameMode == 3)
                return "Premier";
        } else if (gameType == 1) {
            if (gameMode == 0)
                return "Arms Race";
            if (gameMode == 2)
                return "Deathmatch";
        }
        return "Match";
    }

    [[nodiscard]] MatchState gatherMatchState() const noexcept
    {
        MatchState match;
        const auto snap = player_list::snapshot();
        for (int i = 0; i < snap.count; ++i) {
            const auto& row = snap.rows[i];
            if (row.team == 3) {
                ++match.ctCount;
                if (row.alive)
                    ++match.ctAlive;
                else
                    ++match.ctDead;
            } else if (row.team == 2) {
                ++match.tCount;
                if (row.alive)
                    ++match.tAlive;
                else
                    ++match.tDead;
            }
            if (row.isLocalPlayer) {
                match.inMatch = true;
                match.teamDamage = row.teamDamage;
                const auto gameType = hookContext.cvarSystem().readIntConVar("game_type");
                const auto gameMode = hookContext.cvarSystem().readIntConVar("game_mode");
                match.mode = modeName(gameType.value_or(0), gameMode.value_or(0));
            }
        }
        return match;
    }

    // Renders a template line. Placeholders are brace-delimited names, so {t} and {talive} can
    // never be confused; an unknown {name} is emitted literally.
    static void renderTemplate(char* out, std::size_t cap, const char* input, const MatchState& match) noexcept
    {
        std::size_t o = 0;
        const auto put = [&](const char* text) {
            for (; *text != '\0' && o + 1 < cap; ++text)
                out[o++] = *text;
        };
        const auto putInt = [&](int value) {
            char buffer[12];
            std::snprintf(buffer, sizeof(buffer), "%d", value);
            put(buffer);
        };
        for (const char* p = input; *p != '\0' && o + 1 < cap;) {
            if (p[0] != '{') {
                out[o++] = *p++;
                continue;
            }
            const char* end = std::strchr(p, '}');
            if (!end) {
                put(p);
                break;
            }
            const std::size_t nameLength = static_cast<std::size_t>(end - p - 1);
            const char* name = p + 1;
            const auto nameIs = [&](const char* candidate) {
                return std::strlen(candidate) == nameLength && std::strncmp(name, candidate, nameLength) == 0;
            };
            if (nameIs("t")) putInt(match.tCount);
            else if (nameIs("ct")) putInt(match.ctCount);
            else if (nameIs("alive")) putInt(match.tAlive + match.ctAlive);
            else if (nameIs("dead")) putInt(match.tDead + match.ctDead);
            else if (nameIs("talive")) putInt(match.tAlive);
            else if (nameIs("tdead")) putInt(match.tDead);
            else if (nameIs("ctalive")) putInt(match.ctAlive);
            else if (nameIs("ctdead")) putInt(match.ctDead);
            else if (nameIs("tdmg")) putInt(match.teamDamage);
            else if (nameIs("mode")) put(match.mode);
            else if (nameIs("myteam")) put(match.tCount >= match.ctCount ? "T" : "CT");
            else {
                for (const char* q = p; q <= end && o + 1 < cap; ++q)
                    out[o++] = *q; // unknown placeholder - literal
                p = end + 1;
                continue;
            }
            p = end + 1;
        }
        out[o] = '\0';
    }

    static void jsonEscape(char* out, std::size_t cap, const char* input) noexcept
    {
        std::size_t o = 0;
        for (const char* p = input; *p != '\0' && o + 2 < cap; ++p) {
            if (static_cast<unsigned char>(*p) < 0x20)
                continue;
            if (*p == '"' || *p == '\\') {
                out[o++] = '\\';
                out[o++] = *p;
            } else {
                out[o++] = *p;
            }
        }
        out[o] = '\0';
    }

    // Writes the wire state the relay consumes: {"clear":true} (disabled), a match object, the
    // fixed in-menu line, or - whenever the web radio is playing - a LISTENING presence for the
    // current station (station beats match: it is what the user is actually doing). Atomic
    // rename so the relay never reads a torn file; content gate so a static presence does not
    // cause pointless SET_ACTIVITY frames.
    void writeStateFile(const MatchState* match, const char* station) noexcept
    {
        loadTemplatesOnce();

        char rendered[1280];
        const bool hasMatch = match && match->inMatch;
        const bool hasRadio = station && station[0] != '\0';

        if (!hasMatch && !hasRadio) {
            std::snprintf(rendered, sizeof(rendered), "{\"clear\":true}");
        } else {
            char matchPart[512] = "null";
            char radioPart[640] = "null";

            if (hasMatch) {
                char details[192];
                char state[192];
                char detailsEscaped[384];
                char stateEscaped[384];
                renderTemplate(details, sizeof(details), detailsTemplate, *match);
                renderTemplate(state, sizeof(state), stateTemplate, *match);
                jsonEscape(detailsEscaped, sizeof(detailsEscaped), details);
                jsonEscape(stateEscaped, sizeof(stateEscaped), state);
                std::snprintf(matchPart, sizeof(matchPart), "{\"details\":\"%s\",\"state\":\"%s\"}", detailsEscaped, stateEscaped);
            }
            if (hasRadio) {
                char stationEscaped[256];
                jsonEscape(stationEscaped, sizeof(stationEscaped), station);
                std::snprintf(radioPart, sizeof(radioPart), "{\"details\":\"%s\",\"state\":\"Neversneeze Web Radio\",\"large\":\"listening to fire while tapping NNs\"}", stationEscaped);
            }
            std::snprintf(rendered, sizeof(rendered), "{\"match\":%s,\"radio\":%s}", matchPart, radioPart);
        }

        std::snprintf(lastRendered, sizeof(lastRendered), "%s", rendered);

        const int fd = ::open(kStatePartPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd < 0)
            return;
        const std::size_t length = std::strlen(rendered);
        std::size_t written = 0;
        while (written < length) {
            const auto chunk = ::write(fd, rendered + written, length - written);
            if (chunk <= 0) {
                ::close(fd);
                ::unlink(kStatePartPath);
                return;
            }
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
        ::rename(kStatePartPath, kStatePath);
    }

    // --- template sidecar (<configDir>/discord_rpc.txt: details line, then state line) ---

    [[nodiscard]] bool templatesFilePath(char (&path)[512]) const noexcept
    {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (!directoryPath)
            return false;
        const auto* dir = reinterpret_cast<const char*>(directoryPath.get());
        std::size_t length = 0;
        while (dir[length] != '\0' && length + 1 < sizeof(path) - sizeof("/discord_rpc.txt"))
            ++length;
        std::memcpy(path, dir, length);
        std::memcpy(path + length, "/discord_rpc.txt", sizeof("/discord_rpc.txt"));
        return true;
    }

    void loadTemplatesOnce() noexcept
    {
        if (templatesLoaded)
            return;
        templatesLoaded = true;
        copyTemplate(detailsTemplate, kDefaultDetails, sizeof(detailsTemplate));
        copyTemplate(stateTemplate, kDefaultState, sizeof(stateTemplate));

        char path[512];
        if (!templatesFilePath(path))
            return;
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0)
            return;
        char fileBuffer[512];
        const auto readBytes = ::read(fd, fileBuffer, sizeof(fileBuffer) - 1);
        ::close(fd);
        if (readBytes <= 0)
            return;
        fileBuffer[readBytes] = '\0';

        const char* lineStart = fileBuffer;
        for (int line = 0; line < 2; ++line) {
            const char* newline = static_cast<const char*>(std::memchr(lineStart, '\n', fileBuffer + readBytes - lineStart));
            const char* lineEnd = newline ? newline : fileBuffer + readBytes;
            char* dst = line == 0 ? detailsTemplate : stateTemplate;
            const std::size_t capacity = line == 0 ? sizeof(detailsTemplate) : sizeof(stateTemplate);
            std::size_t length = 0;
            while (lineStart + length < lineEnd && length + 1 < capacity) {
                dst[length] = lineStart[length];
                ++length;
            }
            dst[length] = '\0';
            if (!newline)
                break;
            lineStart = newline + 1;
        }
    }

    static void copyTemplate(char* dst, const char* src, std::size_t cap) noexcept
    {
        std::size_t i = 0;
        for (; src[i] != '\0' && i < cap - 1; ++i)
            dst[i] = src[i];
        dst[i] = '\0';
    }

    // --- relay lifecycle ----------------------------------------------------------------

    // The relay script is too long for the spawnHostShell command buffer - written to /tmp once
    // per game process (like the mic broadcast switch script).
    void writeRelayScriptOnce() noexcept
    {
        if (relayScriptWritten)
            return;
        relayScriptWritten = true;

        static constexpr char kRelay[] = R"(#!/usr/bin/env python3
import json, os, socket, struct, time

CLIENT_ID = "1545419869732995173"
STATE = "/tmp/ns_discord_rpc.json"

# Opcode numbering per arRPC / the official modern Discord SDK (NOT the legacy discord-rpc C++
# library, whose Frame=0/Handshake=2 numbering makes arRPC read the handshake as CLOSE and hang
# up - that was the "no activity" bug). Verified live against the user's Vesktop + arRPC.
OP_HANDSHAKE = 0
OP_FRAME = 1
OP_CLOSE = 2
OP_PING = 3
OP_PONG = 4

def socket_paths():
    dirs = set()
    env = os.environ.get("XDG_RUNTIME_DIR")
    if env:
        dirs.add(env.rstrip("/"))
    dirs.add("/run/user/%d" % os.getuid())
    paths = []
    for d in sorted(dirs):
        for i in range(10):
            paths.append(os.path.join(d, "discord-ipc-%d" % i))
        paths.append(os.path.join(d, "app", "com.discordapp.Discord", "discord-ipc-0"))
        paths.append(os.path.join(d, "snap.discord", "discord-ipc-0"))
    return paths

def connect():
    for path in socket_paths():
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        try:
            sock.connect(path)
            payload = json.dumps({"v": 1, "client_id": "1545419869732995173"}).encode()
            sock.sendall(struct.pack("<II", OP_HANDSHAKE, len(payload)) + payload)
            read_frame(sock)  # READY dispatch
            return sock
        except OSError:
            try:
                sock.close()
            except Exception:
                pass
    return None

def read_frame(sock):
    header = b""
    while len(header) < 8:
        chunk = sock.recv(8 - len(header))
        if not chunk:
            return None
        header += chunk
    op, length = struct.unpack("<II", header)
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            return None
        body += chunk
    return op, body

def set_activity(sock, activity):
    frame = json.dumps({"cmd": "SET_ACTIVITY",
                        "args": {"pid": os.getpid(), "activity": activity},
                        "nonce": "ns-%f" % time.time()}).encode()
    sock.sendall(struct.pack("<II", OP_FRAME, len(frame)) + frame)
    read_frame(sock)

def main():
    sock = None
    match_start = None
    radio_start = None
    last_seen = None
    while True:
        try:
            with open("/tmp/ns_discord_rpc.json") as f:
                content = f.read()
        except OSError:
            content = None
        if content != last_seen:
            last_seen = content
            try:
                state = json.loads(content) if content else {}
            except Exception:
                state = {}
            if state.get("clear"):
                if sock is not None:
                    try:
                        sock.close()
                    except Exception:
                        pass
                    sock = None
                match_start = None
                radio_start = None
            else:
                match = state.get("match")
                radio = state.get("radio")
                now = time.time()
                if match and match_start is None:
                    match_start = now
                if not match:
                    match_start = None
                if radio and radio_start is None:
                    radio_start = now
                if not radio:
                    radio_start = None

                # both live: alternate every 7s so readers see match info AND the station
                use_radio = bool(radio) and (not match or int(now / 7) % 2 == 1)

                if use_radio and radio:
                    activity = {
                        "type": 2,  # LISTENING
                        "details": radio.get("details", ""),
                        "state": radio.get("state", ""),
                        "assets": {"large_image": "ns2",
                                   "large_text": radio.get("large", "Neversneeze"),
                                   "small_image": "cs2", "small_image_text": "nonprime.club"},
                        "instance": True,
                    }
                    if radio_start:
                        activity["timestamps"] = {"start": radio_start}
                elif match:
                    activity = {
                        "details": match.get("details", ""),
                        "state": match.get("state", ""),
                        "assets": {"large_image": "ns2",
                                   "large_text": "Neversneeze",
                                   "small_image": "cs2", "small_image_text": "nonprime.club"},
                        "instance": True,
                    }
                    if match_start:
                        activity["timestamps"] = {"start": match_start}
                else:
                    activity = None

                while activity is not None:
                    if sock is None:
                        sock = connect()
                        if sock is None:
                            time.sleep(5)
                            continue
                    try:
                        set_activity(sock, activity)
                        break
                    except OSError:
                        try:
                            sock.close()
                        except Exception:
                            pass
                        sock = None
                        time.sleep(5)
        time.sleep(2)

main()
)";

        const int fd = ::open(kRelayScriptPath, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0)
            return;
        constexpr std::size_t length = sizeof(kRelay) - 1;
        std::size_t written = 0;
        while (written < length) {
            const auto chunk = ::write(fd, kRelay + written, length - written);
            if (chunk <= 0) {
                ::close(fd);
                return;
            }
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
    }

    [[nodiscard]] static pid_t spawnHostShell(const char* script) noexcept
    {
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

    void spawnRelay() noexcept
    {
        // exec python3 so the tracked pid IS the relay process (same pattern as the radio's
        // ffplay exec chain). If Discord is not running, the relay retries on its own loop.
        relayPid = spawnHostShell("exec python3 /tmp/ns_discord_rpc.py");
    }

    void killRelay() noexcept
    {
        // The tracked pid is the container-side launch-client; the python itself runs on the
        // host, so kill it there by script name (the radio's pkill-by-marker pattern).
        char* const argv[] = {
            const_cast<char*>("steam-runtime-launch-client"),
            const_cast<char*>("--host"),
            const_cast<char*>("--"),
            const_cast<char*>("pkill"),
            const_cast<char*>("-f"),
            const_cast<char*>("ns_discord_rpc.py"),
            nullptr,
        };
        pid_t pid{};
        if (::posix_spawn(&pid, kLaunchClientPath, nullptr, nullptr, argv, environ) == 0)
            ::waitpid(pid, nullptr, 0);

        if (relayPid > 0) {
            ::kill(relayPid, SIGKILL);
            ::waitpid(relayPid, nullptr, 0);
            relayPid = 0;
        }
    }

    // Feature objects are rebuilt per command - all cross-call state is process-global.
    inline static bool relaySpawned = false;
    inline static bool relayScriptWritten = false;
    inline static pid_t relayPid = 0;
    inline static float lastUpdate = -10.0f;
    inline static char lastRendered[1024] = {};
    inline static bool templatesLoaded = false;
    inline static char detailsTemplate[192] = {};
    inline static char stateTemplate[192] = {};

    HookContext& hookContext;
};

// posix_spawn environment (unistd.h only declares it under feature macros - mirror RadioManager).
extern "C" char** environ;