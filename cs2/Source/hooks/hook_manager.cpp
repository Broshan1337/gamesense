#include "hook_manager.h"
#include "animation_hook.h"
#include "serialize_to_array_hook.h"
#include "create_move_hook.h"
#include "level_init_hook.h"

#include <cstdio>

namespace fva::hooks
{

bool install_all()
{
    std::puts("[HookManager] Installing all hooks...");
    
    if (!install_level_init_hook()) {
        std::puts("[HookManager] Failed to install level_init_hook");
        return false;
    }
    if (!install_create_move_hook()) {
        std::puts("[HookManager] Failed to install create_move_hook");
        return false;
    }
    if (!install_serialize_to_array_hook()) {
        std::puts("[HookManager] Failed to install serialize_to_array_hook");
        return false;
    }
    
    std::puts("[HookManager] All hooks installed successfully");
    return true;
}

void uninstall_all()
{
    uninstall_animation_hook();
    uninstall_serialize_to_array_hook();
    uninstall_create_move_hook();
    uninstall_level_init_hook();
    std::puts("[HookManager] All hooks uninstalled");
}

} 
