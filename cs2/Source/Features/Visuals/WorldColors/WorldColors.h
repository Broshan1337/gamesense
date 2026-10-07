#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_GradientFog.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Config/ConfigVariable.h>
#include <Features/Visuals/WorldColors/WorldColorsConfigVariables.h>
#include <Utils/NsPaths.h>
#include <Utils/NsStr.h>
#include <Features/Visuals/WorldColors/WorldColorsState.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/ColorUtils.h>
#include <Utils/VerifyConsole.h>













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
        
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;
        state.cachedLightCount = 0;
        state.cachedFogCount = 0;
    }

    
    
    void recolorParticles(void* primitives, int primitiveCount) const noexcept
    {
        if (!primitives || primitiveCount <= 0)
            return;

        
        
        
        
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
        
        
        const float scale = static_cast<float>(color.a()) / 255.0f;
        writeLightChannel(light + kLightColorOffset, static_cast<float>(color.r()) * scale / 255.0f);
        writeLightChannel(light + kLightColorOffset + 4, static_cast<float>(color.g()) * scale / 255.0f);
        writeLightChannel(light + kLightColorOffset + 8, static_cast<float>(color.b()) * scale / 255.0f);
    }

    
    
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

    
    
    
    
    
    
    
    
    
    
    void recolorWorld(void* primitives, int primitiveCount) const noexcept
    {
        if (!GET_CONFIG_VAR(WorldColorsWorldEnabled) || !primitives || primitiveCount <= 0)
            return;

        const auto value = static_cast<std::uint32_t>(GET_CONFIG_VAR(WorldColorsWorldColor));
        auto* prim = static_cast<std::byte*>(primitives);
        for (int i = 0; i < primitiveCount; ++i, prim += kSkyPrimitiveStride) {
            std::memcpy(prim + kParticleColorOffset, &value, sizeof(value));

            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
        }
    }

    
    
    
    
    static constexpr auto kSkyTintOffset = 0xD8; 
    
    
    static constexpr auto kSkyDescPointerFromEnd = 0x58;

    
    
    static constexpr const char* kBloomCvarName = "r_csgo_render_post_bloom_strength";


    struct SavedSkyTint {
        void* object;
        float r, g, b;
    };

    [[nodiscard]] int recolorSky(SavedSkyTint* saved, int maxSaved, void* primitives, int primitiveCount) const noexcept
    {
        if (!GET_CONFIG_VAR(WorldColorsSkyEnabled) || !primitives || primitiveCount <= 0)
            return 0;
        if (maxSaved < 1)
            return 0;

        const auto color = GET_CONFIG_VAR(WorldColorsSkyColor);
        
        
        
        const float brightness = static_cast<float>(GET_CONFIG_VAR(WorldColorsSkyBrightness));
        const float channels[3] = {
            static_cast<float>(color.r()) / 255.0f * brightness,
            static_cast<float>(color.g()) / 255.0f * brightness,
            static_cast<float>(color.b()) / 255.0f * brightness,
        };

        
        
        void* object = readPointer(static_cast<std::byte*>(primitives)
            + kSkyPrimitiveStride * primitiveCount - kSkyDescPointerFromEnd);
        if (!isPlausibleObjectPointer(object))
            return 0;
        auto* tint = static_cast<std::byte*>(object) + kSkyTintOffset;
        float original[3]{};
        std::memcpy(original, tint, sizeof(original));
        saved[0] = SavedSkyTint{object, original[0], original[1], original[2]};
        std::memcpy(tint, channels, sizeof(channels));
        return 1;
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
    
    
    
    
    
    
    
    
    
    
    void updateFog() const noexcept
    {
        auto& state = hookContext.featuresStates().visualFeaturesStates.worldColorsState;

        
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

            
            bool alreadyCached = false;
            bool nativelyEnabled = false;
            for (int i = 0; i < state.cachedFogCount; ++i) {
                if (state.cachedFogs[i].fogEntity == entity) {
                    alreadyCached = true;
                    nativelyEnabled = state.cachedFogs[i].isEnabled;
                    break;
                }
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
                
                
                
                
                
                
                
                
                if (!nativelyEnabled)
                    return;
                writeFogField(entity, cubeEndOffset, fogDistance);
                writeFogField(entity, cubeMaxOpacityOffset, density);
            } else {
                
                
                
                
                if (!nativelyEnabled)
                    return;
                writeFogField(entity, endDistanceOffset, fogDistance);
                writeFogField(entity, maxOpacityOffset, density);
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

    
    struct ParticleDrawPrimitive {
        std::byte bytes[0x70];
    };
    static constexpr auto kParticleCollectionOffset = 0x0;
    static constexpr auto kParticleColorOffset = 0x50;
    static constexpr auto kParticleSystemDefinitionOffset = 0x18; 
    static constexpr auto kParticleSystemNameOffset = 0x8;        
    static constexpr auto kLightColorOffset = 0xD4;               
    static constexpr auto kSkyPrimitiveStride = 0x70;             
                                                                  
                                                                  
    static constexpr auto kSceneObjectLightColorOffset = 0x50;    

    [[nodiscard]] static void* readPointer(const std::byte* address) noexcept
    {
        void* pointer = nullptr;
        std::memcpy(&pointer, address, sizeof(pointer));
        return pointer;
    }

    [[nodiscard]] static bool isPlausibleObjectPointer(const void* pointer) noexcept
    {
        
        
        
        return reinterpret_cast<std::uintptr_t>(pointer) > 0x10000;
    }

    
    
    
    [[nodiscard]] static bool particlesDebugEnabled() noexcept
    {
        char debugFile[ns_paths::kMaxPath];
        if (!ns_paths::join(debugFile, sizeof(debugFile), "osiris_particles_debug"))
            return false;
        const int fd = LinuxPlatformApi::open(debugFile, 0 );
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
