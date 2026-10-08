#pragma once
#include <span>
#include <cstddef>
#include <string_view>
#include "ConfigParams.h"
namespace config_document {
// Validate before touching live settings. The config format contains objects,
// numeric scalars and booleans; strings are keys, never values.
class Validator {
public:
    explicit Validator(std::span<const char8_t> input) : input{input} {}
    bool valid() noexcept { const bool result = object(1); whitespace(); return result && at == input.size(); }
private:
    void whitespace() noexcept { while (at < input.size() && (input[at] == ' ' || input[at] == '\t' || input[at] == '\r' || input[at] == '\n')) ++at; }
    bool take(char8_t c) noexcept { whitespace(); if (at < input.size() && input[at] == c) { ++at; return true; } return false; }
    bool digits() noexcept { const auto start = at; while (at < input.size() && input[at] >= '0' && input[at] <= '9') ++at; return at > start; }
    bool key() noexcept {
        if (!take('"')) return false;
        while (at < input.size()) {
            const auto c = input[at++];
            if (c == '"') return true;
            // Schema keys are plain names; reject malformed escapes and controls.
            if (c < 32 || c == '\\') return false;
        }
        return false;
    }
    bool scalar() noexcept {
        whitespace();
        for (auto word : {std::u8string_view{u8"true"}, std::u8string_view{u8"false"}}) {
            if (input.size() - at >= word.size() && std::u8string_view{input.data() + at, word.size()} == word) { at += word.size(); return true; }
        }
        if (at < input.size() && input[at] == '-') ++at;
        if (at < input.size() && input[at] == '0') ++at;
        else if (!digits()) return false;
        if (at < input.size() && input[at] == '.') { ++at; if (!digits()) return false; }
        if (at < input.size() && (input[at] == 'e' || input[at] == 'E')) {
            ++at; if (at < input.size() && (input[at] == '+' || input[at] == '-')) ++at;
            if (!digits()) return false;
        }
        return true;
    }
    bool object(unsigned depth) noexcept {
        if (depth > config_params::kMaxNestingLevel || !take('{')) return false;
        if (take('}')) return true;
        do {
            if (!key() || !take(':')) return false;
            whitespace();
            if (at < input.size() && input[at] == '{') { if (!object(depth + 1)) return false; }
            else if (!scalar()) return false;
            if (take('}')) return true;
        } while (take(','));
        return false;
    }
    std::span<const char8_t> input;
    std::size_t at{};
};
inline bool valid(std::span<const char8_t> input) noexcept { return Validator{input}.valid(); }
}
