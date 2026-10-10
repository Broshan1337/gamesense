#pragma once

#include <span>

#include "ConfigStringConversionState.h"
#include <Platform/Macros/FunctionAttributes.h>

// SkipUnknownKeys: the load-time mode. The strict sequential walk (default)
// requires file keys in exact schema order; the tolerant mode additionally
// skips key/value pairs the schema does not know, so a config file saved by
// an older schema revision (extra/renamed keys) still loads. The real load
// path (Config.h) runs valid() first and always passes the flag; the raw
// parser default keeps the documented strict state machine.
struct SkipUnknownKeysTag {
    explicit SkipUnknownKeysTag() = default;
};
inline constexpr SkipUnknownKeysTag skipUnknownKeys{};

class ConfigFromString {
public:
    ConfigFromString(std::span<const char8_t> buffer, ConfigStringConversionState& conversionState) noexcept
        : buffer{buffer}
        , conversionState{conversionState}
    {
    }

    ConfigFromString(std::span<const char8_t> buffer, ConfigStringConversionState& conversionState, SkipUnknownKeysTag) noexcept
        : buffer{buffer}
        , conversionState{conversionState}
        , skipUnknownKeys{true}
    {
    }

    void beginRoot() noexcept
    {
        if (shouldReadMe() && skipWhitespaces() && readChar(u8'{'))
            increaseConversionNestingLevel();
        increaseNestingLevel();
    }

    [[nodiscard]] std::size_t endRoot() noexcept
    {
        if (shouldReadMe() && skipWhitespaces() && readChar(u8'}'))
            markConversionComplete();
        decreaseNestingLevel();
        conversionState.offset += readIndex;
        return readIndex;
    }

    void beginObject(const char8_t* key) noexcept
    {
        if (shouldReadMe()) {
            const auto previousReadIndex = readIndex;
            if (readUntilStartOfValue(key) && readChar(u8'{'))
                increaseConversionNestingLevel();
            else
                readIndex = previousReadIndex;
        }
        increaseNestingLevel();
    }

    void endObject() noexcept
    {
        // Tolerant mode only: skip pairs the schema does not consume
        // (older-revision leftovers) up to this object's closing brace, so
        // the walk stays in sync with the file even when the schema dropped
        // or renamed keys. Strict mode keeps the documented whitespace +
        // closing-brace read.
        if (shouldReadMe() && (skipUnknownKeys ? skipUntilObjectEnd() : skipWhitespaces()) && readChar(u8'}'))
            decreaseConversionNestingLevel();
        decreaseNestingLevel();
    }

    void boolean(const char8_t* key, auto&& valueSetter, auto&& )
    {
        if (bool value; parseBool(key, value))
            valueSetter(value);
    }

    void uint(const char8_t* key, auto&& valueSetter, auto&& )
    {
        if (std::uint64_t value; parseUint(key, value))
            valueSetter(value);
    }

    void floating(const char8_t* key, auto&& valueSetter, auto&& )
    {
        if (float value; parseFloat(key, value))
            valueSetter(value);
    }

private:
    [[nodiscard]] bool parseBool(const char8_t* key, bool& value) noexcept
    {
        bool parsed = false;
        if (shouldReadMe()) {
            const auto previousReadIndex = readIndex;
            if (readUntilStartOfValue(key) && parseBool(value)) {
                parsed = true;
                markElementReadAtCurrentNestingLevel();
            } else {
                readIndex = previousReadIndex;
            }
        }
        increaseIndexInNestingLevel();
        return parsed;
    }

    [[nodiscard]] bool parseUint(const char8_t* key, std::uint64_t& value) noexcept
    {
        bool parsed = false;
        if (shouldReadMe()) {
            const auto previousReadIndex = readIndex;
            if (readUntilStartOfValue(key) && parseUint(value)) {
                parsed = true;
                markElementReadAtCurrentNestingLevel();
            } else {
                readIndex = previousReadIndex;
            }
        }
        increaseIndexInNestingLevel();
        return parsed;
    }

    [[nodiscard]] bool parseFloat(const char8_t* key, float& value) noexcept
    {
        bool parsed = false;
        if (shouldReadMe()) {
            const auto previousReadIndex = readIndex;
            if (readUntilStartOfValue(key) && parseFloatValue(value)) {
                parsed = true;
                markElementReadAtCurrentNestingLevel();
            } else {
                readIndex = previousReadIndex;
            }
        }
        increaseIndexInNestingLevel();
        return parsed;
    }

    void markConversionComplete() noexcept
    {
        assert(conversionState.nestingLevel == 1);
        assert(conversionState.indexInNestingLevel[0] == 0);
        ++conversionState.indexInNestingLevel[--conversionState.nestingLevel];
    }

    void markElementReadAtCurrentNestingLevel() noexcept
    {
        conversionState.indexInNestingLevel[conversionState.nestingLevel] = 0;
    }

    [[nodiscard]] bool readUntilStartOfValue(const char8_t* key) noexcept
    {
        return readCommaAfterPreviousElement() && readKey(key) && skipWhitespaces();
    }

    [[nodiscard]] bool readCommaAfterPreviousElement() noexcept
    {
        if (conversionState.indexInNestingLevel[conversionState.nestingLevel] != config_params::kInvalidObjectIndex) {
            skipWhitespaces();
            return readChar(u8',');
        }
        return true;
    }

    [[nodiscard]] bool readKey(const char8_t* key) noexcept
    {
        if (!(skipWhitespaces() && readChar(u8'"')))
            return false;
        if (!skipUnknownKeys)
            return readString(key) && readChar(u8'"') && skipWhitespaces() && readChar(u8':');
        const auto entryReadIndex = readIndex;
        while (true) {
            if (readString(key) && readChar(u8'"') && skipWhitespaces() && readChar(u8':'))
                return true;
            // The key at the cursor is not the expected one (or a longer one):
            // it was left by an older schema revision. Skip it (string, ':',
            // value, ',') and try the next key instead of failing the load.
            if (!(skipKeyRest() && readChar(u8'"') && skipWhitespaces() && readChar(u8':') && skipValue()))
                break;
            if (!(skipWhitespaces() && readChar(u8',') && skipWhitespaces() && readChar(u8'"')))
                break;
        }
        // Expected key not in this object: restore the cursor so the caller's
        // save/restore contract (and the object-end skip) sees the old state.
        readIndex = entryReadIndex;
        return false;
    }

    // Consume the remaining characters of a partially-read key up to (not
    // including) its closing quote.
    [[nodiscard]] bool skipKeyRest() noexcept
    {
        while (readIndex < buffer.size()) {
            if (buffer[readIndex] == u8'"')
                return true;
            ++readIndex;
        }
        return false;
    }

    // Skip one value: nested object (brace-depth skip), string, bool or number.
    [[nodiscard]] bool skipValue() noexcept
    {
        if (!skipWhitespaces() || readIndex >= buffer.size())
            return false;
        const auto c = buffer[readIndex];
        if (c == u8'{') {
            unsigned depth = 0;
            while (readIndex < buffer.size()) {
                const auto ch = buffer[readIndex];
                ++readIndex;
                if (ch == u8'{') {
                    ++depth;
                } else if (ch == u8'}') {
                    if (--depth == 0)
                        return true;
                }
            }
            return false;
        }
        if (c == u8'"') {
            ++readIndex;
            return skipKeyRest() && readChar(u8'"');
        }
        {
            const auto savedReadIndex = readIndex;
            if (readString(u8"true"))
                return true;
            readIndex = savedReadIndex;
            if (readString(u8"false"))
                return true;
            readIndex = savedReadIndex;
        }
        const auto start = readIndex;
        while (readIndex < buffer.size()) {
            const auto ch = buffer[readIndex];
            if ((ch >= u8'0' && ch <= u8'9') || ch == u8'-' || ch == u8'+' || ch == u8'.' || ch == u8'e' || ch == u8'E')
                ++readIndex;
            else
                break;
        }
        return readIndex > start;
    }

    // Skip key/value pairs (older-revision leftovers) until this object's
    // closing brace (not consumed).
    [[nodiscard]] bool skipUntilObjectEnd() noexcept
    {
        while (true) {
            skipWhitespaces();
            if (readIndex >= buffer.size())
                return false;
            const auto c = buffer[readIndex];
            if (c == u8'}')
                return true;
            if (c == u8',') {
                ++readIndex;
                continue;
            }
            if (c != u8'"')
                return false;
            ++readIndex;
            if (!(skipKeyRest() && readChar(u8'"') && skipWhitespaces() && readChar(u8':') && skipValue()))
                return false;
        }
    }

    void increaseNestingLevel() noexcept
    {
        assert(nestingLevel < config_params::kMaxNestingLevel);
        indexInNestingLevel[++nestingLevel] = 0;
    }

    void increaseConversionNestingLevel() noexcept
    {
        assert(conversionState.nestingLevel < config_params::kMaxNestingLevel);
        conversionState.indexInNestingLevel[conversionState.nestingLevel] = indexInNestingLevel[nestingLevel];
        conversionState.indexInNestingLevel[++conversionState.nestingLevel] = config_params::kInvalidObjectIndex;
    }

    void decreaseNestingLevel() noexcept
    {
        assert(nestingLevel > 0);
        assert(indexInNestingLevel[nestingLevel - 1] < config_params::kMaxObjectIndex);
        ++indexInNestingLevel[--nestingLevel];
    }

    void decreaseConversionNestingLevel() noexcept
    {
        assert(conversionState.nestingLevel > 0);
        --conversionState.nestingLevel;
    }  

    void increaseIndexInNestingLevel() noexcept
    {
        ++indexInNestingLevel[nestingLevel];
    }

    [[nodiscard]] bool shouldReadMe() const noexcept
    {
        if (conversionState.nestingLevel != nestingLevel)
            return false;
        for (auto i = 0; i < conversionState.nestingLevel; ++i) {
            if (indexInNestingLevel[i] != conversionState.indexInNestingLevel[i])
                return false;
        }
        return true;
    }

    [[nodiscard]] bool parseUint(std::uint64_t& result) noexcept
    {
        result = 0;
        bool parsedAtLeastOneDigit = false;
        while (readIndex < buffer.size()) {
            if (const char c = buffer[readIndex]; c >= u8'0' && c <= u8'9') {
                parsedAtLeastOneDigit = true;
                ++readIndex;
                const auto lastResult = result;
                result *= 10;
                result += c - u8'0';
                if (result < lastResult)
                    return false;
            } else {
                return parsedAtLeastOneDigit;
            }
        }
        return false;
    }

    
    
    [[nodiscard]] bool parseFloatValue(float& result) noexcept
    {
        bool negative = false;
        if (readIndex < buffer.size() && (buffer[readIndex] == u8'-' || buffer[readIndex] == u8'+')) {
            negative = buffer[readIndex] == u8'-';
            ++readIndex;
        }

        std::uint64_t integerPart = 0;
        if (!parseUint(integerPart))
            return false;

        float value = static_cast<float>(integerPart);
        if (readIndex < buffer.size() && buffer[readIndex] == u8'.') {
            ++readIndex;
            float fraction = 0.0f;
            float scale = 0.1f;
            bool parsedFractionDigit = false;
            while (readIndex < buffer.size() && buffer[readIndex] >= u8'0' && buffer[readIndex] <= u8'9') {
                fraction += static_cast<float>(buffer[readIndex] - u8'0') * scale;
                scale *= 0.1f;
                parsedFractionDigit = true;
                ++readIndex;
            }
            if (!parsedFractionDigit)
                return false;
            value += fraction;
        }

        result = negative ? -value : value;
        return true;
    }

    [[nodiscard]] bool parseBool(bool& result) noexcept
    {
        const auto previousReadIndex = readIndex;
        if (readString(u8"true")) {
            result = true;
            return true;
        }

        readIndex = previousReadIndex;
        if (readString(u8"false")) {
            result = false;
            return true;
        }

        readIndex = previousReadIndex;
        return false;
    }

    [[nodiscard]] static constexpr bool isWhitespace(char8_t c) noexcept
    {
        switch (c) {
        case u8' ':
        case u8'\t':
        case u8'\n':
        case u8'\r':
            return true;
        default:
            return false;
        }
    }

    [[NOINLINE]] bool skipWhitespaces() noexcept
    {
        while (readIndex < buffer.size() && isWhitespace(buffer[readIndex]))
            ++readIndex;
        return true; 
    }

    [[nodiscard]] bool readChar(char8_t c) noexcept
    {
        if (readIndex < buffer.size() && buffer[readIndex] == c) {
            ++readIndex;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool readString(const char8_t* str) noexcept
    {
        while (*str && readIndex < buffer.size()) {
            if (*str++ != buffer[readIndex++])
                return false;
        }
        return *str == 0;
    }

    std::span<const char8_t> buffer;
    std::size_t readIndex{0};
    ConfigStringConversionState& conversionState;
    bool skipUnknownKeys = false;
    std::array<config_params::ObjectIndexType, config_params::kMaxNestingLevel + 1> indexInNestingLevel{};
    config_params::NestingLevelIndexType nestingLevel{0};
};
