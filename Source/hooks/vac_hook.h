#pragma once

#include <cstddef>
#include <cstdint>

namespace fva::hooks
{

bool install_vac_hook();
void uninstall_vac_hook();

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
