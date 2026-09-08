#pragma once

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
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
//                                                   "Timestamp"; this build writes no
//                                                   "mostrecent" field) -> SteamID64 + PersonaName
//   ~/.local/share/Steam/config/avatarcache/<sid>.png  (local copy of the profile avatar)
// and falls back to the public profile XML (steamcommunity.com/profiles/<sid>?xml=1 -> avatarFull,
// same host-curl pattern as the web radio) when the cache misses. Outputs land in /tmp
// (guaranteed shared with the container): ns_steam_persona.txt (name) + ns_steam_avatar.png.
//
// This file only owns the fetch + the name cache; the avatar file is consumed by the menu's
// loadAvatar() (which retries until the texture is staged).
namespace steam_persona
{

static constexpr const char* kScriptPath = "/tmp/ns_steam_persona.sh";
static constexpr const char* kNamePath = "/tmp/ns_steam_persona.txt";

inline bool fetchStarted = false;
inline char personaName[64] = {};
inline float nextNameRead = 0.0f;

inline constexpr char kScript[] = R"(#!/bin/sh
V="$HOME/.local/share/Steam/config/loginusers.vdf"
C="$HOME/.local/share/Steam/config/avatarcache"
[ -f "$V" ] || exit 0
SID=$(awk '
  match($0, /"7656[0-9]+"/) { key=substr($0, RSTART+1, RLENGTH-2) }
  /"Timestamp"/ {
    ts=$0; sub(/.*"Timestamp"[ \t]*"/, "", ts); sub(/".*/, "", ts)
    if (ts+0 > best) { best=ts+0; bestkey=key }
  }
  END { print bestkey }' "$V")
[ -n "$SID" ] || exit 0
NAME=$(awk -v sid="\"$SID\"" '
  index($0, sid) { inblk=1; next }
  inblk && /^\t}/ { exit }
  inblk && /"PersonaName"/ {
    sub(/.*"PersonaName"[ \t]*"/, ""); sub(/"$/, ""); print; exit }' "$V")

if [ -f "$C/$SID.png" ]; then
  cp "$C/$SID.png" /tmp/ns_steam_avatar.png
else
  URL=$(curl -s --max-time 15 "https://steamcommunity.com/profiles/$SID/?xml=1" | grep -o '<avatarFull><!\[CDATA\[[^]]*\]\]></avatarFull>' | head -1 | sed 's/.*\[CDATA\[//; s/\]\].*//')
  if [ -n "$URL" ]; then
    curl -s --max-time 20 "$URL" -o /tmp/ns_steam_avatar.png.part && mv -f /tmp/ns_steam_avatar.png.part /tmp/ns_steam_avatar.png
  fi
fi
printf "%s" "$NAME" > /tmp/ns_steam_persona.txt
)";

// Writes the script and spawns it (fire-and-forget; outputs are polled from /tmp). Once per
// session, first menu frame.
inline void ensureFetchStarted() noexcept
{
    if (fetchStarted)
        return;
    fetchStarted = true;

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
        char* const argv[] = {
            const_cast<char*>("steam-runtime-launch-client"),
            const_cast<char*>("--host"),
            const_cast<char*>("--"),
            const_cast<char*>("sh"),
            const_cast<char*>("-c"),
            const_cast<char*>("exec sh /tmp/ns_steam_persona.sh"),
            nullptr,
        };
        pid_t pid{};
        if (::posix_spawn(&pid, "/usr/bin/steam-runtime-launch-client", nullptr, nullptr, argv, environ) == 0)
            ::waitpid(pid, nullptr, WNOHANG);
    }
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