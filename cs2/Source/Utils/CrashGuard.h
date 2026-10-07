#pragma once


















#include <cstdint>
#include <csetjmp>

namespace crash_guard
{

inline thread_local sigjmp_buf bouncePoint;
inline thread_local volatile std::uint32_t armed = 0;


[[nodiscard]] inline bool arm() noexcept
{
    if (sigsetjmp(bouncePoint, 1) != 0) {
        armed = 0;
        return false;
    }
    armed = 1;
    return true;
}

inline void disarm() noexcept
{
    armed = 0;
}


inline void bounceIfArmed() noexcept
{
    if (armed) {
        armed = 0;
        siglongjmp(bouncePoint, 1);
    }
}

}
