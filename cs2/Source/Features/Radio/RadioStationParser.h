#pragma once

#include <cstddef>



struct RadioStation {
    char id[24];        
    char text[96];      
    char subtext[192];  
};










class RadioStationParser {
public:
    [[nodiscard]] static int parse(const char* buf, std::size_t len, RadioStation* out, int maxOut,
                                   char* headTitleOut, int headTitleCap) noexcept
    {
        if (headTitleOut && headTitleCap > 0)
            headTitleOut[0] = '\0';

        constexpr int kMaxDepth = 24;
        char curText[kMaxDepth][sizeof(RadioStation::text)];
        char curId[kMaxDepth][sizeof(RadioStation::id)];
        char curSub[kMaxDepth][sizeof(RadioStation::subtext)];
        bool isStation[kMaxDepth];

        int depth = 0;
        int count = 0;
        bool expectingValue = false;
        bool headTitleSet = false;
        char key[32];
        key[0] = '\0';

        std::size_t pos = 0;
        while (pos < len) {
            const char c = buf[pos];

            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
                continue;
            }

            if (c == '{') {
                ++depth;
                if (depth < kMaxDepth) {
                    curText[depth][0] = '\0';
                    curId[depth][0] = '\0';
                    curSub[depth][0] = '\0';
                    isStation[depth] = false;
                }
                expectingValue = false;
                ++pos;
                continue;
            }

            if (c == '}') {
                if (depth > 0 && depth < kMaxDepth && isStation[depth] && curId[depth][0] != '\0' && count < maxOut) {
                    copy(out[count].id, curId[depth], sizeof(out[count].id));
                    copy(out[count].text, curText[depth], sizeof(out[count].text));
                    copy(out[count].subtext, curSub[depth], sizeof(out[count].subtext));
                    ++count;
                }
                if (depth > 0)
                    --depth;
                expectingValue = false;
                ++pos;
                continue;
            }

            if (c == '[') {
                expectingValue = false;
                ++pos;
                continue;
            }

            if (c == ']' || c == ',') {
                ++pos;
                continue;
            }

            if (c == ':') {
                expectingValue = true;
                ++pos;
                continue;
            }

            if (c == '"') {
                char token[sizeof(RadioStation::subtext)];
                pos = readString(buf, len, pos, token, sizeof(token));
                if (expectingValue) {
                    assignField(key, token, depth, kMaxDepth,
                                curText, curId, curSub, isStation,
                                headTitleOut, headTitleCap, headTitleSet);
                    expectingValue = false;
                } else {
                    copy(key, token, sizeof(key));
                }
                continue;
            }

            
            expectingValue = false;
            while (pos < len) {
                const char d = buf[pos];
                if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t' || d == '\r' || d == '\n')
                    break;
                ++pos;
            }
        }

        return count;
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

    
    
    static std::size_t readString(const char* buf, std::size_t len, std::size_t pos, char* out, int cap) noexcept
    {
        ++pos; 
        int oi = 0;
        while (pos < len) {
            const char c = buf[pos++];
            if (c == '"')
                break;
            if (c == '\\' && pos < len) {
                const char e = buf[pos++];
                char decoded;
                switch (e) {
                case 'n': decoded = '\n'; break;
                case 't': decoded = '\t'; break;
                case 'r': decoded = '\r'; break;
                case 'b': decoded = '\b'; break;
                case 'f': decoded = '\f'; break;
                case 'u': {
                    
                    for (int k = 0; k < 4 && pos < len; ++k)
                        ++pos;
                    decoded = '?';
                    break;
                }
                default: decoded = e; break; 
                }
                if (oi < cap - 1)
                    out[oi++] = decoded;
                continue;
            }
            if (oi < cap - 1)
                out[oi++] = c;
        }
        out[oi] = '\0';
        return pos;
    }

    static void assignField(const char* key, const char* value, int depth, int maxDepth,
                            char curText[][sizeof(RadioStation::text)],
                            char curId[][sizeof(RadioStation::id)],
                            char curSub[][sizeof(RadioStation::subtext)],
                            bool* isStation,
                            char* headTitleOut, int headTitleCap, bool& headTitleSet) noexcept
    {
        if (!headTitleSet && headTitleOut && headTitleCap > 0 && equals(key, "title")) {
            copy(headTitleOut, value, headTitleCap);
            headTitleSet = true;
            return;
        }

        if (depth <= 0 || depth >= maxDepth)
            return;

        if (equals(key, "item")) {
            if (equals(value, "station"))
                isStation[depth] = true;
        } else if (equals(key, "guide_id")) {
            copy(curId[depth], value, sizeof(RadioStation::id));
        } else if (equals(key, "text")) {
            copy(curText[depth], value, sizeof(RadioStation::text));
        } else if (equals(key, "subtext")) {
            copy(curSub[depth], value, sizeof(RadioStation::subtext));
        }
    }
};
