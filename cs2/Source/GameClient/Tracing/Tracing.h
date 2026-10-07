#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <GameClient/Tracing/TracingSigs.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>











class Tracing {
public:
    struct Result {
        bool didHit;         
        float fraction;      
        cs2::Vector endPos;  
        cs2::Vector normal;  
        void* hitEntity;
        bool valid{false};

        [[nodiscard]] bool reaches(const void* targetEntity) const noexcept
        {
            return valid && (!didHit || (targetEntity && hitEntity == targetEntity));
        }
    };

    
    
    
    struct DebugState {
        bool anchorsOk;
        int failStep;                 
        std::uintptr_t moduleBase;
        std::uintptr_t traceShape;    
        std::uintptr_t entityToHandle;
        std::uintptr_t managerQword;  
        std::uintptr_t filterVtable;
        void* manager;                
        bool managerGuardRejected;    
        void* managerSubObject;       
        std::uint8_t readinessByte;   
    };

    [[nodiscard]] static DebugState debugState() noexcept
    {
        DebugState out{};
        
        
        
        const auto& anchors = tracing_sigs::resolved();
        out.anchorsOk = anchors.ok;
        out.failStep = anchors.failStep;
        out.moduleBase = static_cast<std::uintptr_t>(anchors.moduleBase);
        out.traceShape = static_cast<std::uintptr_t>(anchors.traceShape);
        out.entityToHandle = static_cast<std::uintptr_t>(anchors.entityToHandle);
        out.managerQword = static_cast<std::uintptr_t>(anchors.managerQword);
        out.filterVtable = static_cast<std::uintptr_t>(anchors.filterVtable);

        if (anchors.managerQword != 0) {
            void* manager{};
            std::memcpy(&manager, reinterpret_cast<const void*>(anchors.managerQword), sizeof(manager));
            const auto asAddress = reinterpret_cast<std::uintptr_t>(manager);
            const auto moduleBase = static_cast<std::uintptr_t>(anchors.moduleBase);
            if (!manager)
                out.manager = nullptr;
            else if (asAddress < moduleBase || asAddress >= moduleBase + tracing_sigs::kMaxModuleExtent)
                out.managerGuardRejected = true;
            else {
                out.manager = manager;
                std::memcpy(&out.managerSubObject, manager, sizeof(out.managerSubObject));
            }
        }
        
        if (out.moduleBase != 0)
            out.readinessByte = *reinterpret_cast<const volatile std::uint8_t*>(out.moduleBase + 0x480D590);
        return out;
    }

    
    
    
    
    [[nodiscard]] static Result traceLine(const cs2::Vector& start, const cs2::Vector& end, void* skipEntity = nullptr, std::uint64_t mask = kDefaultMask) noexcept
    {
        Result out{false, 1.0f, end, cs2::Vector{}, nullptr};

        const auto anchors = resolvedAnchors();
        if (!anchors)
            return out;

        void* const manager = managerPointer();
        if (!manager)
            return out;

        const auto traceShape = reinterpret_cast<TraceShapeFn>(anchors->traceShape);

        
        alignas(16) std::byte ray[kRaySize]{};

        
        alignas(16) std::byte filter[kFilterSize]{};
        writeAt<std::uintptr_t>(filter, 0x00, anchors->filterVtable); 
        writeAt<std::uint64_t>(filter, 0x08, mask);                        
        
        
        std::memset(filter + 0x20, 0xFF, 0x10);
        if (skipEntity) {
            const auto toHandle = reinterpret_cast<EntityToHandleFn>(anchors->entityToHandle);
            writeAt<std::uint32_t>(filter, 0x20, static_cast<std::uint32_t>(toHandle(skipEntity)));
        }
        
        
        
        
        
        
        
        writeAt<std::uint64_t>(filter, 0x30, 0x0000FFFF00000000ull);
        writeAt<std::uint16_t>(filter, 0x37, 0x030F);
        writeAt<std::uint8_t>(filter, 0x39, 0x49);

        
        
        alignas(16) std::byte result[kResultSize]{};
        writeAt<float>(result, kFractionOffset, 1.0f);

        traceShape(manager, ray, &start, &end, filter, result);

        out.fraction = readAt<float>(result, kFractionOffset);
        out.valid = out.fraction >= 0.0f && out.fraction <= 1.0f;
        out.didHit = out.fraction < 1.0f;
        out.endPos = readAt<cs2::Vector>(result, kEndPosOffset);
        out.normal = readAt<cs2::Vector>(result, kNormalOffset);
        out.hitEntity = readAt<void*>(result, kHitEntityOffset);
        return out;
    }

    
    
    
    [[nodiscard]] static Result traceHull(const cs2::Vector& start, const cs2::Vector& end, const cs2::Vector& mins, const cs2::Vector& maxs, void* skipEntity = nullptr, std::uint64_t mask = kDefaultMask) noexcept
    {
        Result out{false, 1.0f, end, cs2::Vector{}, nullptr};

        const auto anchors = resolvedAnchors();
        if (!anchors)
            return out;

        void* const manager = managerPointer();
        if (!manager)
            return out;

        const auto traceShape = reinterpret_cast<TraceShapeFn>(anchors->traceShape);

        
        alignas(16) std::byte ray[kRaySize]{};
        writeAt<cs2::Vector>(ray, 0x00, mins);
        writeAt<cs2::Vector>(ray, 0x0C, maxs);
        writeAt<std::uint8_t>(ray, 0x28, 2);

        alignas(16) std::byte filter[kFilterSize]{};
        writeAt<std::uintptr_t>(filter, 0x00, anchors->filterVtable);
        writeAt<std::uint64_t>(filter, 0x08, mask);
        std::memset(filter + 0x20, 0xFF, 0x10);
        if (skipEntity) {
            const auto toHandle = reinterpret_cast<EntityToHandleFn>(anchors->entityToHandle);
            writeAt<std::uint32_t>(filter, 0x20, static_cast<std::uint32_t>(toHandle(skipEntity)));
        }
        
        writeAt<std::uint64_t>(filter, 0x30, 0x0000FFFF00000000ull);
        writeAt<std::uint16_t>(filter, 0x37, 0x030F);
        writeAt<std::uint8_t>(filter, 0x39, 0x49);

        alignas(16) std::byte result[kResultSize]{};
        writeAt<float>(result, kFractionOffset, 1.0f);

        traceShape(manager, ray, &start, &end, filter, result);

        out.fraction = readAt<float>(result, kFractionOffset);
        out.valid = out.fraction >= 0.0f && out.fraction <= 1.0f;
        out.didHit = out.fraction < 1.0f;
        out.endPos = readAt<cs2::Vector>(result, kEndPosOffset);
        out.normal = readAt<cs2::Vector>(result, kNormalOffset);
        out.hitEntity = readAt<void*>(result, kHitEntityOffset);
        return out;
    }

    
    
    
    [[nodiscard]] static bool isVisible(const cs2::Vector& start, const cs2::Vector& end, void* skipEntity, void* targetEntity = nullptr) noexcept
    {
        const auto result = traceLine(start, end, skipEntity);
        return result.reaches(targetEntity);
    }

private:
    using TraceShapeFn = bool (*)(void* manager, void* ray, const cs2::Vector* start, const cs2::Vector* end, void* filter, void* result);
    using EntityToHandleFn = std::uint64_t (*)(void* entity);

    
    
    
    
    

    static constexpr int kValidationUnknown = 0;
    static constexpr int kValidationOk = 1;
    static constexpr int kValidationFailed = 2;

    static inline std::atomic<int> validationState{kValidationUnknown};

    
    
    
    
    
    
    static constexpr std::array<std::uint8_t, 4> kTraceShapePrefix{0x55, 0x48, 0x8D, 0x05};
    static constexpr std::array<std::uint8_t, 8> kTraceShapeAfterDisp{0x48, 0x89, 0xE5, 0x41, 0x57, 0x49, 0x89, 0xF7};
    static constexpr std::array<std::uint8_t, 16> kEntityToHandleSignature{
        0x48, 0x85, 0xFF, 0x74, 0x3B, 0x48, 0x8B, 0x57, 0x10, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x85};

    [[nodiscard]] static bool bytesMatch(std::uintptr_t address, const std::uint8_t* signature, std::size_t length) noexcept
    {
        return std::memcmp(reinterpret_cast<const void*>(address), signature, length) == 0;
    }

    [[nodiscard]] static bool runValidation(const tracing_sigs::Anchors& anchors) noexcept
    {
        const bool intact = bytesMatch(anchors.traceShape, kTraceShapePrefix.data(), kTraceShapePrefix.size())
            && bytesMatch(anchors.traceShape + 8, kTraceShapeAfterDisp.data(), kTraceShapeAfterDisp.size())
            && std::memcmp(reinterpret_cast<const void*>(anchors.entityToHandle), kEntityToHandleSignature.data(), kEntityToHandleSignature.size()) == 0;
        if (!intact) {
            StatusReport::record("Tracing: signatures drifted - traces fail closed", false);
            return false;
        }
        StatusReport::record("Tracing", true);
        return true;
    }

    [[nodiscard]] static const tracing_sigs::Anchors* resolvedAnchors() noexcept
    {
        auto state = validationState.load(std::memory_order_acquire);
        if (state == kValidationOk)
            return &tracing_sigs::resolved();
        if (state == kValidationFailed)
            return nullptr;
        const auto& anchors = tracing_sigs::resolved();
        state = anchors.ok && runValidation(anchors) ? kValidationOk : kValidationFailed;
        validationState.store(state, std::memory_order_release);
        return state == kValidationOk ? &anchors : nullptr;
    }

    [[nodiscard]] static void* managerPointer() noexcept
    {
        const auto* anchors = resolvedAnchors();
        if (!anchors || anchors->managerQword == 0)
            return nullptr;
        void* manager{};
        std::memcpy(&manager, reinterpret_cast<const void*>(anchors->managerQword), sizeof(manager));
        
        
        
        const std::uintptr_t moduleBase = static_cast<std::uintptr_t>(anchors->moduleBase);
        const auto asAddress = reinterpret_cast<std::uintptr_t>(manager);
        if (!manager || asAddress < moduleBase || asAddress >= moduleBase + tracing_sigs::kMaxModuleExtent)
            return nullptr;
        return manager;
    }

    template <typename T>
    static void writeAt(std::byte* buffer, std::size_t offset, const T& value) noexcept
    {
        std::memcpy(buffer + offset, &value, sizeof(T));
    }

    template <typename T>
    [[nodiscard]] static T readAt(const std::byte* buffer, std::size_t offset) noexcept
    {
        T value{};
        std::memcpy(&value, buffer + offset, sizeof(T));
        return value;
    }

    
    static constexpr std::uint64_t kDefaultMask = 0x1C3003;

    static constexpr std::size_t kRaySize = 48;
    static constexpr std::size_t kFilterSize = 0x50;
    static constexpr std::size_t kResultSize = 0x140;

    
    static constexpr std::size_t kHitEntityOffset = 0x08;
    static constexpr std::size_t kEndPosOffset = 0x84;   
    static constexpr std::size_t kNormalOffset = 0x90;   
    static constexpr std::size_t kFractionOffset = 0xAC; 
};