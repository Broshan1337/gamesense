#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

#include "MemoryAllocator.h"
#include "MemoryDeleter.h"

template <typename T>
using UniquePtr = std::unique_ptr<T, MemoryDeleter<T>>;

namespace mem
{

template <typename T>
    requires (!std::is_array_v<T>)
[[nodiscard]] auto makeUniqueForOverwrite() noexcept
{
    // Placement-new on a null pointer is UB - an exhausted pool must yield an EMPTY pointer,
    // not a construction site at address 0 (callers check for emptiness before use).
    if (const auto memory = MemoryAllocator<T>::allocate())
        return UniquePtr<T>{ new (memory) T };
    return UniquePtr<T>{};
}

template <typename T>
    requires std::is_unbounded_array_v<T>
[[nodiscard]] auto makeUniqueForOverwrite(std::size_t size) noexcept
{
    if (const auto memory = MemoryAllocator<T>::allocate(size))
        return UniquePtr<T>{ new (memory) std::remove_extent_t<T>[size], MemoryDeleter<T>{ size } };
    return UniquePtr<T>{};
}

template <typename T, typename... Args>
    requires std::is_bounded_array_v<T>
void makeUniqueForOverwrite(Args&&...) = delete;

}
