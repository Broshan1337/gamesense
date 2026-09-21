#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/PlayerRankingData.h>
#include <Features/Game/FakePremierConfigVariables.h>
#include <GameClient/Entities/PlayerController.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Makes OUR OWN client display a chosen premier rating for the local player, in two places:
//
// 1. The ranking-data block (the member KeyValues tree the profile card / lobby UI reads):
//    flag 0x4 + the ranking substruct (block+0x70 -> ranking int at +0x3C) publish game/ranking.
//    Requires the game's own ranking substruct to exist (GC ranking data); skipped otherwise.
// 2. The local controller's competitive-rank fields (m_iCompetitiveRanking + rank type 11,
//    11 = premier) - what our player list's rank column reads.
//
// Local and cosmetic, exactly like FakeLevel. Nothing is sent anywhere; the game coordinator
// refreshes both targets, which is why the spoof re-asserts every frame and the originals are
// restored when the feature is switched off or the module unloads.
//
// Controller field offsets are resolved through the schema (update-proof) and cached once they
// resolve: onUnload() must not depend on the unload context's schema access.

template <typename HookContext>
class FakePremier {
public:
    explicit FakePremier(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        applyRankingBlock();

        auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(hookContext.localPlayerController().baseEntity());
        if (!controllerEntity)
            return;

        const auto rankingOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRanking");
        const auto rankTypeOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRankType");
        if (!rankingOffset.has_value() || *rankingOffset <= 0 || !rankTypeOffset.has_value() || *rankTypeOffset <= 0)
            return;

        cachedController = controllerEntity;
        cachedRankingOffset = *rankingOffset;
        cachedRankTypeOffset = *rankTypeOffset;

        if (GET_CONFIG_VAR(FakePremierEnabled)) {
            captureOriginal(controllerEntity, *rankingOffset, *rankTypeOffset);
            apply(controllerEntity, *rankingOffset, *rankTypeOffset);
        } else if (hasOriginal) {
            restore(controllerEntity, *rankingOffset, *rankTypeOffset);
        }
    }

    void onUnload() const noexcept
    {
        if (hasOriginal && cachedController && cachedRankingOffset > 0 && cachedRankTypeOffset > 0)
            restore(cachedController, cachedRankingOffset, cachedRankTypeOffset);
        restoreRankingBlock();
    }

private:
    // Ranking-data block part (the profile card's game/ranking source).
    //
    // CRASH RULE (2026-09-19 death crash): the flag bit MUST only be set when the game's own
    // ranking substruct exists. Setting flag 0x4 while block+0x70 is null makes block consumers
    // dereference structurally invalid state (the death-time profile/scoreboard update crashed
    // in the block's string/clone machinery on a null container). Fabricating our own substruct
    // is equally forbidden - the layout beyond the three ints the publisher reads is unknown.
    void applyRankingBlock() const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;

        void* sub{};
        std::memcpy(&sub, block + cs2::PlayerRankingData::kRankingSubstructOffset, sizeof(sub));
        cachedRankingSub = sub;

        if (GET_CONFIG_VAR(FakePremierEnabled)) {
            if (!sub)
                return; // no game-owned substruct - no safe spoof surface
            const auto bytes = static_cast<std::byte*>(sub) + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            std::int32_t current{};
            std::memcpy(&current, bytes, sizeof(current));
            const std::int32_t score = GET_CONFIG_VAR(FakePremierScore);
            // capture the real value (and the original flag bit) whenever what we see is not
            // already our own spoof, so a refresh from the game coordinator updates the original
            if (!(hasBlockOriginal && current == score)) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                blockOriginalHadFlag = (flags & cs2::PlayerRankingData::kRankingPresentFlag) != 0;
                blockOriginalRanking = current;
                hasBlockOriginal = true;
            }
            std::memcpy(bytes, &score, sizeof(score));
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags |= cs2::PlayerRankingData::kRankingPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        } else if (hasBlockOriginal && cachedRankingSub) {
            const auto bytes = static_cast<std::byte*>(cachedRankingSub) + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            std::int32_t current{};
            std::memcpy(&current, bytes, sizeof(current));
            if (current == static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore)))
                std::memcpy(bytes, &blockOriginalRanking, sizeof(blockOriginalRanking));
            if (!blockOriginalHadFlag) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                flags &= ~cs2::PlayerRankingData::kRankingPresentFlag;
                std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
            }
            hasBlockOriginal = false;
        }
    }

    void restoreRankingBlock() const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;
        if (hasBlockOriginal && cachedRankingSub) {
            const auto bytes = static_cast<std::byte*>(cachedRankingSub) + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            std::memcpy(bytes, &blockOriginalRanking, sizeof(blockOriginalRanking));
        }
        if (hasBlockOriginal && !blockOriginalHadFlag) {
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags &= ~cs2::PlayerRankingData::kRankingPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        }
        hasBlockOriginal = false;
    }
    void apply(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<std::byte*>(controllerEntity);
        const std::int32_t score = GET_CONFIG_VAR(FakePremierScore);
        std::memcpy(bytes + rankingOffset, &score, sizeof(score));
        const std::int8_t premierRankType = kPremierRankType;
        std::memcpy(bytes + rankTypeOffset, &premierRankType, sizeof(premierRankType));
    }

    // Saved so switching the feature off puts the real rank back. Captured only when what we
    // are looking at is not already our own spoof, so a server-side refresh updates the
    // originals instead of us saving our own lie.
    void captureOriginal(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<const std::byte*>(controllerEntity);
        std::int32_t ranking{};
        std::memcpy(&ranking, bytes + rankingOffset, sizeof(ranking));
        std::int8_t rankType{};
        std::memcpy(&rankType, bytes + rankTypeOffset, sizeof(rankType));
        if (hasOriginal && rankType == kPremierRankType && ranking == static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore)))
            return;

        originalRanking = ranking;
        originalRankType = rankType;
        hasOriginal = true;
    }

    void restore(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<std::byte*>(controllerEntity);
        std::memcpy(bytes + rankingOffset, &originalRanking, sizeof(originalRanking));
        std::memcpy(bytes + rankTypeOffset, &originalRankType, sizeof(originalRankType));
        hasOriginal = false;
    }

    static constexpr std::int8_t kPremierRankType = 11; // 0xb - the rank type that reads m_iCompetitiveRanking as a premier rating

    inline static cs2::C_BaseEntity* cachedController{nullptr};
    inline static int cachedRankingOffset{0};
    inline static int cachedRankTypeOffset{0};
    inline static std::int32_t originalRanking{};
    inline static std::int8_t originalRankType{};
    inline static bool hasOriginal{false};

    inline static void* cachedRankingSub{nullptr};
    inline static std::int32_t blockOriginalRanking{};
    inline static bool blockOriginalHadFlag{false};
    inline static bool hasBlockOriginal{false};

    HookContext& hookContext;
};
