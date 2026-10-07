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



CONVAR(sv_air_max_wishspeed, float);
CONVAR(sv_airaccelerate, float);
CONVAR(sv_maxspeed, float);


CONVAR(sv_gravity, float);
CONVAR(sv_standable_normal, float);


CONVAR(sv_friction, float);
CONVAR(sv_stopspeed, float);


CONVAR(sv_accelerate, float);

}

#undef CONVAR_STRINGIFY
