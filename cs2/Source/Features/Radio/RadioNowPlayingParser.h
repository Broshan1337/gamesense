#pragma once

#include <cstddef>

// Now-playing records for the HUD radio element (Features/Hud draw + RadioManager poll).
//
// Two tiny parsers, kept next to RadioStationParser for the same reason: fixed buffers and
// hand-rolled scans because the module is -nostdlib (no std::string), and pure/stringy so the
// unit tests can pin the exact host-side formats without spawning anything.
//
//   - extractStreamTitle: one ICY in-stream metadata block as delivered by the burst probe
//     (ns_radio_meta.py), e.g. "StreamTitle='Artist - Track';StreamUrl='...';". Titles are the
//     whole point of the probe, so this is where "does this station even publish titles" dies.
//   - parseMprisLine: one host-side `playerctl metadata --format` record (0x1f-separated
//     player/title/artist/status) for the MPRIS fallback shown while no radio station plays.

struct MprisNowPlaying {
    char player[48];
    char title[128];
    char artist[128];
    bool paused;
};

class RadioNowPlayingParser {
public:
    // Copies the StreamTitle value out of a raw ICY metadata block. Returns false when the block
    // carries no (non-empty, terminated) StreamTitle - the caller then shows the station name only.
    // ICY has no escaping: the first ' after the opening one ends the title, by spec and by
    // observation (verified against Bauer/sharp-stream servers, icy-metaint intervals).
    [[nodiscard]] static bool extractStreamTitle(const char* block, char* out, int cap) noexcept
    {
        if (!block || !out || cap < 1)
            return false;

        constexpr char marker[] = "StreamTitle='";
        int mi = 0;
        const char* title = nullptr;
        for (const char* p = block; *p != '\0'; ++p) {
            if (marker[mi] == '\0') {
                title = p;
                break;
            }
            if (*p == marker[mi])
                ++mi;
            else
                mi = (*p == marker[0]) ? 1 : 0;
        }
        if (!title)
            return false;

        int oi = 0;
        for (const char* p = title; *p != '\0'; ++p) {
            if (*p == '\'') {
                out[oi] = '\0';
                return oi > 0;
            }
            if (oi < cap - 1)
                out[oi++] = *p;
        }
        return false;
    }

    // Parses "<player>\x1f<title>\x1f<artist>\x1f<status>" (playerctl --format output).
    // Returns false (and leaves `out` usable but meaningless) unless a player AND a title are
    // present - a bare player name is not worth an HUD box. `paused` is true unless the status
    // field is exactly "Playing".
    [[nodiscard]] static bool parseMprisLine(const char* line, MprisNowPlaying& out) noexcept
    {
        if (!line)
            return false;

        char fields[4][sizeof(MprisNowPlaying::title)];
        int fieldIndex = 0;
        int fi = 0;
        for (const char* p = line;; ++p) {
            if (*p == '\x1f' && fieldIndex < 3) {
                fields[fieldIndex][fi] = '\0';
                ++fieldIndex;
                fi = 0;
                continue;
            }
            if (*p == '\0' || *p == '\n') {
                fields[fieldIndex][fi] = '\0';
                break;
            }
            if (fi < static_cast<int>(sizeof(fields[0])) - 1)
                fields[fieldIndex][fi++] = *p;
        }
        if (fieldIndex < 3)
            return false;

        if (fields[0][0] == '\0' || fields[1][0] == '\0'
            || (!equals(fields[3], "Playing") && !equals(fields[3], "Paused")))
            return false;

        copy(out.player, fields[0], sizeof(out.player));
        copy(out.title, fields[1], sizeof(out.title));
        copy(out.artist, fields[2], sizeof(out.artist));
        out.paused = !equals(fields[3], "Playing");
        return true;
    }

[[nodiscard]] static bool parseMprisOutput(const char* text, MprisNowPlaying& out) noexcept
    {
        if (!text)
            return false;
        bool found = false;
        for (const char* line = text; *line;) {
            MprisNowPlaying candidate{};
            if (parseMprisLine(line, candidate)) {
                if (!candidate.paused) {
                    out = candidate;
                    return true;
                }
                if (!found) {
                    out = candidate;
                    found = true;
                }
            }
            while (*line && *line != '\n')
                ++line;
            if (*line)
                ++line;
        }
        return found;
    }

private:
    static void copy(char* dst, const char* src, int cap) noexcept
    {
        int i = 0;
        for (; src[i] != '\0' && i < cap - 1; ++i)
            dst[i] = src[i];
        dst[i] = '\0';
    }

    static bool equals(const char* a, const char* b) noexcept
    {
        int i = 0;
        for (; a[i] != '\0' && b[i] != '\0'; ++i)
            if (a[i] != b[i])
                return false;
        return a[i] == b[i];
    }
};
