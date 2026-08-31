#pragma once

#include <cstdint>

// FrameworkCS2 port (Source/Features/Visuals/WorldColors), lights subset only: per-light original
// colors are cached while the recolor is active and restored when it is disabled or the map
// changes (light objects die with the map, so the cache must not survive game_newmap).
struct WorldColorsState {
    struct CachedLight {
        void* lightObject;
        float r, g, b;
    };

    static constexpr int kMaxCachedLights = 64;

    CachedLight cachedLights[kMaxCachedLights]{};
    int cachedLightCount = 0;

    // Fog original values (same lifecycle as lights: cached while active, restored on
    // disable, dropped on game_newmap because the entities die with the map). Covers both
    // fog entity types - C_GradientFog and C_EnvCubemapFog (several maps drive their actual
    // atmosphere with the cubemap fog while the gradient-fog entities sit dormant).
    // isEnabled doubles as GradientFog's m_bIsEnabled / CubemapFog's m_bActive.
    struct CachedFog {
        void* fogEntity;
        bool isCubemap;
        float startDistance, endDistance, farZ, maxOpacity, strength;
        std::uint32_t color;
        bool heightFogEnabled, startDisabled, isEnabled;
    };

    static constexpr int kMaxCachedFogs = 8;

    CachedFog cachedFogs[kMaxCachedFogs]{};
    int cachedFogCount = 0;

    // Sky Bloom (r_csgo_render_post_bloom_strength): the game's original value cached on first
    // enable so disabling restores it exactly. bloomWasEnabled drives the disable transition.
    bool bloomWasEnabled = false;
    bool bloomOriginalValid = false;
    float bloomOriginal = 0.0f;
};
