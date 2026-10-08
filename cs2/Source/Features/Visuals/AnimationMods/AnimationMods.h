#pragma once
#include "AnimationModsConfigVariables.h"
#include "ConVarOverride.h"
#include <GameClient/ConVars/CvarSystem.h>
#include <HookContext/HookContextMacros.h>

template<typename HookContext> class AnimationMods {
public:
    explicit AnimationMods(HookContext& context) noexcept : hookContext{context} {}
    void run() const noexcept {
        boolean(skipAnimations, "cl_skip_update_animations", GET_CONFIG_VAR(animation_mod_vars::Freeze), true);
        boolean(disableAnimations, "anim_disable", GET_CONFIG_VAR(animation_mod_vars::Freeze), true);
        boolean(footIK, "animgraph_footlock_ik_enable", GET_CONFIG_VAR(animation_mod_vars::DisableIK), false);
        boolean(ragdolls, "cl_disable_ragdolls", GET_CONFIG_VAR(animation_mod_vars::DisableRagdolls), true);
        floating(ragdollScale, "cl_ragdoll_default_scale", GET_CONFIG_VAR(animation_mod_vars::ModifyRagdollScale),
            static_cast<float>(GET_CONFIG_VAR(animation_mod_vars::RagdollScale)));
    }
    void onUnload() const noexcept {
        boolean(skipAnimations, "cl_skip_update_animations", false, false);
        boolean(disableAnimations, "anim_disable", false, false);
        boolean(footIK, "animgraph_footlock_ik_enable", false, false);
        boolean(ragdolls, "cl_disable_ragdolls", false, false);
        floating(ragdollScale, "cl_ragdoll_default_scale", false, 0);
    }
private:
    void boolean(convar_override::State<bool>& state, const char* name, bool enabled, bool value) const noexcept {
        auto cvars = hookContext.template make<CvarSystem>();
        state.update(enabled, value, [&] { return cvars.readBoolConVar(name); },
            [&](bool v) { return cvars.forceBoolConVar(name, v); });
    }
    void floating(convar_override::State<float>& state, const char* name, bool enabled, float value) const noexcept {
        auto cvars = hookContext.template make<CvarSystem>();
        state.update(enabled, value, [&] { return cvars.readFloatConVar(name); },
            [&](float v) { return cvars.forceFloatConVar(name, v); });
    }
    inline static convar_override::State<bool> skipAnimations, disableAnimations, footIK, ragdolls;
    inline static convar_override::State<float> ragdollScale;
    HookContext& hookContext;
};
