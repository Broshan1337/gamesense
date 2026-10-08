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
};
