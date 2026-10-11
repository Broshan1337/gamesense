#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/CParticleSystemMgr.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/BulletTracers/BulletTracersConfigVariables.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/ColorUtils.h>
#include <Utils/NsPaths.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>

// In-game bullet tracers (velocity-cs2 impacts.cpp method): the local
// player's bullet_impact events spawn the universal EMBEDDED tracer system
// ("particles/embedded/tracer.vpcf") through the game's fx spawn primitive,
// with CP0 = beam start, CP1 = impact and CP2 = COLOR (velocity's
// play_bullet_tracer: CP2 = RGB floats, CP3 = duration - our primitive takes
// CP0/CP1/CP2, so the color rides the extra pair; duration stays default).
//
// 2026-10-12 FIX: the old per-weapon path read
// CCSWeaponBaseVData/m_szTracerParticle - that field is a
// CResourceNameTyped<CWeakHandle<IParticleSystemDefinition>>, NOT a char*;
// the read yielded nil -> FindParticleSystem resolved nothing -> the
// spawn silently no-op'd (tracers never rendered). The embedded tracer.vpcf
// is always shipped and precached by the particle manager, so no per-weapon
// resolution happens at all.
//
// The beam must NOT start exactly at the eye (a beam along the view axis is
// invisible in first person) - it starts kMuzzleOffset units along the
// eye->impact direction, roughly at the muzzle.
// Fail-closed everywhere: no pattern / no eye / no config = no-op.
//
// Debug: touch <osirisDir>/osiris_tracers_debug -> throttled breadcrumbs on
// every live round (which gate failed / what was spawned) in the gui log.
template <typename HookContext>
class BulletTracers {
public:
    explicit BulletTracers(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!GET_CONFIG_VAR(BulletTracersEnabled))
            return;
        if (!event || !game_events::is(event, "bullet_impact"))
            return;
        if (!game_events::localPlayerIsSlot(hookContext, game_events::entityForKey(event, "userid")))
            return;

        // Session gate (map-transition rule): never touch game internals
        // while the entity list / pawn chain is being rebuilt mid-load.
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            if (tracersDebugEnabled())
                VerifyConsole::write(3.0f, "tracer", "no session (pawn=%d curtime=%.2f)", localPawn ? 1 : 0, curtime.valueOr(0.0f));
            return;
        }

        // One tracer per tick: shotguns and wallbangs fire several
        // bullet_impact events in the same tick.
        if (curtime.value() == lastSpawnTick)
            return;

        const auto spawn = hookContext.patternSearchResults().template get<PointerToSpawnParticleEffect>();
        if (!spawn) {
            if (tracersDebugEnabled())
                VerifyConsole::write(3.0f, "tracer", "spawn pattern MISSING");
            return;
        }

        const auto eye = localPawn.eyePosition();
        if (!eye.hasValue()) {
            if (tracersDebugEnabled())
                VerifyConsole::write(3.0f, "tracer", "no eye position");
            return;
        }

        const cs2::Vector impact{
            game_events::floatForKey(event, "x"),
            game_events::floatForKey(event, "y"),
            game_events::floatForKey(event, "z"),
        };

        // Muzzle-ish start: kMuzzleOffset units along the shot direction,
        // so the beam is visible in first person instead of lying exactly on
        // the view axis.
        const float dx = impact.x - eye.value().x;
        const float dy = impact.y - eye.value().y;
        const float dz = impact.z - eye.value().z;
        const float len = trig::squareRoot(dx * dx + dy * dy + dz * dz);
        if (len < 1.0f) {
            if (tracersDebugEnabled())
                VerifyConsole::write(3.0f, "tracer", "zero-length shot");
            return;
        }
        const cs2::Vector start{
            eye.value().x + dx / len * kMuzzleOffset,
            eye.value().y + dy / len * kMuzzleOffset,
            eye.value().z + dz / len * kMuzzleOffset,
        };

        lastSpawnTick = curtime.value();
        spawnTracer(start, impact);
    }

private:
    void spawnTracer(const cs2::Vector& start, const cs2::Vector& end) const noexcept
    {
        const auto spawn = hookContext.patternSearchResults().template get<PointerToSpawnParticleEffect>();
        if (!spawn)
            return;

        // velocity's universal embedded tracer system (always shipped):
        // CP0 = beam start, CP1 = impact, CP2 = color RGB floats.
        constexpr const char* kTracerSystemPath = "particles/embedded/tracer.vpcf";

        if (tracersDebugEnabled())
            VerifyConsole::write(3.0f, "tracer", "spawn path='%s' start=(%.0f %.0f %.0f) end=(%.0f %.0f %.0f)",
                kTracerSystemPath, start.x, start.y, start.z, end.x, end.y, end.z);

        // The game's own fx callers pass owner=null, flags=0, attachment=-1,
        // rest=0 (constants read off the fx_tracer.cpp call site). CP2 =
        // the configured tracer color, velocity-style (RGB floats).
        const auto color = GET_CONFIG_VAR(BulletTracersColor);
        spawn(kTracerSystemPath, nullptr, 0, -1, 0, 0,
            cs2::ParticlePair{start.x, start.y}, start.z,
            cs2::ParticlePair{end.x, end.y}, end.z,
            cs2::ParticlePair{static_cast<float>(color.r()), static_cast<float>(color.g())},
            static_cast<float>(color.b()));
    }

    [[nodiscard]] static bool tracersDebugEnabled() noexcept
    {
        char debugFile[ns_paths::kMaxPath];
        if (!ns_paths::join(debugFile, sizeof(debugFile), "osiris_tracers_debug"))
            return false;
        const int fd = LinuxPlatformApi::open(debugFile, 0);
        if (fd >= 0) {
            LinuxPlatformApi::close(fd);
            return true;
        }
        return false;
    }

    static constexpr float kMuzzleOffset = 32.0f;

    HookContext& hookContext;
    inline static float lastSpawnTick{-1.0e8f};
};