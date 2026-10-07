#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/CEntityInstance.h>
#include <CS2/Classes/EntitySystem/CConcreteEntityList.h>
#include <CS2/Classes/EntitySystem/CEntityClass.h>
#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Classes/EntitySystem/CEntityIdentity.h>
#include <CS2/Classes/EntitySystem/CEntityIndex.h>
#include <CS2/Classes/EntitySystem/CGameEntitySystem.h>
#include <GameClient/EntitySystem/EntityIdentity.h>
#include <MemoryPatterns/PatternTypes/EntitySystemPatternTypes.h>
#include <UI/ImGui/GuiLog.h>
#include <ctime>

template <typename HookContext>
class EntitySystem {
public:
    explicit EntitySystem(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] decltype(auto) getEntityIdentityFromHandle(cs2::CEntityHandle handle) const noexcept
    {
        return hookContext.template make<EntityIdentity>(getRawEntityIdentityFromHandle(handle));
    }

    [[nodiscard]] cs2::CEntityInstance* getEntityFromHandle(cs2::CEntityHandle handle) const noexcept
    {
        if (const auto entityIdentity = getRawEntityIdentityFromHandle(handle))
            return entityIdentity->entity;
        return nullptr;
    }

    [[nodiscard]] decltype(auto) getEntityFromHandle2(cs2::CEntityHandle handle) const noexcept
    {
        return hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(getEntityFromHandle(handle)));
    }
    
    
    
    
    
    
    
    
    [[nodiscard]] cs2::CEntityInstance* getEntityFromIndex(cs2::CEntityIndex entityIndex) const noexcept
    {
        if (!entityIndex.isValid())
            return nullptr;

        const auto entityList = getEntityList();
        if (!entityList)
            return nullptr;

        
        
        const auto chunkIndex = entityIndex.value / cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk;
        auto* const chunk = entityList->chunks[chunkIndex];
        if (!chunk)
            return nullptr;

        const auto indexInChunk = entityIndex.value % cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk;
        auto& entityIdentity = (*chunk)[indexInChunk];
        if (!entityIdentity.entity || entityIdentity.handle.index().value != entityIndex.value)
            return nullptr;

        return entityIdentity.entity;
    }

    template <typename F>
    void forEachNetworkableEntityIdentity(F&& f) const noexcept
    {
        const auto entityList = getEntityList();

        
        
        
        
        {
            static std::int64_t lastDiagNs = 0;
            static std::uint32_t diagCount = 0;
            const bool first = lastDiagNs == 0;
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const std::int64_t nowNs = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
            if (first || nowNs - lastDiagNs > 10'000'000'000LL) {
                if (++diagCount <= 12) {   
                    lastDiagNs = nowNs;
                    const auto offset = hookContext.patternSearchResults().template get<EntityListOffset>();
                    const auto es = entitySystem();
                    gui_log::write("[chaindiag] getEntityList: es=%p offset=%d list=%p",
                        reinterpret_cast<const void*>(es),
                        offset ? static_cast<int>(offset.rawOffset()) : -1,
                        reinterpret_cast<const void*>(entityList));
                }
            }
        }
        if (!entityList)
            return;

        
        
        
        
        
        
        
        
        
        
        {
            static int strideVerdict = 0;   
            static bool inconclusiveLogged = false;
            if (strideVerdict == 0) {
                if (const auto* const chunk0 = entityList->chunks[0]) {
                    auto matches = [chunk0](std::size_t stride) {
                        int hits = 0;
                        for (std::size_t i = 0; i < 4; ++i) {
                            const auto& identity = *reinterpret_cast<const cs2::CEntityIdentity*>(
                                reinterpret_cast<const std::byte*>(chunk0) + i * stride);
                            if (identity.entity && identity.handle.index().value == static_cast<int>(i))
                                ++hits;
                        }
                        return hits;
                    };
                    const int hits112 = matches(112);
                    const int hits120 = matches(120);
                    if (hits112 == 4 && hits120 != 4) {
                        strideVerdict = 112;
                        gui_log::write("[chaindiag] identity stride probe: 112 (sizeof) confirmed, 4/4 handle matches");
                    } else if (hits120 == 4 && hits112 != 4) {
                        strideVerdict = 120;
                        gui_log::write("[chaindiag] identity stride probe: 120 - sizeof(CEntityIdentity)=112 IS WRONG, "
                                       "identities past slot 0 are misread; stop and re-derive the layout");
                    } else if (!inconclusiveLogged) {
                        inconclusiveLogged = true;
                        gui_log::write("[chaindiag] identity stride probe inconclusive (hits112=%d hits120=%d), "
                                       "will retry while slots stay empty", hits112, hits120);
                    }
                }
            }
        }

        for (auto chunkIndex = 0; chunkIndex < cs2::CConcreteEntityList::kNumberOfNetworkableEntityChunks; ++chunkIndex) {
            const auto* const chunk = entityList->chunks[chunkIndex];
            if (!chunk)
                continue;

            for (auto indexInChunk = 0; indexInChunk < cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk; ++indexInChunk) {
                if (const auto& entityIdentity = (*chunk)[indexInChunk]; entityIdentity.entity)
                    f(entityIdentity);
            }
        }
    }

    [[nodiscard]] cs2::CEntityClass* findEntityClass(const char* className) const noexcept
    {
        const auto entityClasses = getEntityClasses();
        if (!entityClasses)
            return nullptr;

        
        
        
        
        const auto count = std::min<std::uint32_t>(entityClasses->numElements, kMaxEntityClassScan);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (std::strcmp(entityClasses->memory[i].key, className) == 0)
                return entityClasses->memory[i].value;
        }
        return nullptr;
    }

    
    
    
    
    
    [[nodiscard]] const char* entityClassNameForIndex(int entityIndex) const noexcept
    {
        const cs2::CEntityIndex index{entityIndex};
        if (!index.isValid())
            return nullptr;

        const auto entityList = getEntityList();
        if (!entityList)
            return nullptr;

        const auto chunkIndex = index.value / cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk;
        auto* const chunk = entityList->chunks[chunkIndex];
        if (!chunk)
            return nullptr;

        const auto indexInChunk = index.value % cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk;
        auto& entityIdentity = (*chunk)[indexInChunk];
        if (!entityIdentity.entity || entityIdentity.handle.index().value != index.value)
            return nullptr;

        const auto entityClasses = getEntityClasses();
        if (!entityClasses)
            return nullptr;

        const auto count = std::min<std::uint32_t>(entityClasses->numElements, kMaxEntityClassScan);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (entityClasses->memory[i].value == entityIdentity.entityClass)
                return entityClasses->memory[i].key;
        }
        return nullptr;
    }

    
    
    static constexpr std::uint32_t kMaxEntityClassScan = 4096;

private:
    [[nodiscard]] cs2::CEntityIdentity* getRawEntityIdentityFromHandle(cs2::CEntityHandle handle) const noexcept
    {
        const auto entityIndex = handle.index();
        if (!entityIndex.isValid())
            return nullptr;

        const auto entityList = getEntityList();
        if (!entityList)
            return nullptr;

        const auto chunkIndex = entityIndex.value / cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk;
        if (auto* const chunk = entityList->chunks[chunkIndex]) {
            const auto indexInChunk = entityIndex.value % cs2::CConcreteEntityList::kNumberOfIdentitiesPerChunk;
            if (auto& entityIndentity = (*chunk)[indexInChunk]; entityIndentity.handle == handle)
                return &entityIndentity;
        }
        return nullptr;
    }

    [[nodiscard]] cs2::CGameEntitySystem* entitySystem() const noexcept
    {
        if (hookContext.patternSearchResults().template get<EntitySystemPointer>())
            return *hookContext.patternSearchResults().template get<EntitySystemPointer>();
        return nullptr;
    }

    [[nodiscard]] auto getEntityList() const noexcept
    {
        
        
        
        
        
        
        
        const auto offset = hookContext.patternSearchResults().template get<EntityListOffset>();
        if (!offset || offset.rawOffset() <= 0 || !entitySystem())
            return static_cast<cs2::CConcreteEntityList*>(nullptr);
        return reinterpret_cast<cs2::CConcreteEntityList*>(
            reinterpret_cast<std::uintptr_t>(entitySystem()) + offset.rawOffset());
    }

    [[nodiscard]] auto getEntityClasses() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToEntityClasses>().of(entitySystem()).get();
    }

    HookContext& hookContext;
};
