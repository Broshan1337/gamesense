#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace utils
{

// Pack expansion, not linear recursion: the ConfigVariableTypes tuple grew past ~500
// entries and the recursive form blew GCC's default constexpr evaluation depth (512).
template <typename T, typename Tuple>
[[nodiscard]] consteval std::size_t typeIndex() noexcept
{
    constexpr auto find = []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        constexpr bool matches[] = {std::is_same_v<T, std::tuple_element_t<Is, Tuple>>...};
        for (std::size_t i = 0; i < sizeof...(Is); ++i)
            if (matches[i])
                return i;
        return sizeof...(Is);
    };
    constexpr std::size_t index = find(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    static_assert(index < std::tuple_size_v<Tuple>, "T does not exist in Tuple");
    return index;
}

}