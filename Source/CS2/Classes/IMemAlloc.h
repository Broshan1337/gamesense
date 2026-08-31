#pragma once

#include <cstddef>

namespace cs2
{

struct IMemAlloc {
    using Alloc = void*(IMemAlloc* thisptr, std::size_t size);
    using Free = void(IMemAlloc* thisptr, void* memory);

    const void* vmt;
};

}
