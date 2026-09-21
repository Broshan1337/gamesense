#pragma once

#include <cstddef>
#include <cstdint>

namespace fva::hooks
{

bool install_vac_hook();
void uninstall_vac_hook();

// link_map hiding: install_vac_hook unlinks our node (in-process
// enumeration via dl_iterate_phdr / dlopen-NOLOAD-by-name no longer lists
// us); uninstall_vac_hook re-links it before anything else, so the SelfUnload
// dladdr/RTLD_NOLOAD/dlclose chain and any later dlopen see a consistent
// loader view again.
//
// The unlink runs DEFERRED (apply_deferred_link_hide, called from the Vulkan
// present path), never in the constructor: splicing loader structures while
// our own dlopen still holds the loader lock froze the game deterministically
// (silent vanish, no fault/core/log - 2026-09-11). Test seam below.
void apply_deferred_link_hide() noexcept;
bool link_hide_by_substr(const char* substr) noexcept;
void link_restore() noexcept;
bool link_is_hidden() noexcept;

// Observability for the in-game status page: how many maps opens were
// intercepted, how many output lines filtered, how many cheat-file opens
// were served the spoofed clean image.
struct VacHookStats {
    unsigned long maps_opens{0};
    unsigned long lines_filtered{0};
    unsigned long spoofs{0};
};

VacHookStats vac_hook_stats() noexcept;

namespace security::regions
{
void add(void* base, std::size_t size);
void remove(void* base);
bool is_protected(void* addr, std::size_t size);
}

namespace security::prologues
{
void save(std::uintptr_t addr);
}

}
