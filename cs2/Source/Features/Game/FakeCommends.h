#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/PlayerRankingData.h>
#include <Features/Game/FakeCommendsConfigVariables.h>
#include <GameClient/Entities/PlayerController.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Makes OUR OWN client display chosen commend counters (leader / teacher / friendly) for the
// local player, in two places:
//
// 1. The ranking-data block (the member KeyValues tree the profile card reads): flag 0x8 +
//    the commends substruct (block+0x78) - the game formats "[f%d][t%d][l%d]" from the ints at
//    +0x18 (friendly) / +0x1C (teaching) / +0x20 (leader) into game/commends. Spoofing only
//    happens when the game's OWN substruct exists (never fabricated - death crash 2026-09-19).
// 2. The local controller's InventoryServices persona fields (m_nPersonaDataPublicCommends*) -
//    the networked copies other UI reads.
//
// Local and cosmetic, exactly like FakeLevel. Real commends are awarded by the Steam game
// coordinator (k_EMsgGCCStrike15_v2_ClientCommendPlayer), which is a different connection and
// not what this feature touches. The coordinator refreshes both targets, so the spoof is
// re-asserted every frame and the originals are restored on toggle-off / unload.
//
// Controller field offsets are resolved through the schema (update-proof) and cached once they
// resolve: onUnload() must not depend on the unload context's schema access.

template <typename HookContext>
class FakeCommends {
public:
    explicit FakeCommends(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        applyCommendsBlock();

        auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(hookContext.localPlayerController().baseEntity());
        if (!controllerEntity)
            return;

        const auto inventoryServicesOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_pInventoryServices");
        const auto leaderOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController_InventoryServices", "m_nPersonaDataPublicCommendsLeader");
        const auto teachingOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController_InventoryServices", "m_nPersonaDataPublicCommendsTeacher");
        const auto friendlyOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController_InventoryServices", "m_nPersonaDataPublicCommendsFriendly");
        if (!inventoryServicesOffset.has_value() || *inventoryServicesOffset <= 0
            || !leaderOffset.has_value() || *leaderOffset <= 0
            || !teachingOffset.has_value() || *teachingOffset <= 0
            || !friendlyOffset.has_value() || *friendlyOffset <= 0)
            return;

        cachedController = controllerEntity;
        cachedInventoryServicesOffset = *inventoryServicesOffset;
        cachedLeaderOffset = *leaderOffset;
        cachedTeachingOffset = *teachingOffset;
        cachedFriendlyOffset = *friendlyOffset;

        if (GET_CONFIG_VAR(FakeCommendsEnabled)) {
            captureOriginal(controllerEntity, *inventoryServicesOffset, { *leaderOffset, *teachingOffset, *friendlyOffset });
            apply(controllerEntity, *inventoryServicesOffset, { *leaderOffset, *teachingOffset, *friendlyOffset });
        } else if (hasOriginal) {
            restore(controllerEntity, *inventoryServicesOffset, { cachedLeaderOffset, cachedTeachingOffset, cachedFriendlyOffset });
        }
    }

    void onUnload() const noexcept
    {
        restoreCommendsBlock();
        if (hasOriginal && cachedController && cachedInventoryServicesOffset > 0
            && cachedLeaderOffset > 0 && cachedTeachingOffset > 0 && cachedFriendlyOffset > 0)
            restore(cachedController, cachedInventoryServicesOffset, { cachedLeaderOffset, cachedTeachingOffset, cachedFriendlyOffset });
    }

private:
    // Ranking-data block part (the profile card's game/commends source).
    //
    // CRASH RULE (2026-09-19 death crash): only spoof when the game's OWN commends substruct
    // exists (block+0x78). Installing our own zero-filled struct made the death-time
    // profile/scoreboard update walk unknown members of a 28-byte struct and crash on a null
    // container inside the block's string/clone machinery. Same rule as FakePremier: never set
    // the present-flag for data the game does not own.
    void applyCommendsBlock() const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;

        void* sub{};
        std::memcpy(&sub, block + cs2::PlayerRankingData::kCommendsSubstructOffset, sizeof(sub));
        cachedCommendsSub = sub;

        if (GET_CONFIG_VAR(FakeCommendsEnabled)) {
            if (!sub)
                return; // no game-owned substruct - no safe spoof surface
            auto* const bytes = static_cast<std::byte*>(sub);
            const std::int32_t friendly = GET_CONFIG_VAR(FakeCommendsFriendly);
            const std::int32_t teaching = GET_CONFIG_VAR(FakeCommendsTeaching);
            const std::int32_t leader = GET_CONFIG_VAR(FakeCommendsLeader);
            std::int32_t currentF{}, currentT{}, currentL{};
            std::memcpy(&currentF, bytes + cs2::PlayerRankingData::kCommendsSubstructFriendlyOffset, sizeof(currentF));
            std::memcpy(&currentT, bytes + cs2::PlayerRankingData::kCommendsSubstructTeachingOffset, sizeof(currentT));
            std::memcpy(&currentL, bytes + cs2::PlayerRankingData::kCommendsSubstructLeaderOffset, sizeof(currentL));
            if (!(hasBlockOriginal && currentF == friendly && currentT == teaching && currentL == leader)) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                blockOriginalHadFlag = (flags & cs2::PlayerRankingData::kCommendsPresentFlag) != 0;
                blockOriginalFriendly = currentF;
                blockOriginalTeaching = currentT;
                blockOriginalLeader = currentL;
                hasBlockOriginal = true;
            }
            std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructFriendlyOffset, &friendly, sizeof(friendly));
            std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructTeachingOffset, &teaching, sizeof(teaching));
            std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructLeaderOffset, &leader, sizeof(leader));
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags |= cs2::PlayerRankingData::kCommendsPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        } else if (hasBlockOriginal && cachedCommendsSub) {
            auto* const bytes = static_cast<std::byte*>(cachedCommendsSub);
            std::int32_t currentF{}, currentT{}, currentL{};
            std::memcpy(&currentF, bytes + cs2::PlayerRankingData::kCommendsSubstructFriendlyOffset, sizeof(currentF));
            std::memcpy(&currentT, bytes + cs2::PlayerRankingData::kCommendsSubstructTeachingOffset, sizeof(currentT));
            std::memcpy(&currentL, bytes + cs2::PlayerRankingData::kCommendsSubstructLeaderOffset, sizeof(currentL));
            if (currentF == static_cast<std::int32_t>(GET_CONFIG_VAR(FakeCommendsFriendly))
                && currentT == static_cast<std::int32_t>(GET_CONFIG_VAR(FakeCommendsTeaching))
                && currentL == static_cast<std::int32_t>(GET_CONFIG_VAR(FakeCommendsLeader))) {
                std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructFriendlyOffset, &blockOriginalFriendly, sizeof(blockOriginalFriendly));
                std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructTeachingOffset, &blockOriginalTeaching, sizeof(blockOriginalTeaching));
                std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructLeaderOffset, &blockOriginalLeader, sizeof(blockOriginalLeader));
            }
            if (!blockOriginalHadFlag) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                flags &= ~cs2::PlayerRankingData::kCommendsPresentFlag;
                std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
            }
            hasBlockOriginal = false;
        }
    }

    void restoreCommendsBlock() const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;
        if (hasBlockOriginal && cachedCommendsSub) {
            auto* const bytes = static_cast<std::byte*>(cachedCommendsSub);
            std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructFriendlyOffset, &blockOriginalFriendly, sizeof(blockOriginalFriendly));
            std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructTeachingOffset, &blockOriginalTeaching, sizeof(blockOriginalTeaching));
            std::memcpy(bytes + cs2::PlayerRankingData::kCommendsSubstructLeaderOffset, &blockOriginalLeader, sizeof(blockOriginalLeader));
            if (!blockOriginalHadFlag) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                flags &= ~cs2::PlayerRankingData::kCommendsPresentFlag;
                std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
            }
        }
        hasBlockOriginal = false;
    }
    struct Offsets {
        int leader;
        int teaching;
        int friendly;
    };

    [[nodiscard]] static std::byte* inventoryServicesOf(cs2::C_BaseEntity* controllerEntity, int inventoryServicesOffset) noexcept
    {
        const void* inventoryServices = nullptr;
        std::memcpy(&inventoryServices, reinterpret_cast<const std::byte*>(controllerEntity) + inventoryServicesOffset, sizeof(inventoryServices));
        return static_cast<std::byte*>(const_cast<void*>(inventoryServices));
    }

    void apply(cs2::C_BaseEntity* controllerEntity, int inventoryServicesOffset, Offsets offsets) const noexcept
    {
        auto* const services = inventoryServicesOf(controllerEntity, inventoryServicesOffset);
        if (!services)
            return;
        const std::int32_t friendly = GET_CONFIG_VAR(FakeCommendsFriendly);
        const std::int32_t teaching = GET_CONFIG_VAR(FakeCommendsTeaching);
        const std::int32_t leader = GET_CONFIG_VAR(FakeCommendsLeader);
        std::memcpy(services + offsets.friendly, &friendly, sizeof(friendly));
        std::memcpy(services + offsets.teaching, &teaching, sizeof(teaching));
        std::memcpy(services + offsets.leader, &leader, sizeof(leader));
    }

    // Saved so switching the feature off puts the real counters back. Captured only when what we
    // are looking at is not already our own spoof, so a server-side refresh updates the
    // originals instead of us saving our own lie.
    void captureOriginal(cs2::C_BaseEntity* controllerEntity, int inventoryServicesOffset, Offsets offsets) const noexcept
    {
        auto* const services = inventoryServicesOf(controllerEntity, inventoryServicesOffset);
        if (!services)
            return;
        std::int32_t friendly{}, teaching{}, leader{};
        std::memcpy(&friendly, services + offsets.friendly, sizeof(friendly));
        std::memcpy(&teaching, services + offsets.teaching, sizeof(teaching));
        std::memcpy(&leader, services + offsets.leader, sizeof(leader));
        if (hasOriginal
            && friendly == static_cast<std::int32_t>(GET_CONFIG_VAR(FakeCommendsFriendly))
            && teaching == static_cast<std::int32_t>(GET_CONFIG_VAR(FakeCommendsTeaching))
            && leader == static_cast<std::int32_t>(GET_CONFIG_VAR(FakeCommendsLeader)))
            return;

        originalFriendly = friendly;
        originalTeaching = teaching;
        originalLeader = leader;
        hasOriginal = true;
    }

    void restore(cs2::C_BaseEntity* controllerEntity, int inventoryServicesOffset, Offsets offsets) const noexcept
    {
        auto* const services = inventoryServicesOf(controllerEntity, inventoryServicesOffset);
        if (!services)
            return;
        std::memcpy(services + offsets.friendly, &originalFriendly, sizeof(originalFriendly));
        std::memcpy(services + offsets.teaching, &originalTeaching, sizeof(originalTeaching));
        std::memcpy(services + offsets.leader, &originalLeader, sizeof(originalLeader));
        hasOriginal = false;
    }

    inline static void* cachedCommendsSub{nullptr};
    inline static std::int32_t blockOriginalFriendly{};
    inline static std::int32_t blockOriginalTeaching{};
    inline static std::int32_t blockOriginalLeader{};
    inline static bool blockOriginalHadFlag{false};
    inline static bool hasBlockOriginal{false};

    inline static cs2::C_BaseEntity* cachedController{nullptr};
    inline static int cachedInventoryServicesOffset{0};
    inline static int cachedLeaderOffset{0};
    inline static int cachedTeachingOffset{0};
    inline static int cachedFriendlyOffset{0};
    inline static std::int32_t originalFriendly{};
    inline static std::int32_t originalTeaching{};
    inline static std::int32_t originalLeader{};
    inline static bool hasOriginal{false};

    HookContext& hookContext;
};
