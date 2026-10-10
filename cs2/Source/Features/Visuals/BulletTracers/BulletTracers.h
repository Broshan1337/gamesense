#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSWeaponBaseVData.h>
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
#include <Utils/NsPaths.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>

// In-game bullet tracers: the local player's bullet_impact events spawn the
// ACTIVE WEAPON's own tracer particle system through the game's fx spawn
// primitive (name + start/end -> effect spec -> particle collection,
// CP0=start CP1=end) - the tracer renders inside the world like any game
// effect, NOT an ImGui overlay.
//
// RE (2026-10-10, live-verified): the spawn primitive's name resolver takes
// FULL VPK PATHS ONLY - the VData/CCSWeaponBaseVData m_szTracerParticle
// short names ("weapon_tracers_assrifle") resolve to nil, the full path
// ("particles/weapons/cs_weapon_fx/weapon_tracers_assrifle.vpcf") resolves.
// The beam also must NOT start exactly at the eye (a beam along the view
// axis is invisible in first person) - it starts kMuzzleOffset units along
// the eye->impact direction, roughly at the muzzle.
// Fail-closed everywhere: no pattern / no eye / no vdata / empty or
// unresolvable name = no-op.
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

        char path[kMaxPathChars];
        if (!buildSystemPath(path, sizeof(path))) {
            if (tracersDebugEnabled())
                VerifyConsole::write(3.0f, "tracer", "path build FAILED (no vdata / no tracer name)");
            return;
        }

        if (tracersDebugEnabled())
            VerifyConsole::write(3.0f, "tracer", "spawn path='%s' start=(%.0f %.0f %.0f) end=(%.0f %.0f %.0f)",
                path, start.x, start.y, start.z, end.x, end.y, end.z);

        // The game's own fx callers pass owner=null, flags=0, attachment=-1,
        // rest=0, extra=(0,0,0) (constants read off the fx_tracer.cpp call site).
        spawn(path, nullptr, 0, -1, 0, 0,
            cs2::ParticlePair{start.x, start.y}, start.z,
            cs2::ParticlePair{end.x, end.y}, end.z,
            cs2::ParticlePair{0.0f, 0.0f}, 0.0f);
    }

    // VData short name ("weapon_tracers_rifle") -> full VPK path
    // ("particles/weapons/cs_weapon_fx/weapon_tracers_rifle.vpcf").
    // Names that already look like paths pass through unchanged.
    [[nodiscard]] bool buildSystemPath(char* out, std::size_t capacity) const noexcept
    {
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        auto&& activeWeapon = localPawn.getActiveWeapon();
        const auto vdata = static_cast<cs2::CCSWeaponBaseVData*>(
            activeWeapon.baseEntity().vData().valueOr(nullptr));
        if (!vdata)
            return false;

        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSWeaponBaseVData", "m_szTracerParticle");
        if (!offset.has_value() || *offset <= 0)
            return false;

        const char* name = nullptr;
        std::memcpy(&name, reinterpret_cast<const std::byte*>(vdata) + *offset, sizeof(name));
        if (!name || !name[0])
            return false;
        for (const char* c = name; *c; ++c)
            if (static_cast<unsigned char>(*c) < 0x20)
                return false;

        int written;
        if (std::strstr(name, "/"))
            written = std::snprintf(out, capacity, "%s%s", name, std::strstr(name, ".vpcf") ? "" : ".vpcf");
        else
            written = std::snprintf(out, capacity, "particles/weapons/cs_weapon_fx/%s.vpcf", name);
        return written > 0 && static_cast<std::size_t>(written) < capacity;
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
    static constexpr std::size_t kMaxPathChars = 128;

    HookContext& hookContext;
    inline static float lastSpawnTick{-1.0e8f};
};