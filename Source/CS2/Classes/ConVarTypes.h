#pragma once

#define CONVAR_STRINGIFY(name) #name
#define CONVAR(name, valueType) \
struct name { \
    static constexpr auto kName{CONVAR_STRINGIFY(name)}; \
    using ValueType = valueType; \
}

namespace cs2
{

CONVAR(mp_teammates_are_enemies, bool);
CONVAR(viewmodel_fov, float);
CONVAR(cl_crosshaircolor, int);
CONVAR(cl_crosshaircolor_r, int);
CONVAR(cl_crosshaircolor_g, int);
CONVAR(cl_crosshaircolor_b, int);

// Air-acceleration constants. The autostrafer needs all three to compute the strafe angle that
// actually gains speed, rather than just picking a side to hold.
CONVAR(sv_air_max_wishspeed, float);
CONVAR(sv_airaccelerate, float);
CONVAR(sv_maxspeed, float);
// Gravity and the normal a surface needs before it counts as standable ground: the pair the
// bunnyhop's landing predictor integrates forward and validates the trace hit with.
CONVAR(sv_gravity, float);
CONVAR(sv_standable_normal, float);
// Ground friction, used to predict where WE will be a few ticks from now. Extrapolating on raw
// velocity overshoots, because a player who has stopped pressing keys is already decelerating.
CONVAR(sv_friction, float);
CONVAR(sv_stopspeed, float);
// Ground acceleration: how fast the wish move converts into velocity per tick. The edgestop's
// counter-strafe needs it to size the stop move so the player halts in as few ticks as possible.
CONVAR(sv_accelerate, float);

}

#undef CONVAR_STRINGIFY
