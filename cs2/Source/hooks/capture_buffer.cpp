#include "capture_buffer.h"

#include <atomic>

namespace fva::capture_buffer
{

valve::pb::raw::CBaseUserCmdPB g_scratch{};

namespace {
std::atomic<unsigned long long> g_generation{0};
}

void publish()
{
    g_generation.fetch_add(1, std::memory_order_release);
}

unsigned long long generation() noexcept
{
    return g_generation.load(std::memory_order_acquire);
}

void cache_vtable_from(void* src) noexcept
{
    if (!src) return;
    if (g_scratch.vtable) return;
    g_scratch.vtable = *reinterpret_cast<void**>(src);
}

} 
