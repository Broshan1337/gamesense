#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace feature_binds {
struct Record {
    std::uint64_t id{};
    int key{};
    bool holdMode{};
    bool hasValue{};
    double value{};
};
[[nodiscard]] inline bool parseRecord(const char* line, Record& record, int lastKey) noexcept
{
    if (!line)
        return false;
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    const auto skip = [&] { while (space(*line)) ++line; };
    const auto number = [&](std::uint64_t& value, unsigned base) {
        value = 0;
        bool any = false;
        for (;;) {
            const char c = *line;
            const unsigned digit = c >= '0' && c <= '9' ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
            if (digit >= base)
                break;
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / base)
                return false;
            value = value * base + digit;
            ++line;
            any = true;
        }
        return any;
    };
    Record parsed;
    std::uint64_t key{}, mode{};
    skip();
    if (!number(parsed.id, 16) || !space(*line)) return false;
    skip();
    if (!number(key, 10) || key > static_cast<unsigned>(lastKey) || !space(*line)) return false;
    skip();
    if (!number(mode, 10) || mode > 1 || (*line && !space(*line))) return false;
    parsed.key = static_cast<int>(key);
    parsed.holdMode = mode != 0;
    skip();
    if (*line) {
        char* end{};
        parsed.value = std::strtod(line, &end);
        if (end == line || !std::isfinite(parsed.value)) return false;
        line = end;
        skip();
        if (*line) return false;
        parsed.hasValue = true;
    }
    record = parsed;
    return true;
}
}
