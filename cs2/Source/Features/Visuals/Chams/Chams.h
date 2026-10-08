#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Visuals/Chams/ChamsConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <GameClient/ClientBuildProfile.h>
#include <Utils/CrashLogger.h>

















template <typename HookContext>
class Chams {
public:
    explicit Chams(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // CGlowHelperSceneObjectDesc::GeneratePrimitives calls the attached mesh
    // descriptor at +0x20, returning to client +0x1907de1 on this build.
    // Preserve that nested generation while suppressing its ordinary mesh pass.
    [[nodiscard]] static bool isOutlineGeneration(std::uintptr_t returnAddress) noexcept
    {
        return hideModelsSupported() && returnAddress == CrashLogger::clientModule.base + 0x1907de1;
    }
    [[nodiscard]] static bool hideModelsSupported() noexcept
    {
        int status = outlineProfileStatus.load(std::memory_order_relaxed);
        if (!status) {
            status = client_build_profile::supported(CrashLogger::clientModule.base) ? 2 : 1;
            outlineProfileStatus.store(status, std::memory_order_relaxed);
        }
        return status == 2;
    }
    [[nodiscard]] static std::uint32_t primitiveCount(void* primitives) noexcept {
        std::uint32_t count{};
        if (primitives) std::memcpy(&count, static_cast<std::byte*>(primitives) + kPrimitiveCountOffset, sizeof(count));
        return count;
    }
    static void suppressGeneratedPrimitives(void* primitives, std::uint32_t previous) noexcept {
        if (primitives && primitiveCount(primitives) >= previous)
            std::memcpy(static_cast<std::byte*>(primitives) + kPrimitiveCountOffset, &previous, sizeof(previous));
    }

    static constexpr std::size_t kPrimitiveStride = 0x70;
    static constexpr int kMaxLivePawns = 32;
    static constexpr int kSnapshotRefreshInterval = 64;

    
    
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

    
    
    
    
    [[nodiscard]] static std::uint32_t primitiveColor(std::uint32_t rgbaPacked) noexcept
    {
        return rgbaPacked;
    }

private:
    static constexpr std::ptrdiff_t kBodyComponentOffset = 0x100;   
    static constexpr std::ptrdiff_t kSkeletonInstanceSlotDelta = 0x100; 
    static constexpr std::ptrdiff_t kNodeOwnerOffset = 0x30;        
    static constexpr std::ptrdiff_t kGeneratedLayersFlagOffset = 0x78;
    static constexpr std::uint32_t kGeneratedLayersBit = 1u << 3;
    static constexpr std::size_t kPrimitiveCountOffset = 0xC;
    static constexpr std::size_t kPrimitiveColorOffset = 0x50;
    static constexpr std::size_t kPrimitiveDepthOffset = 0x5E;
    static constexpr std::size_t kPrimitiveFlagsOffset = 0x62;
    
    
    static constexpr std::uint16_t kPrimitiveNoDepthBits = 0x0020;

    inline static std::atomic<int> outlineProfileStatus{0};
    inline static cs2::C_BaseEntity* livePawnPointers[kMaxLivePawns]{};
    inline static int livePawnCount{0};
    inline static cs2::C_BaseEntity* localPawnPointer{nullptr};
    inline static int callsUntilRefresh{0};

public:
    [[nodiscard]] static int livePawnCountForDiagnostics() noexcept { return livePawnCount; }
    
    
    
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
