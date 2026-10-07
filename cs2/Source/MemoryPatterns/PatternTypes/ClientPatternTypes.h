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





STRONG_TYPE_ALIAS(GameEventManagerGlobalPointer, cs2::IGameEventManager2*);




STRONG_TYPE_ALIAS(HudChatDelegatePointer, cs2::CHudChatDelegate**);
STRONG_TYPE_ALIAS(ChatPrintFunction, cs2::ChatPrint*);





STRONG_TYPE_ALIAS(ChangeTeammateColorCycle, void(*)());


STRONG_TYPE_ALIAS(CSGOInputPointer, cs2::CCSGOInput*);









STRONG_TYPE_ALIAS(PlayerRankingDataPointer, void*);

STRONG_TYPE_ALIAS(EconSystemAccessor, void*(*)());
STRONG_TYPE_ALIAS(GameAccountClientAccessor, void*(*)(void* sharedObjectCache));

STRONG_TYPE_ALIAS(CreateSubtickMoveStep, void*(*)(void* arena));
STRONG_TYPE_ALIAS(RepeatedPtrFieldAddAllocated, void*(*)(void* field, void* element));










STRONG_TYPE_ALIAS(GetUserCmd, void*(*)(void* controller, int commandNumber));



STRONG_TYPE_ALIAS(AbandonCooldownGate, const void*);
STRONG_TYPE_ALIAS(ManageGlowSceneObjectPointer, cs2::ManageGlowSceneObject*);
STRONG_TYPE_ALIAS(PointerToClientMode, cs2::ClientModeCSNormal*);

#if IS_WIN64()
STRONG_TYPE_ALIAS(SetSceneObjectAttributeFloat4, void(*)(cs2::SceneObjectAttributes::FloatAttributes* attributes, unsigned int attributeNameHash, float value[4]));
#else
STRONG_TYPE_ALIAS(SetSceneObjectAttributeFloat4, void(*)(cs2::SceneObjectAttributes::FloatAttributes* attributes, unsigned int attributeNameHash, double value1, double value2));
#endif
