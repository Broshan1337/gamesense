#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

template <std::size_t BufferSize = 0, std::size_t NumberOfStrings = 0, typename... Types>
class TypedStaticStringPool {
public:
    using TypeList = std::tuple<Types...>;

    template <typename T, std::uint8_t N>
    [[nodiscard]] consteval auto add(const char(&str)[N]) const noexcept
    {
        if (str[N - 1] != '\0')
            error("string must be null-terminated");

        TypedStaticStringPool<BufferSize + N, NumberOfStrings + 1, Types..., T> newPool;
        // std::copy (not std::ranges::copy): libc++ 15's ranges support is incomplete and
        // this must compile on BOTH toolchains (GCC/clang22 production + clang15/17 OLLVM).
        std::copy(buffer.begin(), buffer.end(), newPool.buffer.begin());
        std::copy(str, str + N, newPool.buffer.begin() + buffer.size());
        std::copy(stringLengths.begin(), stringLengths.end(), newPool.stringLengths.begin());
        newPool.stringLengths.back() = N;
        return newPool;
    }

    template <typename F>
    void forEach(F f) const noexcept
    {
        std::size_t index = 0;
        for (const auto stringLength : stringLengths) {
            const auto str = buffer.data() + index;
            f(str);
            index += stringLength;
        }
    }

private:
    static void error(const char*);

    template <std::size_t, std::size_t, typename...>
    friend class TypedStaticStringPool;

    std::array<char, BufferSize> buffer{};
    std::array<std::uint8_t, NumberOfStrings> stringLengths{};
};
