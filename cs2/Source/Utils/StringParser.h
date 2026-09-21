#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <string_view>

class StringParser {
public:
    explicit StringParser(const char* string) noexcept
        : string{ string }
    {
    }

    [[nodiscard]] std::string_view getLine(char delimiter) noexcept
    {
        const auto begin = string;
        std::size_t length = 0;
        while (*string != '\0' && *string++ != delimiter)
            ++length;
        return { begin, length };
    }

    [[nodiscard]] char getChar() noexcept
    {
        if (*string != '\0')
            return *string++;
        return '\0';
    }

    template <std::integral IntegralType>
    bool parseInt(IntegralType& result) noexcept
    {
        static_assert(std::is_unsigned_v<IntegralType>);

        if (*string < '0' || *string > '9')
            return false;

        constexpr auto maxValue = std::numeric_limits<IntegralType>::max();
        IntegralType parsedInteger{};

        while (*string >= '0' && *string <= '9') {
            const auto digit = static_cast<IntegralType>(*string - '0');
            // Saturate on overflow instead of wrapping: an oversized number (e.g. typed into a text
            // entry) clamps to the type's max, which the caller's own range check then handles.
            //
            // The previous version instead capped the DIGIT COUNT at std::numeric_limits::digits10,
            // which for std::uint8_t is 2 (only 2 decimal digits are guaranteed to fit) - so it
            // silently truncated every value to 2 digits, capping every uint8 slider at 99 even
            // though the type holds 255. That was the "sliders stop at 99" bug.
            if (parsedInteger > static_cast<IntegralType>((maxValue - digit) / 10))
                parsedInteger = maxValue;
            else
                parsedInteger = static_cast<IntegralType>(parsedInteger * 10 + digit);

            ++string;
        }

        result = parsedInteger;
        return true;
    }

    template <std::floating_point FloatType>
    bool parseFloat(FloatType& result) noexcept
    {
        FloatType parsedFloat{};
        bool parseSuccessful = false;

        if (*string == '-') {
            // unsupported
            return false;
        }

        while (*string >= '0' && *string <= '9') {
            parsedFloat *= 10.0f;
            parsedFloat += static_cast<FloatType>(*string - '0');
            parseSuccessful = true;
            ++string;
        }

        if (*string == '.')
            ++string;

        FloatType fraction = 1.0f;
        while (*string >= '0' && *string <= '9') {
            fraction /= 10.0f;
            parsedFloat += static_cast<FloatType>(*string - '0') * fraction;
            parseSuccessful = true;
            ++string;
        }

        if (parseSuccessful)
            result = parsedFloat;

        return parseSuccessful;
    }

private:
    const char* string;
};
