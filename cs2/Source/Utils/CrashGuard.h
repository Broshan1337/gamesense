#pragma once

// Guarded-call machinery: convert SIGSEGV/SIGBUS faults inside a risky region into a local
// bounce (siglongjmp) instead of a full crash report.
//
// Built for network-transition windows: during map switches/disconnects the
// CNetworkGameClient/CNetChan objects are gutted-but-not-freed in orders that vary (pawn
// destroyed before the channel on one path, after it on another - both observed live), and
// vcalls into their nulled internals fault (libengine2+0x58820d, fault 0x0, twice). Rather
// than guessing every teardown ordering, the lagger wraps the risky region: a fault there
// bounces to a stand-down instead of killing the game.
//
// Integration: the crash logger's handleSignal calls bounceIfArmed() FIRST - armed means
// "this thread expected a possible fault" and the bounce replaces the crash report.
//
// Signal-safety: siglongjmp from a handler is the standard guarded-call pattern; the target
// used sigsetjmp(., 1) so the signal mask is saved/restored. No loop risk: a bounce lands in
// a stand-down path that does not re-enter the guarded region.

#include <cstdint>
#include <csetjmp>

namespace crash_guard
{

inline thread_local sigjmp_buf bouncePoint;
inline thread_local volatile std::uint32_t armed = 0;

// Returns true to run the risky region; returns false after a fault was bounced here.
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

// The crash handler calls this before anything else. Never returns when armed.
inline void bounceIfArmed() noexcept
{
    if (armed) {
        armed = 0;
        siglongjmp(bouncePoint, 1);
    }
}

}
