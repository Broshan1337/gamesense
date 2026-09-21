#pragma once

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
    
    // Looks an entity up by bare index, for the cases where the game hands us one instead of a
    // handle - m_iIDEntIndex (the entity under the crosshair) being the reason this exists.
    //
    // Unlike the handle lookup there is no serial number to check against, so a stale index can
    // only be caught by confirming the slot actually holds an entity whose own handle agrees with
    // the index we asked for. That is what stops a recycled slot from being read as the entity that
    // used to live in it.
    [[nodiscard]] cs2::CEntityInstance* getEntityFromIndex(cs2::CEntityIndex entityIndex) const noexcept
    {
        if (!entityIndex.isValid())
            return nullptr;

        const auto entityList = getEntityList();
        if (!entityList)
            return nullptr;

        // kMaxValidEntityIndex (0x7FFE) is one short of kNumberOfChunks * kNumberOfIdentitiesPerChunk
        // (64 * 512), so isValid() above already bounds this to a real chunk.
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
        if (!entityList)
            return;

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

        for (int i = 0; i < entityClasses->numElements; ++i) {
            if (std::strcmp(entityClasses->memory[i].key, className) == 0)
                return entityClasses->memory[i].value;
        }
        return nullptr;
    }

    // Most-derived schema class name for a bare entity index ("C_CSPlayerPawn" etc.): the
    // identity's class pointer reverse-looked-up in the game's own entity-class map (the same
    // map findEntityClass searches forward - no new RE, no baked-in hierarchy). Null when the
    // index is stale/recycled or the map is unavailable. The returned pointer is game-owned
    // map storage - copy it before the next frame.
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

        for (int i = 0; i < entityClasses->numElements; ++i) {
            if (entityClasses->memory[i].value == entityIdentity.entityClass)
                return entityClasses->memory[i].key;
        }
        return nullptr;
    }

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
        return hookContext.patternSearchResults().template get<EntityListOffset>().of(entitySystem()).get();
    }

    [[nodiscard]] auto getEntityClasses() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToEntityClasses>().of(entitySystem()).get();
    }

    HookContext& hookContext;
};
