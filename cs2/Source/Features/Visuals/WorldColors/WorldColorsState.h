#pragma once

#include <cstdint>




struct WorldColorsState {
    struct CachedLight {
        void* lightObject;
        float r, g, b;
    };

    static constexpr int kMaxCachedLights = 64;

    CachedLight cachedLights[kMaxCachedLights]{};
    int cachedLightCount = 0;

    
    
    
    
    
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

    
    
    bool bloomWasEnabled = false;
    bool bloomOriginalValid = false;
    float bloomOriginal = 0.0f;
    bool bloomQueuedValid = false;
    float lastQueued = 0.0f;
    double lastQueueTime = 0.0;

    // Engine fog_* cvar originals (cached on first fog enable, restored on disable).
    bool fogCvarsCached = false;
    int fogOverrideOriginal = 0;
    bool fogOverrideEnableOriginal = false;
    float fogColorOriginal[3]{};
    float fogStartOriginal = 0.0f;
    float fogEndOriginal = 0.0f;
    float fogMaxDensityOriginal = 0.0f;
    double fogLastQueueTime = 0.0;
};
