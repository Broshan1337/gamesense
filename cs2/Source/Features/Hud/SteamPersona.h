#pragma once

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>

// posix_spawn environment (unistd.h only declares it under feature macros - mirror RadioManager;
// must stay at global scope).
extern "C" char** environ;

// Local Steam persona for the account bar (name + avatar.png for the existing avatar loader).
//
// All IO happens on the HOST (the Steam client's own data lives there; the game runs inside the
// Steam container): a small shell script written once to /tmp/ns_steam_persona.sh and spawned
// through steam-runtime-launch-client reads
//   ~/.local/share/Steam/config/loginusers.vdf   -> most recently used account (newest
//                                                   timestamp - NOTE: a Steam client update
//                                                   (~Sep 2026) changed the field casing from
//                                                   "Timestamp" to "timestamp", which killed a
//                                                   case-sensitive parser silently; match
//                                                   case-insensitively) -> SteamID64 + PersonaName
//   ~/.local/share/Steam/config/avatarcache/<sid>.png  (local copy of the profile avatar)
// and falls back to the public profile XML (steamcommunity.com/profiles/<sid>?xml=1 -> avatarFull,
// same host-curl pattern as the web radio) when the cache misses. Outputs land in /tmp
// (guaranteed shared with the container): ns_steam_persona.txt (name) + ns_steam_avatar.png.
// The name file is written BEFORE the avatar section so a slow/blocked steamcommunity fetch can
// never delay the name.
//
// This file owns the fetch + the name cache; the avatar file is consumed by the menu's
// loadAvatar() (which retries until the texture is staged).
namespace steam_persona
{

static constexpr const char* kScriptPath = "/tmp/ns_steam_persona.sh";
static constexpr const char* kNamePath = "/tmp/ns_steam_persona.txt";

inline bool scriptWritten = false;
inline int fetchAttempts = 0;
inline float nextFetchAttempt = 0.0f;
inline char personaName[64] = {};
inline float nextNameRead = 0.0f;

inline constexpr int kMaxFetchAttempts = 40;     // ~10 min of retries, then give up for the session
inline constexpr float kFetchRetryDelay = 15.0f;

inline constexpr char kScript[] = R"(#!/bin/sh
V="$HOME/.local/share/Steam/config/loginusers.vdf"
C="$HOME/.local/share/Steam/config/avatarcache"
[ -f "$V" ] || exit 0
SID=$(awk '
  match($0, /"7656[0-9]+"/) { key=substr($0, RSTART+1, RLENGTH-2) }
  tolower($0) ~ /"timestamp"/ {
    n = split($0, a, "\""); ts = a[n-1]
    if (ts+0 > best) { best=ts+0; bestkey=key }
  }
  END { print bestkey }' "$V")
[ -n "$SID" ] || exit 0
NAME=$(awk -v sid="\"$SID\"" '
  index($0, sid) { inblk=1; next }
  inblk && /^\t}/ { exit }
  inblk && tolower($0) ~ /"personaname"/ {
    n = split($0, a, "\""); print a[n-1]; exit }' "$V")

printf "%s" "$NAME" > /tmp/ns_steam_persona.txt

if [ -f "$C/$SID.png" ]; then
  cp "$C/$SID.png" /tmp/ns_steam_avatar.png
else
  URL=$(curl -s --max-time 15 "https://steamcommunity.com/profiles/$SID/?xml=1" | grep -o '<avatarFull><!\[CDATA\[[^]]*\]\]></avatarFull>' | head -1 | sed 's/.*\[CDATA\[//; s/\]\].*//')
  if [ -n "$URL" ]; then
    curl -s --max-time 20 "$URL" -o /tmp/ns_steam_avatar.png.part && mv -f /tmp/ns_steam_avatar.png.part /tmp/ns_steam_avatar.png
  fi
fi
)";

// Writes the script once and spawns it fire-and-forget; outputs are polled from /tmp. The spawn
// itself retries (rate-limited) while no name has arrived, because a single failed spawn used to
// mean the fallback label for the whole session. The launcher is resolved at spawn time:
// /usr/bin/steam-runtime-launch-client is the canonical in-container path, the pv-runtime copies
// under the Steam dir are the fallback (the Steam client dir is mounted into the container), and
// a direct /bin/sh run is the last resort - the script's [ -f ... ] guards make that a harmless
// no-op when the Steam client's config is not shared into the container.
inline void spawnFetch() noexcept
{
    char launcher[512];
    std::snprintf(launcher, sizeof(launcher), "%s", "/usr/bin/steam-runtime-launch-client");
    bool useLauncher = ::access(launcher, X_OK) == 0;
    if (!useLauncher) {
        if (const char* const home = ::getenv("HOME")) {
            static constexpr const char* kPvCandidates[] = {
                "/.local/share/Steam/steamrt64/pv-runtime/steam-runtime-steamrt/pressure-vessel/bin/steam-runtime-launch-client",
                "/.local/share/Steam/steamrt64/pv-runtime/steam-runtime-steamrt/bin/steam-runtime-launch-client",
            };
            for (const char* const suffix : kPvCandidates) {
                char candidate[512];
                const int written = std::snprintf(candidate, sizeof(candidate), "%s%s", home, suffix);
                if (written > 0 && static_cast<std::size_t>(written) < sizeof(candidate) && ::access(candidate, X_OK) == 0) {
                    std::snprintf(launcher, sizeof(launcher), "%s", candidate);
                    useLauncher = true;
                    break;
                }
            }
        }
    }

    pid_t pid{};
    if (useLauncher) {
        char* const argv[] = {
            const_cast<char*>("steam-runtime-launch-client"),
            const_cast<char*>("--host"),
            const_cast<char*>("--"),
            const_cast<char*>("sh"),
            const_cast<char*>("-c"),
            const_cast<char*>("exec sh /tmp/ns_steam_persona.sh"),
            nullptr,
        };
        if (::posix_spawn(&pid, launcher, nullptr, nullptr, argv, environ) == 0)
            ::waitpid(pid, nullptr, WNOHANG);
    } else {
        char* const argv[] = {
            const_cast<char*>("/bin/sh"),
            const_cast<char*>("-c"),
            const_cast<char*>("exec sh /tmp/ns_steam_persona.sh"),
            nullptr,
        };
        if (::posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, environ) == 0)
            ::waitpid(pid, nullptr, WNOHANG);
    }
}

inline void ensureFetchStarted() noexcept
{
    if (personaName[0] != '\0')
        return; // name fetched (avatar staging retries separately) - done for this session
    if (fetchAttempts >= kMaxFetchAttempts)
        return; // give up for this session

    const float now = ImGui::GetTime();
    if (scriptWritten && now < nextFetchAttempt)
        return;

    if (!scriptWritten) {
        scriptWritten = true;
        const int fd = ::open(kScriptPath, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd >= 0) {
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
    }

    ++fetchAttempts;
    nextFetchAttempt = now + kFetchRetryDelay;
    spawnFetch();
}

// Cached persona name; re-reads /tmp at most every 2s until a non-empty name arrives.
// "" = not fetched yet (caller renders its fallback label).
inline const char* name() noexcept
{
    if (personaName[0] == '\0' && ImGui::GetTime() >= nextNameRead) {
        nextNameRead = ImGui::GetTime() + 2.0f;
        const int fd = ::open(kNamePath, O_RDONLY);
        if (fd >= 0) {
            char buffer[96];
            const ssize_t bytes = ::read(fd, buffer, sizeof(buffer) - 1);
            ::close(fd);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                // strip a trailing newline / CR, cap to the buffer
                std::size_t length = static_cast<std::size_t>(bytes);
                while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
                    buffer[--length] = '\0';
                if (length >= sizeof(personaName))
                    length = sizeof(personaName) - 1;
                std::memcpy(personaName, buffer, length);
                personaName[length] = '\0';
            }
        }
    }
    return personaName;
}

} // namespace steam_persona