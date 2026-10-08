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
                return; 
            const auto bytes = static_cast<std::byte*>(sub) + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            std::int32_t current{};
            std::memcpy(&current, bytes, sizeof(current));
            const std::int32_t score = GET_CONFIG_VAR(FakePremierScore);
            
            
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

    static constexpr std::int8_t kPremierRankType = 11; 

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
