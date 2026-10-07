#pragma once

#include <cstddef>
#include <cstdint>

namespace fva::hooks
{

bool install_vac_hook();
void uninstall_vac_hook();











void apply_deferred_link_hide() noexcept;
bool link_hide_by_substr(const char* substr) noexcept;
void link_restore() noexcept;
bool link_is_hidden() noexcept;




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
