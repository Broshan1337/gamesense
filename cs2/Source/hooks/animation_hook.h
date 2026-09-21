// ============================================================================
// Hook D — animationsystem.dll ShouldUpdateSequences.
//
// FVA H4 target — identified via workflow w8jdy69re + UnknownCheats
// forum research on knife animation sequences.
//
// Target: animationsystem.dll `sub_18014EFF0` = ShouldUpdateSequences.
//   Prototype (Win64 __fastcall):
//     bool __fastcall ShouldUpdateSequences(
//       void* this,               // rcx — CBaseAnimGraph or similar
//       void* networkedVars,      // rdx — networked vars container
//       void* animGraphContext);  // r8  — anim graph context (reads [r8+0x48])
//
// FVA behavior per UC forum research:
//   Before calling original, clears `*(void**)(networkedVars + 0x30) = nullptr`
//   to force the anim graph subsystem to rebuild the sequence state.
//   Used to sync knife inspect/reload animations after weapon change.
// ============================================================================
#pragma once

namespace fva::hooks
{

bool install_animation_hook();
void uninstall_animation_hook();

} // namespace fva::hooks
