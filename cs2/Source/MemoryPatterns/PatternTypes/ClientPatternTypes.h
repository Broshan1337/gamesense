#pragma once

#include <CS2/Classes/GlobalVars.h>
#include <CS2/Classes/ClientModeCSNormal.h>
#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CHudChatDelegate.h>
#include <CS2/Classes/CSceneObject.h>
#include <CS2/Classes/CViewRender.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <CS2/Classes/Glow.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Classes/VMatrix.h>
#include <CS2/Panorama/CPanel2D.h>
#include <Platform/Macros/IsPlatform.h>
#include <Utils/StrongTypeAlias.h>

STRONG_TYPE_ALIAS(MainMenuPanelPointer, cs2::CPanel2D**);
STRONG_TYPE_ALIAS(HudPanelPointer, cs2::CPanel2D**);
STRONG_TYPE_ALIAS(GlobalVarsPointer, cs2::GlobalVars**);
STRONG_TYPE_ALIAS(TransformTranslate3dVMT, const void*);
STRONG_TYPE_ALIAS(TransformScale3dVMT, const void*);
STRONG_TYPE_ALIAS(WorldToProjectionMatrixPointer, cs2::VMatrix*);
STRONG_TYPE_ALIAS(ViewToProjectionMatrixPointer, cs2::VMatrix*);
STRONG_TYPE_ALIAS(ViewRenderPointer, cs2::CViewRender**);
STRONG_TYPE_ALIAS(LocalPlayerControllerPointer, cs2::CCSPlayerController**);
// The client's own g_pGameEventManager global. NOT obtainable via CreateInterface: the client
// registers "GAMEEVENTSMANAGER002" into a separate runtime registry instead of adding it to
// libclient.so's CreateInterface list, so that lookup returns null - see project notes.
STRONG_TYPE_ALIAS(GameEventManagerGlobalPointer, cs2::IGameEventManager2**);
// The chat message list, and the game's own function for appending a real entry to it. Both are
// resolved by pattern rather than through any interface: the delegate is a plain module global
// (libclient.so qword_48AA498) and the printer is a free function (sub_1FFDF30), neither of which
// is exposed through CreateInterface or a vtable we already hold.
STRONG_TYPE_ALIAS(HudChatDelegatePointer, cs2::CHudChatDelegate**);
STRONG_TYPE_ALIAS(ChatPrintFunction, cs2::ChatPrint*);
// The C++ handler of the game's "ChangeTeammateColor" action (the "Change Preferred Color"
// dialog path): reads cl_color, SetValue((cur+1)%5), then force-notifies the GC party
// member-data sender. It is NOT a real ConCommand - it is registered into the Panorama/JS
// binding tables (which is why executing the name through the console prints
// "Unknown command"), so the only way to run the mechanism is this patterned direct call.
STRONG_TYPE_ALIAS(ChangeTeammateColorCycle, void(*)());
// The CCSGOInput singleton. This is the OBJECT's address, not a pointer to it - the client
// constructs it in place at a fixed address inside libclient.so.
STRONG_TYPE_ALIAS(CSGOInputPointer, cs2::CCSGOInput*);

// The two halves of appending a real CSubtickMoveStep, both the game's own functions rather than
// anything reimplemented: one arena-allocates a zeroed step, the other is protobuf's
// RepeatedPtrFieldBase::AddAllocated. Calling these is what makes a synthesised subtick move
// indistinguishable from one the input system produced.
// The two accessors that reach the local account's econ state. Both resolved from ONE call site,
// which is the only place in the module that pairs them - see ClientPatternsLinux.h for why that
// matters.
// The local player's ranking block (level, XP and the flags that gate them).
STRONG_TYPE_ALIAS(PlayerRankingDataPointer, void*);

STRONG_TYPE_ALIAS(EconSystemAccessor, void*(*)());
STRONG_TYPE_ALIAS(GameAccountClientAccessor, void*(*)(void* sharedObjectCache));

STRONG_TYPE_ALIAS(CreateSubtickMoveStep, void*(*)(void* arena));
STRONG_TYPE_ALIAS(RepeatedPtrFieldAddAllocated, void*(*)(void* field, void* element));

// The command ring's getter: `get_usercmd(controller, commandNumber)`. The first argument is
// UNUSED in this build - the function reaches the command manager through the global off_455BC98
// itself - but it is kept in the signature to match the game's own.
//
// Body (sub_15DAB20): `v4 = <ring base>; if (n <= 0) return 0; v5 = 152 * (n % 150) + v4;
// return n == *(int*)(v5 + 8) ? v5 : 0;` - a 150-entry ring of 152-byte (0x98) commands, keyed by
// command number, with the number stored at +8 of each entry and checked before it is handed back.
// 0x98 is exactly sizeof(UserCmd) in the internal reference base, which is a nice independent
// confirmation that this really is the command array.
STRONG_TYPE_ALIAS(GetUserCmd, void*(*)(void* controller, int commandNumber));

// The SteamID comparison gating the matchmaking-cooldown notice - points at that branch's two
// opcode bytes, not at the enclosing function. See CooldownRevealer.
STRONG_TYPE_ALIAS(AbandonCooldownGate, const void*);
STRONG_TYPE_ALIAS(ManageGlowSceneObjectPointer, cs2::ManageGlowSceneObject*);
STRONG_TYPE_ALIAS(PointerToClientMode, cs2::ClientModeCSNormal*);

#if IS_WIN64()
STRONG_TYPE_ALIAS(SetSceneObjectAttributeFloat4, void(*)(cs2::SceneObjectAttributes::FloatAttributes* attributes, unsigned int attributeNameHash, float value[4]));
#else
STRONG_TYPE_ALIAS(SetSceneObjectAttributeFloat4, void(*)(cs2::SceneObjectAttributes::FloatAttributes* attributes, unsigned int attributeNameHash, double value1, double value2));
#endif
