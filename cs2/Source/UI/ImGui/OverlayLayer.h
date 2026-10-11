#pragma once

#include <cstdint>
#include <mutex>

#include <CS2/Classes/Vector.h>

#include <Utils/SpinLock.h>








namespace overlay_layer
{

// Producer side (game thread) publishes raw data; renderGameOverlay draws it.
// The old per-frame producer for Arrow was removed with the velocity-ESP tab;
// the struct is repurposed for the off-screen enemy arrows feature.

struct Arrow {
    float xPercent;   // projected enemy head position, percent of screen width (may be off-screen)
    float yPercent;   // projected enemy head position, percent of screen height
    bool behind;      // clip w <= 0: projected point mirrors through screen center
};

constexpr int kMaxArrows = 16;

inline SpinLock lock;
inline Arrow arrows[kMaxArrows];
inline int arrowCount = 0;

// Bullet tracers are IN-GAME (the weapon's own tracer particle system spawned
// via the game's fx primitive - see Features/Visuals/BulletTracers): they are
// NOT overlay primitives and never pass through here.

// Kill-effect lightning bolts: a strike from the SKY down to the victim's
// death position. The producer projects the death spot AND the world point
// straight above it (2500u up) and publishes both as screen percents; the
// draw side converts to pixels, follows the base->sky direction (capped at
// 1.5x screen height, clamped to never draw sideways/below - the floor-lines
// bug class), and builds the jagged channel + forks there.
struct Bolt {
    float xPercent;       // strike point (victim death position)
    float yPercent;
    float upXPercent;   // sky end (world point 2500u above the kill, projected)
    float upYPercent;
    float ageScale;        // 0 = fresh .. 1 = expired (fade baked in by producer)
    std::uint32_t seed;
    std::uint32_t rgba;
};

constexpr int kMaxBolts = 4;

inline SpinLock boltLock;
inline Bolt bolts[kMaxBolts];
inline int boltCount = 0;


struct Hitmarker {
    float gap;
    float length;
    std::uint32_t rgba; 
};

inline SpinLock hitmarkerLock;
inline Hitmarker hitmarker{};
inline bool hasHitmarker = false;



struct Timer {
    float xPercent; 
    float yPercent; 
    float seconds;
    std::uint32_t rgba;
};

constexpr int kMaxTimers = 16;

inline SpinLock timersLock;
inline Timer timers[kMaxTimers];
inline int timerCount = 0;


inline void publishTimers(const Timer* newTimers, int count) noexcept
{
    if (count > kMaxTimers)
        count = kMaxTimers;
    const std::lock_guard guard{timersLock};
    for (int i = 0; i < count; ++i)
        timers[i] = newTimers[i];
    timerCount = count;
}


inline void publish(const Arrow* newArrows, int count) noexcept
{
    if (count > kMaxArrows)
        count = kMaxArrows;
    const std::lock_guard guard{lock};
    for (int i = 0; i < count; ++i)
        arrows[i] = newArrows[i];
    arrowCount = count;
}

inline void publishBolts(const Bolt* newBolts, int count) noexcept
{
    if (count > kMaxBolts)
        count = kMaxBolts;
    const std::lock_guard boltGuard{boltLock};
    for (int i = 0; i < count; ++i)
        bolts[i] = newBolts[i];
    boltCount = count;
}


inline void publishHitmarker(const Hitmarker& newHitmarker) noexcept
{
    const std::lock_guard guard{hitmarkerLock};
    hitmarker = newHitmarker;
    hasHitmarker = newHitmarker.rgba != 0;
}


// Rage multipoint scan cloud (velocity's debug multipoints): the game thread
// (CreateMove) publishes the generated scan points projected to screen percents;
// renderGameOverlay draws filled circles colored per hitbox group, center
// points larger. Producer: Features/Combat/Aimbot (Aimbot::refineMultipointCapsule).
struct MultiPoint {
    float xPercent;
    float yPercent;
    std::uint32_t rgba;   // color::Rgba packing (r<<24|g<<16|b<<8|a)
    bool center;
};

constexpr int kMaxMultiPoints = 128;

inline SpinLock multiPointLock;
inline MultiPoint multiPoints[kMaxMultiPoints];
inline int multiPointCount = 0;

inline void publishMultiPoints(const MultiPoint* newPoints, int count) noexcept
{
    if (count > kMaxMultiPoints)
        count = kMaxMultiPoints;
    const std::lock_guard multiPointGuard{multiPointLock};
    for (int i = 0; i < count; ++i)
        multiPoints[i] = newPoints[i];
    multiPointCount = count;
}

// Anti-aim direction indicator (velocity misc.cpp on_render): the game thread
// publishes the arc points (feet-centered 60 degree sweep at the AA yaw,
// projected to screen percents, fade baked into rgba); renderGameOverlay draws
// the tapering polyline. Producer: Features/Combat/AntiAim.
struct AaArcPoint {
    float xPercent;
    float yPercent;
    std::uint32_t rgba;
};

constexpr int kMaxAaArcPoints = 56;

inline SpinLock aaArcLock;
inline AaArcPoint aaArcPoints[kMaxAaArcPoints];
inline int aaArcPointCount = 0;

inline void publishAaArc(const AaArcPoint* newPoints, int count) noexcept
{
    if (count > kMaxAaArcPoints)
        count = kMaxAaArcPoints;
    const std::lock_guard aaArcGuard{aaArcLock};
    for (int i = 0; i < count; ++i)
        aaArcPoints[i] = newPoints[i];
    aaArcPointCount = count;
}

// ---- ESP scene primitives (core ESP suite, Features/Visuals/Esp) ----
// All coordinates are PERCENTS OF DISPLAY (xPercent/yPercent like Arrow);
// the draw side converts with the current display size. rgba values carry
// color::Rgba packing. Fixed-capacity: producers must respect the caps.

struct EspRect {
    float xPercent;         // box left
    float yPercent;         // box top
    float wPercent;         // box width
    float hPercent;         // box height
    std::uint32_t rgba;
    bool cornered;          // corner-brackets instead of full rect
    bool fill;              // also fill the box
    float cornerPercent;    // corner bracket arm length
    float thickness;        // px, draw-side
};

struct EspLine {
    float axPercent, ayPercent, bxPercent, byPercent;
    std::uint32_t rgba;
    float thickness;        // px, draw-side
};

struct EspCircle {
    float xPercent, yPercent, radiusPx;
    std::uint32_t rgba;
    float thickness;
};

struct EspText {
    float xPercent, yPercent;
    bool center;             // horizontally centered at x
    std::uint32_t rgba;
    float size;               // font pt
    char text[32];
};

// Vertical bar (health/armor): fill rises from the bottom of the bar.
// backupFill/backupRgba = the white damage-echo overlay (hzqst backup-health
// animation; producer fades its alpha).
struct EspBar {
    float xPercent, yPercent;
    float wPx;              // width in PIXELS (constant bar width, not display percent)
    float hPercent;
    float fill;             // 0..1
    float backupFill;        // 0..1, 0 = none
    std::uint32_t rgba;
    std::uint32_t backupRgba;
};

// Custom crosshair (vesta crosshair.cpp pieces): filled rects in
// percent-of-display coordinates, reference-1920 px inputs scaled by the
// producer. Penetration resolution bakes into rgba (producer side).
struct CrosshairPiece {
    float xPercent, yPercent, wPercent, hPercent;
};

struct Crosshair {
    CrosshairPiece pieces[5];
    int pieceCount{};
    std::uint32_t rgba;
    float thicknessPx;   // unused by the draw side (pieces carry their sizes)
    float outlinePx;    // >0 = draw a black outline rect around each piece
    char damageText[16];  // penetration crosshair: wallbang damage ("37 dmg")
    bool hasDamageText{};
};

constexpr int kMaxCrosshairPieces = 5;

inline SpinLock crosshairLock;
inline Crosshair crosshairPublished{};

inline void publishCrosshair(const Crosshair& newCrosshair) noexcept
{
    const std::lock_guard crosshairGuard{crosshairLock};
    crosshairPublished = newCrosshair;
}

// Radar overlay (Features/Visuals/Radar): north-up, positions relative to
// the local player in UNITS; the draw side scales by the window size / zoom.
struct RadarDot {
    float xOffsetUnits;
    float yOffsetUnits;
    bool enemy;
    float yaw;           // view yaw for the cone
};

constexpr int kMaxRadarDots = 32;

struct RadarScene {
    RadarDot dots[kMaxRadarDots];
    int dotCount = 0;
    bool hasBomb = false;
    float bombXOffsetUnits{};
    float bombYOffsetUnits{};
    float zoom = 12.0f;     // world units per radar-pixel band
    float sizePx = 180.0f;
    bool viewCones = true;
};

inline SpinLock radarLock;
inline RadarScene radarScene{};

inline void publishRadar(const RadarScene& newScene) noexcept
{
    const std::lock_guard radarGuard{radarLock};
    radarScene = newScene;
}

// Bomb info (Features/Visuals/BombInfo): world position + countdown; the draw
// side projects the world position and renders the panel + safe-zone ring.
struct BombInfo {
    bool planted = false;
    bool ticking = false;
    bool beingDefused = false;
    bool canDefuse = false;
    int site = -1;             // 0 = A, 1 = B
    float timeLeft = -1.0f;
    float defuseTimeLeft = -1.0f;
    int localHealth = 100;
    float surviveRadius = 0.0f;  // world units; 0 = no safe zone
    float x{}, y{}, z{};
};

inline SpinLock bombInfoLock;
inline BombInfo bombInfo{};

inline void publishBombInfo(const BombInfo& newInfo) noexcept
{
    const std::lock_guard bombGuard{bombInfoLock};
    bombInfo = newInfo;
}

// Grenade markers (Features/Visuals/GrenadePrediction).
struct GrenadeMarker {
    float x, y, z;
    float timeLeft;   // flash: seconds to blast; inferno: unused
};

struct GrenadeScene {
    GrenadeMarker flashes[16];
    int flashCount = 0;
    GrenadeMarker infernos[16];
    int infernoCount = 0;
    // Held-grenade trajectory preview (world-space points + landing; the draw
    // side projects). Producer: Features/Visuals/GrenadePrediction.
    cs2::Vector trajectory[64];
    int trajectoryCount = 0;
    bool hasLanding = false;
    cs2::Vector landing{};
};

constexpr int kMaxGrenadeMarkers = 16;

inline SpinLock grenadeLock;
inline GrenadeScene grenadeScene{};

inline void publishGrenades(const GrenadeScene& newScene) noexcept
{
    const std::lock_guard grenadeGuard{grenadeLock};
    grenadeScene = newScene;
}

constexpr int kMaxEspRects = 32;
constexpr int kMaxEspLines = 512;
constexpr int kMaxEspCircles = 32;
constexpr int kMaxEspTexts = 192;
constexpr int kMaxEspBars = 64;

struct EspScene {
    EspRect rects[kMaxEspRects];
    int rectCount = 0;
    EspLine lines[kMaxEspLines];
    int lineCount = 0;
    EspCircle circles[kMaxEspCircles];
    int circleCount = 0;
    EspText texts[kMaxEspTexts];
    int textCount = 0;
    EspBar bars[kMaxEspBars];
    int barCount = 0;
};

inline SpinLock espLock;
inline EspScene espScene;

inline void publishEsp(const EspScene& newScene) noexcept
{
    const std::lock_guard espGuard{espLock};
    espScene = newScene;
}

struct Snapshot {
    Arrow arrows[kMaxArrows];
    int count = 0;
    Hitmarker hitmarker{};
    bool hasHitmarker = false;
    Timer timers[kMaxTimers];
    int timerCount = 0;
    Bolt bolts[kMaxBolts];
    int boltCount = 0;
    MultiPoint multiPoints[kMaxMultiPoints];
    int multiPointCount = 0;
    AaArcPoint aaArcPoints[kMaxAaArcPoints];
    int aaArcPointCount = 0;
    EspScene espScene;
    Crosshair crosshair{};
    RadarScene radar{};
    BombInfo bombInfo{};
    GrenadeScene grenadeScene{};
};

[[nodiscard]] inline Snapshot snapshot() noexcept
{
    const std::lock_guard guard{lock};
    Snapshot s;
    s.count = arrowCount;
    for (int i = 0; i < arrowCount; ++i)
        s.arrows[i] = arrows[i];
    {
        const std::lock_guard hitmarkerGuard{hitmarkerLock};
        s.hitmarker = hitmarker;
        s.hasHitmarker = hasHitmarker;
    }
    {
        const std::lock_guard timersGuard{timersLock};
        s.timerCount = timerCount;
        for (int i = 0; i < timerCount; ++i)
            s.timers[i] = timers[i];
    }
    {
        const std::lock_guard boltGuard{boltLock};
        s.boltCount = boltCount;
        for (int i = 0; i < boltCount; ++i)
            s.bolts[i] = bolts[i];
    }
    {
        const std::lock_guard multiPointGuard{multiPointLock};
        s.multiPointCount = multiPointCount;
        for (int i = 0; i < multiPointCount; ++i)
            s.multiPoints[i] = multiPoints[i];
    }
    {
        const std::lock_guard aaArcGuard{aaArcLock};
        s.aaArcPointCount = aaArcPointCount;
        for (int i = 0; i < aaArcPointCount; ++i)
            s.aaArcPoints[i] = aaArcPoints[i];
    }
    {
        const std::lock_guard espGuard{espLock};
        s.espScene = espScene;
    }
    {
        const std::lock_guard crosshairGuard{crosshairLock};
        s.crosshair = crosshairPublished;
    }
    {
        const std::lock_guard radarGuard{radarLock};
        s.radar = radarScene;
    }
    {
        const std::lock_guard bombGuard{bombInfoLock};
        s.bombInfo = bombInfo;
    }
    {
        const std::lock_guard grenadeGuard{grenadeLock};
        s.grenadeScene = grenadeScene;
    }
    return s;
}

}
