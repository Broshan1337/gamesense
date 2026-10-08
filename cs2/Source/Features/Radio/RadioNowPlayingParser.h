#pragma once

#include <cstddef>













struct MprisNowPlaying {
    char player[48];
    char title[128];
    char artist[128];
    bool paused;
    char artwork[512];
};

class RadioNowPlayingParser {
public:
    
    
    
    
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

    
    
    
    
    [[nodiscard]] static bool parseMprisLine(const char* line, MprisNowPlaying& out) noexcept
    {
        if (!line)
            return false;

        char fields[5][sizeof(MprisNowPlaying::artwork)]{};
        int fieldIndex = 0;
        int fi = 0;
        for (const char* p = line;; ++p) {
            if (*p == '\x1f' && fieldIndex < 4) {
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
        copy(out.artwork, fields[4], sizeof(out.artwork));
        return true;
    }

    // playerctl --all-players can report a paused browser ahead of active music.
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
