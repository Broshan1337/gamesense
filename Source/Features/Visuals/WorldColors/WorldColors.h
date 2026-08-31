#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_GradientFog.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Config/ConfigVariable.h>
#include <Features/Visuals/WorldColors/WorldColorsConfigVariables.h>
#include <Features/Visuals/WorldColors/WorldColorsState.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/ColorUtils.h>
#include <Utils/VerifyConsole.h>

// FrameworkCS2 port (Source/Features/Visuals/WorldColors): recolors molotov/incendiary fire
// particles, scene lights and the sky. Every dereference below is guarded and fails closed per
// element - a stale offset must cost a missing recolor, never a crash.
//
// Sky on this build: the "SkyTint" attribute string became its murmur2 hash 0x99E6002A, and the
// SkyBox DrawArray (libscenesystem 0x335760) copies the tint INTO the render attribute list from
// scene_animatable_object +0xD8/+0xDC/+0xE0 (r/g/b; alpha is forced to 0 by the game, it is not
// read anymore). FrameworkCS2's +0x120 {r,g,b,a} is the same field pair on their older build.
template <typename HookContext>
class WorldColors {
public:
    explicit WorldColors(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;
        if (!game_events::is(event, "game_newmap"))
            return;
        // New map - the cached light objects and fog entities are gone, dump the caches.
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;
        state.cachedLightCount = 0;
        state.cachedFogCount = 0;
    }

    // Called from the hooked CParticleObjectDesc::DrawArray BEFORE the original runs, so the
    // game consumes the modified primitives itself.
    void recolorParticles(void* primitives, int primitiveCount) const noexcept
    {
        if (!primitives || primitiveCount <= 0)
            return;

        // Debug aid (SpectatorList's /tmp-file convention): `touch /tmp/osiris_particles_debug`
        // dumps the distinct particle system names seen here into the engine log, so fire
        // recoloring can be matched against the REAL system names (the molotov core flame kept
        // its color while the edges changed - name mismatch suspected). Remove the file to stop.
        if (particlesDebugEnabled())
            dumpParticleNames(primitives, primitiveCount);

        if (!GET_CONFIG_VAR(WorldColorsInfernoEnabled))
            return;

        const auto molotovColor = GET_CONFIG_VAR(MolotovColor);
        const auto incendiaryColor = GET_CONFIG_VAR(IncendiaryColor);

        auto* prim = static_cast<const std::byte*>(primitives);
        for (int i = 0; i < primitiveCount; ++i, prim += sizeof(ParticleDrawPrimitive)) {
            const void* collection = readPointer(prim + kParticleCollectionOffset);
            if (!isPlausibleObjectPointer(collection))
                continue;
            const void* definition = readPointer(static_cast<const std::byte*>(collection) + kParticleSystemDefinitionOffset);
            if (!isPlausibleObjectPointer(definition))
                continue;
            const char* const* namePointer = static_cast<const char* const*>(readPointer(static_cast<const std::byte*>(definition) + kParticleSystemNameOffset));
            if (!namePointer)
                continue;
            const char* name = *namePointer;
            if (!name)
                continue;

            if (std::strstr(name, "molotov"))
                writeColor(prim, molotovColor);
            else if (std::strstr(name, "incendiary"))
                writeColor(prim, incendiaryColor);
        }
    }

    // Called from the hooked CLightBinnerGPU::ProcessLights before the original runs. Original
    // colors are cached once per light object; disabling the feature restores them (run()).
    void recolorLight(void* sceneLightObject) const noexcept
    {
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;
        if (!GET_CONFIG_VAR(WorldColorsLightsEnabled)) {
            restoreLightColors(state);
            return;
        }
        if (!sceneLightObject)
            return;

        auto* light = static_cast<std::byte*>(sceneLightObject);
        cacheOriginalLight(state, sceneLightObject, light + kLightColorOffset);

        const auto color = GET_CONFIG_VAR(WorldColorsLightColor);
        // Lights cannot be transparent, so alpha doubles as an intensity multiplier
        // (the reference's exact trick).
        const float scale = static_cast<float>(color.a()) / 255.0f;
        writeLightChannel(light + kLightColorOffset, static_cast<float>(color.r()) * scale / 255.0f);
        writeLightChannel(light + kLightColorOffset + 4, static_cast<float>(color.g()) * scale / 255.0f);
        writeLightChannel(light + kLightColorOffset + 8, static_cast<float>(color.b()) * scale / 255.0f);
    }

    // Per-frame tick: restores cached light colors once the feature is disabled, drives the
    // gradient-fog override, and forces the post-process bloom strength while Sky Bloom is on.
    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(WorldColorsLightsEnabled))
            restoreLightColors(hookContext.featuresStates().visualFeaturesStates.worldColorsState);
        if (!GET_CONFIG_VAR(WorldColorsFogEnabled))
            restoreFogs();
        else
            updateFog();
        updateBloom();
    }

    // Sky Bloom: scale the game's own post-process bloom (r_csgo_render_post_bloom_strength).
    //
    // TWO write paths, because the first iteration (memory write alone) showed NO visual effect:
    //   1. the console path (EngineCommandExecutor) - the command goes through the engine's
    //      command buffer exactly as if typed, so any callback the post pipeline hooks to pick
    //      the value up fires; this is what working reference clients do,
    //   2. the direct memory write (forceFloatConVar) - belt-and-braces in case the cvar turns
    //      out to be memory-read-per-frame.
    // Queued on CHANGE only, plus a throttled self-heal: every ~2s while enabled the live cvar
    // is read back and the command re-queued if something (map load, another writer) moved it.
    // The game's original value is cached on first enable, restored once on disable. [bloom]
    // diagnostics log each queue.
    void updateBloom() const noexcept
    {
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;
        const bool enabled = GET_CONFIG_VAR(WorldColorsBloomEnabled);
        const float desired = enabled
            ? static_cast<float>(GET_CONFIG_VAR(WorldColorsBloomStrength)) * 0.05f
            : 0.0f;

        if (!enabled) {
            if (state.bloomWasEnabled) {
                if (state.bloomOriginalValid)
                    queueBloom(state.bloomOriginal, state);
                else
                    VerifyConsole::write(10.0f, "bloom", "disable skipped - no cached original");
            }
            state.bloomWasEnabled = false;
            return;
        }

        if (!state.bloomOriginalValid) {
            if (const auto original = cvarSystem().readFloatConVar(kBloomCvarName); original.has_value()) {
                state.bloomOriginal = original.value();
                state.bloomOriginalValid = true;
                VerifyConsole::write(30.0f, "bloom", "cvar found, original=%.3f", state.bloomOriginal);
            } else {
                VerifyConsole::write(30.0f, "bloom", "cvar NOT found/type mismatch - memory path inactive");
            }
        }

        const bool valueChanged = !state.bloomQueuedValid || state.lastQueued != desired;
        bool drifted = false;
        if (monotonicSeconds() - state.lastQueueTime > 2.0) {
            if (const auto live = cvarSystem().readFloatConVar(kBloomCvarName); live.has_value()
                && (live.value() < desired - 0.001f || live.value() > desired + 0.001f))
                drifted = true;
        }

        if (valueChanged || drifted || !state.bloomWasEnabled)
            queueBloom(desired, state);
        state.bloomWasEnabled = true;
    }

    // World-geometry recolor, DrawArray pass-through style (hooked CBaseSceneObjectDesc /
    // CAggregateSceneObjectDesc slot 1): overwrites the per-primitive color u32 BEFORE the
    // original consumes the array. Same primitive layout as particles/sky (stride 0x70,
    // color @0x50 - verified in the live draw path 0x40E240).
    //
    // The prim color alone only tints materials that multiply albedo by it (trims/corners).
    // Big lit surfaces take their tint from the per-object LIGHT data: the game's own draw
    // path copies sceneObject+0x50/+0x54 (scene object = prim+0x18) into the per-frame light
    // entries (verified at 0x40E300-0x40E326: entry+4/+8 = [obj+0x50]/[obj+0x54], gated by
    // the frame counter at obj+0x58). Writing the color there too is what tints the walls.
    void recolorWorld(void* primitives, int primitiveCount) const noexcept
    {
        if (!GET_CONFIG_VAR(WorldColorsWorldEnabled) || !primitives || primitiveCount <= 0)
            return;

        const auto value = static_cast<std::uint32_t>(GET_CONFIG_VAR(WorldColorsWorldColor));
        auto* prim = static_cast<std::byte*>(primitives);
        for (int i = 0; i < primitiveCount; ++i, prim += kSkyPrimitiveStride) {
            std::memcpy(prim + kParticleColorOffset, &value, sizeof(value));

            auto* object = static_cast<std::byte*>(readPointer(prim + kSkyAnimatableObjectOffset));
            if (!isPlausibleObjectPointer(object))
                continue;
            std::memcpy(object + kSceneObjectLightColorOffset, &value, sizeof(value));
            std::memcpy(object + kSceneObjectLightColorOffset + 4, &value, sizeof(value));
        }
    }

    // Sky recolor, DrawArray pass-through style: the hook saves the original tint floats for
    // every sky object it is about to draw, we overwrite them, the original runs, then the hook
    // restores. recolorSky() fills the save buffer and returns how many entries it touched;
    // restoreSky() puts the originals back. Both fail closed per element.
    static constexpr auto kSkyTintOffset = 0xD8; // r/g/b floats on the sky scene object

    // The post-process bloom strength knob the Sky Bloom feature forces (the game's only
    // user-facing bloom amount control on this build - verified present in libclient strings).
    static constexpr const char* kBloomCvarName = "r_csgo_render_post_bloom_strength";


    struct SavedSkyTint {
        void* object;
        float r, g, b;
    };

    [[nodiscard]] int recolorSky(SavedSkyTint* saved, int maxSaved, void* primitives, int primitiveCount) const noexcept
    {
        if (!GET_CONFIG_VAR(WorldColorsSkyEnabled) || !primitives || primitiveCount <= 0)
            return 0;

        const auto color = GET_CONFIG_VAR(WorldColorsSkyColor);
        const float channels[3] = {
            static_cast<float>(color.r()) / 255.0f,
            static_cast<float>(color.g()) / 255.0f,
            static_cast<float>(color.b()) / 255.0f,
        };

        int savedCount = 0;
        auto* prim = static_cast<std::byte*>(primitives);
        for (int i = 0; i < primitiveCount; ++i, prim += kSkyPrimitiveStride) {
            void* object = readPointer(prim + kSkyAnimatableObjectOffset);
            if (!isPlausibleObjectPointer(object) || savedCount >= maxSaved)
                continue;
            auto* tint = static_cast<std::byte*>(object) + kSkyTintOffset;
            float original[3]{};
            std::memcpy(original, tint, sizeof(original));
            saved[savedCount++] = SavedSkyTint{object, original[0], original[1], original[2]};
            std::memcpy(tint, channels, sizeof(channels));
        }
        return savedCount;
    }

    static void restoreSky(const SavedSkyTint* saved, int savedCount) noexcept
    {
        for (int i = 0; i < savedCount; ++i) {
            auto* tint = static_cast<std::byte*>(saved[i].object) + kSkyTintOffset;
            const float channels[3] = {saved[i].r, saved[i].g, saved[i].b};
            std::memcpy(tint, channels, sizeof(channels));
        }
    }

private:
    [[nodiscard]] CvarSystem<HookContext> cvarSystem() const noexcept
    {
        return hookContext.template make<CvarSystem>();
    }

    // Queues the console command AND writes the value through memory (whichever path the
    // renderer honors, we are covered), logs both, and records the queued value for the
    // change/self-heal detection above.
    void queueBloom(float value, auto& state) const noexcept
    {
        char command[64];
        std::snprintf(command, sizeof(command), "%s %.3f", kBloomCvarName, value);
        hookContext.template make<EngineCommandExecutor>().execute(command);
        static_cast<void>(cvarSystem().forceFloatConVar(kBloomCvarName, value));

        VerifyConsole::write(5.0f, "bloom", "queued '%s'", command);
        state.lastQueued = value;
        state.bloomQueuedValid = true;
        state.lastQueueTime = monotonicSeconds();
    }

    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
    }
    // Fog override (velocity-cs2 port, night-mode atmosphere): every map-placed fog entity is
    // rewritten each frame while enabled; the originals are cached once and restored on
    // disable / map change. All offsets come from the runtime schema and every field is
    // optional - a missing offset only means that field keeps its map value.
    //
    // TWO entity types (live-measured 2026-08-30): C_GradientFog - the game's fog controller
    // rewrites start/maxOpacity/color/heightFog back to map values every frame, but endDist +
    // enabled PERSIST; and C_EnvCubemapFog - several maps (incl. the user's test map) drive
    // their ACTUAL atmosphere with cubemap fog while the gradient-fog entities sit dormant.
    // Visible fog strength = end distance: close to start = dense, far away = haze.
    void updateFog() const noexcept
    {
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;

        // C_GradientFog fields
        const auto startDistanceOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogStartDistance");
        const auto endDistanceOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogEndDistance");
        const auto heightFogOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_bHeightFogEnabled");
        const auto maxOpacityOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogMaxOpacity");
        const auto colorOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_fogColor");
        const auto strengthOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogStrength");
        const auto startDisabledOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_bStartDisabled");
        const auto enabledOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_bIsEnabled");
        // C_EnvCubemapFog fields
        const auto cubeStartOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_flStartDistance");
        const auto cubeEndOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_flEndDistance");
        const auto cubeMaxOpacityOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_flFogMaxOpacity");
        const auto cubeActiveOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_bActive");
        const auto cubeStartDisabledOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_bStartDisabled");
        if (!endDistanceOffset.has_value() || !enabledOffset.has_value() || !cubeEndOffset.has_value() || !cubeActiveOffset.has_value())
            return;

        const auto fogColor = static_cast<std::uint32_t>(GET_CONFIG_VAR(WorldColorsFogColor));
        const auto density = static_cast<float>(GET_CONFIG_VAR(WorldColorsFogDensity)) / 100.0f;
        const float fogDistance = static_cast<float>(GET_CONFIG_VAR(WorldColorsFogDistance));

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (state.cachedFogCount >= WorldColorsState::kMaxCachedFogs)
                return;
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            const bool gradientFog = entityTypeInfo.template is<cs2::C_GradientFog>();
            const bool cubemapFog = entityTypeInfo.template is<cs2::C_EnvCubemapFog>();
            if (!gradientFog && !cubemapFog)
                return;

            auto* const entity = reinterpret_cast<std::byte*>(entityIdentity.entity);
            if (!entity)
                return;

            // Cache originals once per fog entity (linear scan, the cache holds at most 8).
            bool alreadyCached = false;
            for (int i = 0; i < state.cachedFogCount; ++i) {
                if (state.cachedFogs[i].fogEntity == entity)
                    alreadyCached = true;
            }
            if (!alreadyCached) {
                auto& cached = state.cachedFogs[state.cachedFogCount];
                cached.fogEntity = entity;
                cached.isCubemap = cubemapFog;
                if (cubemapFog) {
                    readFogField(entity, cubeStartOffset, cached.startDistance);
                    readFogField(entity, cubeEndOffset, cached.endDistance);
                    readFogField(entity, cubeMaxOpacityOffset, cached.maxOpacity);
                    cached.isEnabled = readFogBool(entity, cubeActiveOffset);
                    cached.startDisabled = readFogBool(entity, cubeStartDisabledOffset);
                } else {
                    readFogField(entity, startDistanceOffset, cached.startDistance);
                    readFogField(entity, endDistanceOffset, cached.endDistance);
                    readFogField(entity, maxOpacityOffset, cached.maxOpacity);
                    readFogField(entity, strengthOffset, cached.strength);
                    cached.color = 0;
                    if (colorOffset.has_value())
                        std::memcpy(&cached.color, entity + *colorOffset, sizeof(cached.color));
                    cached.heightFogEnabled = readFogBool(entity, heightFogOffset);
                    cached.startDisabled = readFogBool(entity, startDisabledOffset);
                    cached.isEnabled = readFogBool(entity, enabledOffset);
                }
                ++state.cachedFogCount;
            }

            if (cubemapFog) {
                // No per-frame reset observed on these; end distance is the strength lever
                // (map starts here are 50..800).
                writeFogField(entity, cubeStartOffset, 0.0f);
                writeFogField(entity, cubeEndOffset, fogDistance);
                writeFogField(entity, cubeMaxOpacityOffset, density);
                writeFogBool(entity, cubeActiveOffset, true);
                writeFogBool(entity, cubeStartDisabledOffset, false);
            } else {
                // Fields the fog controller lets us keep: end distance (the actual strength
                // lever) and enabled. The rest are written best-effort - the controller
                // overwrites them with map values every frame.
                writeFogField(entity, endDistanceOffset, fogDistance);
                writeFogBool(entity, enabledOffset, true);
                writeFogField(entity, startDistanceOffset, 0.0f);
                writeFogField(entity, maxOpacityOffset, density);
                writeFogField(entity, strengthOffset, 1.0f);
                if (colorOffset.has_value())
                    std::memcpy(entity + *colorOffset, &fogColor, sizeof(fogColor));
                writeFogBool(entity, heightFogOffset, false);
                writeFogBool(entity, startDisabledOffset, false);
            }
        });
    }

    void restoreFogs() const noexcept
    {
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;
        if (state.cachedFogCount == 0)
            return;

        const auto startDistanceOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogStartDistance");
        const auto endDistanceOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogEndDistance");
        const auto heightFogOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_bHeightFogEnabled");
        const auto maxOpacityOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogMaxOpacity");
        const auto colorOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_fogColor");
        const auto strengthOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_flFogStrength");
        const auto startDisabledOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_bStartDisabled");
        const auto enabledOffset = hookContext.schemaSystem().getFieldOffset("C_GradientFog", "m_bIsEnabled");
        const auto cubeStartOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_flStartDistance");
        const auto cubeEndOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_flEndDistance");
        const auto cubeMaxOpacityOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_flFogMaxOpacity");
        const auto cubeActiveOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_bActive");
        const auto cubeStartDisabledOffset = hookContext.schemaSystem().getFieldOffset("C_EnvCubemapFog", "m_bStartDisabled");

        for (int i = 0; i < state.cachedFogCount; ++i) {
            const auto& cached = state.cachedFogs[i];
            auto* const entity = reinterpret_cast<std::byte*>(cached.fogEntity);
            if (cached.isCubemap) {
                writeFogField(entity, cubeStartOffset, cached.startDistance);
                writeFogField(entity, cubeEndOffset, cached.endDistance);
                writeFogField(entity, cubeMaxOpacityOffset, cached.maxOpacity);
                writeFogBool(entity, cubeActiveOffset, cached.isEnabled);
                writeFogBool(entity, cubeStartDisabledOffset, cached.startDisabled);
            } else {
                writeFogField(entity, startDistanceOffset, cached.startDistance);
                writeFogField(entity, endDistanceOffset, cached.endDistance);
                writeFogField(entity, maxOpacityOffset, cached.maxOpacity);
                writeFogField(entity, strengthOffset, cached.strength);
                if (colorOffset.has_value())
                    std::memcpy(entity + *colorOffset, &cached.color, sizeof(cached.color));
                writeFogBool(entity, heightFogOffset, cached.heightFogEnabled);
                writeFogBool(entity, startDisabledOffset, cached.startDisabled);
                writeFogBool(entity, enabledOffset, cached.isEnabled);
            }
        }
        state.cachedFogCount = 0;
    }

    [[nodiscard]] static float readFogFloat(const std::byte* entity, std::optional<std::int32_t> offset) noexcept
    {
        float value = 0.0f;
        if (offset.has_value())
            std::memcpy(&value, entity + *offset, sizeof(value));
        return value;
    }

    static void writeFogField(std::byte* entity, std::optional<std::int32_t> offset, float value) noexcept
    {
        if (offset.has_value())
            std::memcpy(entity + *offset, &value, sizeof(value));
    }

    static void readFogField(std::byte* entity, std::optional<std::int32_t> offset, float& target) noexcept
    {
        target = readFogFloat(entity, offset);
    }

    [[nodiscard]] static bool readFogBool(const std::byte* entity, std::optional<std::int32_t> offset) noexcept
    {
        bool value = false;
        if (offset.has_value())
            std::memcpy(&value, entity + *offset, sizeof(value));
        return value;
    }

    static void writeFogBool(std::byte* entity, std::optional<std::int32_t> offset, bool value) noexcept
    {
        if (offset.has_value())
            std::memcpy(entity + *offset, &value, sizeof(value));
    }

    // Layout constants; stride 0x70 is the chams-verified Linux CMeshDrawPrimitive stride.
    struct ParticleDrawPrimitive {
        std::byte bytes[0x70];
    };
    static constexpr auto kParticleCollectionOffset = 0x0;
    static constexpr auto kParticleColorOffset = 0x50;
    static constexpr auto kParticleSystemDefinitionOffset = 0x18; // ParticleCollection::definition (FrameworkCS2 layout, guarded)
    static constexpr auto kParticleSystemNameOffset = 0x8;        // ParticleSystemDefinition::name (guarded)
    static constexpr auto kLightColorOffset = 0xD4;               // SceneLightObject r/g/b (disassembly-verified)
    static constexpr auto kSkyPrimitiveStride = 0x70;             // same 0x70 CMeshDrawPrimitive stride
    static constexpr auto kSkyAnimatableObjectOffset = 0x18;      // prim -> scene object pointer
    static constexpr auto kSceneObjectLightColorOffset = 0x50;    // scene object light-tint dwords (draw path 0x40E300 copies these into the light entries)

    [[nodiscard]] static void* readPointer(const std::byte* address) noexcept
    {
        void* pointer = nullptr;
        std::memcpy(&pointer, address, sizeof(pointer));
        return pointer;
    }

    [[nodiscard]] static bool isPlausibleObjectPointer(const void* pointer) noexcept
    {
        // Only a sanity floor: module bases are high, the null page is low. The real safety is
        // that a wrong pointer still has to survive three chained reads AND a string match
        // before any write happens.
        return reinterpret_cast<std::uintptr_t>(pointer) > 0x10000;
    }

    // /tmp/osiris_particles_debug exists -> collect distinct particle system names and print
    // them (throttled by VerifyConsole) to /tmp/gamesense_gui.log.
    [[nodiscard]] static bool particlesDebugEnabled() noexcept
    {
        const int fd = LinuxPlatformApi::open("/tmp/osiris_particles_debug", 0 /* O_RDONLY */);
        if (fd >= 0) {
            LinuxPlatformApi::close(fd);
            return true;
        }
        return false;
    }

    void dumpParticleNames(void* primitives, int primitiveCount) const noexcept
    {
        char names[12][48]{};
        int nameCount = 0;

        auto* prim = static_cast<const std::byte*>(primitives);
        for (int i = 0; i < primitiveCount && nameCount < 12; ++i, prim += sizeof(ParticleDrawPrimitive)) {
            const void* collection = readPointer(prim + kParticleCollectionOffset);
            if (!isPlausibleObjectPointer(collection))
                continue;
            const void* definition = readPointer(static_cast<const std::byte*>(collection) + kParticleSystemDefinitionOffset);
            if (!isPlausibleObjectPointer(definition))
                continue;
            const char* const* namePointer = static_cast<const char* const*>(readPointer(static_cast<const std::byte*>(definition) + kParticleSystemNameOffset));
            if (!namePointer || !*namePointer)
                continue;

            bool alreadyListed = false;
            for (int n = 0; n < nameCount; ++n) {
                if (std::strncmp(names[n], *namePointer, sizeof(names[0]) - 1) == 0) {
                    alreadyListed = true;
                    break;
                }
            }
            if (alreadyListed)
                continue;
            std::strncpy(names[nameCount], *namePointer, sizeof(names[0]) - 1);
            names[nameCount][sizeof(names[0]) - 1] = '\0';
            ++nameCount;
        }

        for (int n = 0; n < nameCount; ++n)
            VerifyConsole::write(5.0f, "particles", "system name: %s", names[n]);
    }

    static void writeColor(const std::byte* prim, color::Rgba packedColor) noexcept
    {
        const auto address = reinterpret_cast<std::uintptr_t>(prim) + kParticleColorOffset;
        const auto value = static_cast<std::uint32_t>(packedColor);
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    }

    static void cacheOriginalLight(WorldColorsState& state, void* lightObject, const std::byte* colorAddress) noexcept
    {
        for (int i = 0; i < state.cachedLightCount; ++i) {
            if (state.cachedLights[i].lightObject == lightObject)
                return;
        }
        if (state.cachedLightCount >= WorldColorsState::kMaxCachedLights)
            return;
        float rgb[3]{};
        std::memcpy(rgb, colorAddress, sizeof(rgb));
        state.cachedLights[state.cachedLightCount] = WorldColorsState::CachedLight{lightObject, rgb[0], rgb[1], rgb[2]};
        ++state.cachedLightCount;
    }

    static void writeLightChannel(std::byte* address, float value) noexcept
    {
        std::memcpy(address, &value, sizeof(value));
    }

    static void restoreLightColors(WorldColorsState& state) noexcept
    {
        for (int i = 0; i < state.cachedLightCount; ++i) {
            const auto& cached = state.cachedLights[i];
            auto* colorAddress = static_cast<std::byte*>(cached.lightObject) + kLightColorOffset;
            writeLightChannel(colorAddress, cached.r);
            writeLightChannel(colorAddress + 4, cached.g);
            writeLightChannel(colorAddress + 8, cached.b);
        }
        state.cachedLightCount = 0;
    }

    HookContext& hookContext;
};
