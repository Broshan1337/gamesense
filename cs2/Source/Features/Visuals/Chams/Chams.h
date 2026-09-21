#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Visuals/Chams/ChamsConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Platform/Linux/LinuxPlatformApi.h>

// Enemy chams - the GeneratePrimitives overlay port (velocity phase 2.3 state, preserved in the
// project memory when the old visual features were removed; now revived for the skeet-parity
// chams). The scene render thread re-runs GeneratePrimitives a second time for enemy scene
// objects and tints the APPENDED primitive range:
//
//   primitive buffer = { elements ptr @+0x0, capacity u32 @+0x8, count u32 @+0xC }
//   primitive stride 0x70 (LINUX - Windows 0x68 is WRONG and corrupts the array)
//   color  @prim+0x50 (packed R | G<<8 | B<<16 | A<<24)
//   depth  @prim+0x5E (u16 sort key; 0xFFFF = unset sentinel that also sorts last)
//
// Classification: scene_object+0x3C8 = owning ENTITY POINTER (Linux; Windows stores a u32 handle
// at +0xC0). A scene object can OUTLIVE its entity for a frame (death / round restart), so the
// owner pointer is only dereferenced after membership in a live-pawn pointer snapshot that is
// refreshed from the entity system every 64 generate calls - entity add/remove happens on the
// main thread during sim, never during the render phase's GeneratePrimitives, which makes the
// snapshot race-free (2026-08-25 crash fix).
template <typename HookContext>
class Chams {
public:
    explicit Chams(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    static constexpr std::size_t kPrimitiveStride = 0x70;
    static constexpr int kMaxLivePawns = 32;
    static constexpr int kSnapshotRefreshInterval = 64;

    // Refresh the live-pawn pointer snapshot (called from the hook every kSnapshotRefreshInterval
    // generate calls; also the place the enemy count for the heartbeat comes from).
    void refreshLivePawnPointers() noexcept
    {
        livePawnCount = 0;
        localPawnPointer = nullptr;
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (livePawnCount >= kMaxLivePawns)
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = baseEntity.template as<PlayerPawn>();
            if (!pawn)
                return;
            if (pawn.isControlledByLocalPlayer()) {
                localPawnPointer = static_cast<cs2::C_BaseEntity*>(identity.entity);
                return;
            }
            if (pawn.isEnemy() != true || pawn.isAlive() != true)
                return;
            livePawnPointers[livePawnCount++] = static_cast<cs2::C_BaseEntity*>(identity.entity);
        });
    }

    // Does this scene object belong to an alive enemy pawn?
    //
    // The scene object no longer stores its owner directly (the old +0x3C8 field is gone in the
    // 2026-09 build). The verified chain (2026-09-06, live-correlated + schema-backed):
    //   sceneObject+0x100 = the pawn's CBodyComponentSkeletonInstance NetworkVar_m_skeletonInstance
    //                       member (typeinfo "N30CBodyComponentSkeletonInstance29NetworkVar_..."),
    //   wrapper-0x100     = the component's m_skeletonInstance slot (schema: component+0x80,
    //                       CSkeletonInstance = the pawn's game scene node),
    //   node+0x30         = the owning C_BaseEntity (moved from the old owner offset).
    // The final value is validated against the live-pawn snapshot, so a layout shift anywhere in
    // the chain degrades to "no match" (no overlay), never a false hit.
    [[nodiscard]] bool wantsOverlayPass(const void* sceneObject) noexcept
    {
        if (!sceneObject)
            return false;

        void* wrapper = nullptr;
        if (!LinuxPlatformApi::safeRead(static_cast<const std::byte*>(sceneObject) + kBodyComponentOffset, &wrapper, sizeof(wrapper)))
            return false;
        if (reinterpret_cast<std::uintptr_t>(wrapper) < 0x10000)
            return false;

        void* node = nullptr;
        if (!LinuxPlatformApi::safeRead(static_cast<std::byte*>(wrapper) - kSkeletonInstanceSlotDelta, &node, sizeof(node)))
            return false;
        if (reinterpret_cast<std::uintptr_t>(node) < 0x10000)
            return false;

        void* owner = nullptr;
        if (!LinuxPlatformApi::safeRead(static_cast<std::byte*>(node) + kNodeOwnerOffset, &owner, sizeof(owner)))
            return false;
        return isLivePawnPointer(owner);
    }

    // Re-running GeneratePrimitives for the same object only produces duplicates if its
    // "already generated" marker is cleared first (verified in the CBaseSceneObjectDesc impl).
    static void clearGeneratedLayersFlag(void* sceneObject) noexcept
    {
        if (!sceneObject)
            return;
        auto* const flag = static_cast<std::byte*>(sceneObject) + kGeneratedLayersFlagOffset;
        std::uint32_t value{};
        std::memcpy(&value, flag, sizeof(value));
        value &= ~kGeneratedLayersBit;
        std::memcpy(flag, &value, sizeof(value));
    }

    // Tints primitives [prevCount, count) with the chams color (prim+0x50 packed RGBA, layout
    // re-verified 2026-09-06 against the current Animatable GeneratePrimitives decompile -
    // sub_52A630 packs and stores to +0x50). prevCount = 0 tints the object's own primitives
    // in place (flat chams); the depth/sort/flag fields are left to the engine.
    static void applyOverlay(void* primitives, std::uint32_t prevCount, std::uint32_t colorPacked) noexcept
    {
        if (!primitives)
            return;

        void* elements = nullptr;
        std::memcpy(&elements, primitives, sizeof(elements));
        std::uint32_t count = 0;
        std::memcpy(&count, static_cast<std::byte*>(primitives) + kPrimitiveCountOffset, sizeof(count));
        if (!elements || count <= prevCount)
            return;

        for (std::uint32_t i = prevCount; i < count; ++i) {
            auto* const prim = static_cast<std::byte*>(elements) + static_cast<std::size_t>(i) * kPrimitiveStride;
            std::memcpy(prim + kPrimitiveColorOffset, &colorPacked, sizeof(colorPacked));
        }
    }

    // 0xAABBGGRR (Rgba) -> the engine's primitive color layout. Live-verified 2026-09-06: the
    // dword at prim+0x50 is consumed with R in the HIGH byte - i.e. the RAW Rgba packed value
    // stored as-is. (The August-era note claiming "R | G<<8 | B<<16 | A<<24" produced a green
    // world for a purple pick - the observed channel swap proved the bswap wrong.)
    [[nodiscard]] static std::uint32_t primitiveColor(std::uint32_t rgbaPacked) noexcept
    {
        return rgbaPacked;
    }

private:
    static constexpr std::ptrdiff_t kBodyComponentOffset = 0x100;   // sceneObject -> NetworkVar_m_skeletonInstance
    static constexpr std::ptrdiff_t kSkeletonInstanceSlotDelta = 0x100; // wrapper-0x100 = component+0x80 (m_skeletonInstance)
    static constexpr std::ptrdiff_t kNodeOwnerOffset = 0x30;        // CGameSceneNode owner (2026-09 build)
    static constexpr std::ptrdiff_t kGeneratedLayersFlagOffset = 0x78;
    static constexpr std::uint32_t kGeneratedLayersBit = 1u << 3;
    static constexpr std::size_t kPrimitiveCountOffset = 0xC;
    static constexpr std::size_t kPrimitiveColorOffset = 0x50;
    static constexpr std::size_t kPrimitiveDepthOffset = 0x5E;
    static constexpr std::size_t kPrimitiveFlagsOffset = 0x62;
    // Render-state flag experiment (see applyOverlay): bit 0x0020 = the per-primitive flag the
    // scene system checks to force a render-context state write; 0x0800 is tested alongside it.
    static constexpr std::uint16_t kPrimitiveNoDepthBits = 0x0020;

    inline static cs2::C_BaseEntity* livePawnPointers[kMaxLivePawns]{};
    inline static int livePawnCount{0};
    inline static cs2::C_BaseEntity* localPawnPointer{nullptr};
    inline static int callsUntilRefresh{0};

public:
    [[nodiscard]] static int livePawnCountForDiagnostics() noexcept { return livePawnCount; }
    // World-modulation discrimination: does this scene object belong to the LOCAL player pawn?
    // Same owner chain as wantsOverlayPass, validated against the local-pawn snapshot captured
    // by refreshLivePawnPointers - a stale layout anywhere degrades to "not the local pawn".
    [[nodiscard]] bool ownsLocalPawn(const void* sceneObject) noexcept
    {
        if (!sceneObject)
            return false;

        void* wrapper = nullptr;
        if (!LinuxPlatformApi::safeRead(static_cast<const std::byte*>(sceneObject) + kBodyComponentOffset, &wrapper, sizeof(wrapper)))
            return false;
        if (reinterpret_cast<std::uintptr_t>(wrapper) < 0x10000)
            return false;

        void* node = nullptr;
        if (!LinuxPlatformApi::safeRead(static_cast<std::byte*>(wrapper) - kSkeletonInstanceSlotDelta, &node, sizeof(node)))
            return false;
        if (reinterpret_cast<std::uintptr_t>(node) < 0x10000)
            return false;

        void* owner = nullptr;
        if (!LinuxPlatformApi::safeRead(static_cast<std::byte*>(node) + kNodeOwnerOffset, &owner, sizeof(owner)))
            return false;
        return localPawnPointer && static_cast<const void*>(owner) == static_cast<const void*>(localPawnPointer);
    }
    [[nodiscard]] static const void* livePawnPointerForDiagnostics(int index) noexcept
    {
        return (index >= 0 && index < livePawnCount) ? static_cast<const void*>(livePawnPointers[index]) : nullptr;
    }
    [[nodiscard]] static bool isLivePawnPointer(const void* candidate) noexcept
    {
        if (!candidate)
            return false;
        for (int i = 0; i < livePawnCount; ++i) {
            if (static_cast<const void*>(livePawnPointers[i]) == candidate)
                return true;
        }
        return false;
    }

private:

protected:
    HookContext& hookContext;
};
