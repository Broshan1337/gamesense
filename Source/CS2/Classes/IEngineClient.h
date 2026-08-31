#pragma once

namespace cs2
{

// The engine's client-side interface (concrete class CEngineClient, living in libengine2.so -
// NOT libclient.so like most interfaces this project resolves). Registered under the classic
// interface-registry name "Source2EngineToClient001"; note libengine2.so also exports
// "Source2EngineToClientStringTable001", which is a different interface - match the exact string.
struct IEngineClient {
    // Runs a string through the engine's client command buffer, exactly as if it had been typed
    // into the console. Reverse-engineered on Linux as sub_4E6FD0 in libengine2.so, found via
    // the "__beginseq" string landmark taken from a Windows decompile of the same function; the
    // two are structurally identical (same `(a2 - 4) > 1` guard, same __beginseq/__endseq
    // bracketing, same "%s\n" command formatting).
    //
    // The extra arguments are not understood in detail and are simply passed the way the
    // (working) Windows caller passes them: (this, 0, command, 1).
    using ExecuteClientCommand = void(IEngineClient* thisptr, int unknown0, const char* command, char unknown1);

    // Vtable slot of ExecuteClientCommand in CEngineClient (_ZTV13CEngineClient @ 0x9ae9a0 in
    // libengine2.so). Derived as (0x9aeb48 - (0x9ae9a0 + 16)) / 8, where 0x9aeb48 is where the
    // function's address is stored, and verified by dumping the whole vtable prefix: all 51
    // preceding entries are real code addresses with no embedded offset-to-top/typeinfo pair,
    // so there is no secondary vtable segment shifting the base.
    static constexpr int kExecuteClientCommandVtableSlot = 51;
};

}
