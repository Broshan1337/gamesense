#include "game_state_resolver.h"
#include "integrity_audit.h"

#include <Platform/Linux/LinuxDynamicLibrary.h>

#include <cstdio>

namespace fva::game_state
{

namespace
{

bool g_initialized = false;

}

void force_reresolve()
{
    if (!g_initialized)
        return;
        
    std::puts("[GameState] Force re-resolve triggered");
    security::integrity::shutdown();
    security::integrity::initialize();
}

bool initialize()
{
    if (g_initialized)
        return true;
        
    std::puts("[GameState] Initializing...");
    
    if (!security::integrity::initialize())
    {
        std::puts("[GameState] Failed to initialize integrity system");
        return false;
    }
    
    g_initialized = true;
    std::puts("[GameState] Initialized successfully");
    return true;
}

void shutdown()
{
    if (!g_initialized)
        return;
        
    security::integrity::shutdown();
    g_initialized = false;
    std::puts("[GameState] Shutdown complete");
}

} 