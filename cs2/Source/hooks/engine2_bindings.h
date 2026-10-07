







#pragma once

#include <cstdint>

namespace fva::hooks::engine2
{


bool init() noexcept;



bool is_in_game() noexcept;


bool is_connected() noexcept;




bool install_level_shutdown_hook() noexcept;
void uninstall_level_shutdown_hook() noexcept;


void notify_level_init_complete() noexcept;







bool is_stable_gameplay() noexcept;


struct ResolvedRVAs {
    std::uintptr_t get_aspect_ratio;
    std::uintptr_t is_in_game;
    std::uintptr_t is_connected;
    std::uintptr_t run_command;
    std::uintptr_t get_level_name;
    std::uintptr_t get_level_name_short;
    std::uintptr_t connect;
    std::uintptr_t level_shutdown;
};

const ResolvedRVAs& resolved() noexcept;





using ComputeRandomSeedFn = std::uint32_t(__fastcall*)(void*, void*, int);
ComputeRandomSeedFn compute_random_seed() noexcept;



using ForceButtonsDownFn = void(__fastcall*)(void*, void*);
ForceButtonsDownFn force_buttons_down() noexcept;




using GetViewAnglesFn = void*(__fastcall*)(void*, int);
using SetViewAnglesFn = void(__fastcall*)(void*, int, void*);
GetViewAnglesFn get_view_angles() noexcept;
SetViewAnglesFn set_view_angles() noexcept;

} 
