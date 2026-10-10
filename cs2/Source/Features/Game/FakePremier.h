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
        const bool premier = GET_CONFIG_VAR(FakePremierEnabled);
        const bool wingman = GET_CONFIG_VAR(FakeWingmanEnabled);
        // The controller carries ONE ranking slot - wingman takes the slot when both are
        // enabled (the two features are mutually exclusive by construction).
        const bool wingmanMode = wingman;

        applyRankingBlock(premier, wingmanMode);

        auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(hookContext.localPlayerController().baseEntity());
        if (!controllerEntity)
            return;

        const auto rankingOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRanking");
        const auto rankTypeOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRankType");
        const auto winsOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveWins");
        if (!rankingOffset.has_value() || *rankingOffset <= 0 || !rankTypeOffset.has_value() || *rankTypeOffset <= 0 || !winsOffset.has_value() || *winsOffset <= 0)
            return;

        cachedController = controllerEntity;
        cachedRankingOffset = *rankingOffset;
        cachedRankTypeOffset = *rankTypeOffset;
        cachedWinsOffset = *winsOffset;

        if (premier || wingmanMode) {
            captureOriginal(controllerEntity, *rankingOffset, *rankTypeOffset, *winsOffset);
            apply(controllerEntity, *rankingOffset, *rankTypeOffset, *winsOffset, premier, wingmanMode);
        } else if (hasOriginal) {
            restore(controllerEntity, *rankingOffset, *rankTypeOffset, *winsOffset);
        }
    }

    void onUnload() const noexcept
    {
        if (hasOriginal && cachedController && cachedRankingOffset > 0 && cachedRankTypeOffset > 0 && cachedWinsOffset > 0)
            restore(cachedController, cachedRankingOffset, cachedRankTypeOffset, cachedWinsOffset);
        restoreRankingBlock();
    }

private:
    
    
    
    
    
    
    
    void applyRankingBlock(bool premier, bool wingmanMode) const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;

        void* sub{};
        std::memcpy(&sub, block + cs2::PlayerRankingData::kRankingSubstructOffset, sizeof(sub));
        cachedRankingSub = sub;

        if (premier || wingmanMode) {
            if (!sub)
                return; 
            auto* const subBytes = static_cast<std::byte*>(sub);
            const auto rankingBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            const auto winsBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructWinsOffset;
            std::int32_t currentRanking{};
            std::memcpy(&currentRanking, rankingBytes, sizeof(currentRanking));
            std::int32_t currentWins{};
            std::memcpy(&currentWins, winsBytes, sizeof(currentWins));
            const std::int32_t score = wingmanMode
                ? static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank))
                : static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore));
            // Premier mode: also satisfy the >= 10 competitive-wins gate or the UI keeps
            // the skill group hidden regardless of the score.
            const std::int32_t wins = premier && GET_CONFIG_VAR(FakePremierWins)
                ? static_cast<std::int32_t>(GET_CONFIG_VAR(PremierWins))
                : currentWins;
            
            if (!(hasBlockOriginal && currentRanking == score && (!premier || wins == currentWins))) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                blockOriginalHadFlag = (flags & cs2::PlayerRankingData::kRankingPresentFlag) != 0;
                blockOriginalRanking = currentRanking;
                blockOriginalWins = currentWins;
                hasBlockOriginal = true;
            }
            std::memcpy(rankingBytes, &score, sizeof(score));
            if (premier)
                std::memcpy(winsBytes, &wins, sizeof(wins));
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags |= cs2::PlayerRankingData::kRankingPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        } else if (hasBlockOriginal && cachedRankingSub) {
            auto* const subBytes = static_cast<std::byte*>(cachedRankingSub);
            const auto rankingBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            const auto winsBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructWinsOffset;
            std::int32_t currentRanking{};
            std::memcpy(&currentRanking, rankingBytes, sizeof(currentRanking));
            std::int32_t currentWins{};
            std::memcpy(&currentWins, winsBytes, sizeof(currentWins));
            if (currentRanking == static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore))
                || currentRanking == static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank))) {
                std::memcpy(rankingBytes, &blockOriginalRanking, sizeof(blockOriginalRanking));
                std::memcpy(winsBytes, &blockOriginalWins, sizeof(blockOriginalWins));
            }
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
            auto* const subBytes = static_cast<std::byte*>(cachedRankingSub);
            std::memcpy(subBytes + cs2::PlayerRankingData::kRankingSubstructRankingOffset, &blockOriginalRanking, sizeof(blockOriginalRanking));
            std::memcpy(subBytes + cs2::PlayerRankingData::kRankingSubstructWinsOffset, &blockOriginalWins, sizeof(blockOriginalWins));
        }
        if (hasBlockOriginal && !blockOriginalHadFlag) {
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags &= ~cs2::PlayerRankingData::kRankingPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        }
        hasBlockOriginal = false;
    }
    void apply(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset, int winsOffset, bool premier, bool wingmanMode) const noexcept
    {
        const auto bytes = reinterpret_cast<std::byte*>(controllerEntity);
        if (wingmanMode) {
            const std::int32_t rank = static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank));
            std::memcpy(bytes + rankingOffset, &rank, sizeof(rank));
            const std::int8_t rankType = kWingmanRankType;
            std::memcpy(bytes + rankTypeOffset, &rankType, sizeof(rankType));
            return;
        }
        const std::int32_t score = GET_CONFIG_VAR(FakePremierScore);
        std::memcpy(bytes + rankingOffset, &score, sizeof(score));
        const std::int8_t premierRankType = kPremierRankType;
        std::memcpy(bytes + rankTypeOffset, &premierRankType, sizeof(premierRankType));
        // The competitive-wins gate on the controller side (schema m_iCompetitiveWins):
        // keep the real value unless the wins spoof is on.
        std::int32_t wins{};
        std::memcpy(&wins, bytes + winsOffset, sizeof(wins));
        const std::int32_t spoofedWins = GET_CONFIG_VAR(FakePremierWins)
            ? static_cast<std::int32_t>(GET_CONFIG_VAR(PremierWins))
            : wins;
        std::memcpy(bytes + winsOffset, &spoofedWins, sizeof(spoofedWins));
    }

    
    
    
    void captureOriginal(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset, int winsOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<const std::byte*>(controllerEntity);
        std::int32_t ranking{};
        std::memcpy(&ranking, bytes + rankingOffset, sizeof(ranking));
        std::int8_t rankType{};
        std::memcpy(&rankType, bytes + rankTypeOffset, sizeof(rankType));
        std::int32_t wins{};
        std::memcpy(&wins, bytes + winsOffset, sizeof(wins));
        if (hasOriginal && rankType == kPremierRankType && ranking == static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore)))
            return;
        if (hasOriginal && rankType == kWingmanRankType && ranking == static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank)))
            return;

        originalRanking = ranking;
        originalRankType = rankType;
        originalWins = wins;
        hasOriginal = true;
    }

    void restore(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset, int winsOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<std::byte*>(controllerEntity);
        std::memcpy(bytes + rankingOffset, &originalRanking, sizeof(originalRanking));
        std::memcpy(bytes + rankTypeOffset, &originalRankType, sizeof(originalRankType));
        std::memcpy(bytes + winsOffset, &originalWins, sizeof(originalWins));
        hasOriginal = false;
    }

    static constexpr std::int8_t kPremierRankType = 11; 
    // The game's own rankType jump table (0x1EE5540 on 11106093): 7=Wingman, 8=ScrimComp2v2,
    // 9=ScrimComp5v5, 10=DangerZone, 11=Premier, 12=Competitive.
    static constexpr std::int8_t kWingmanRankType = 7;
    // The premier skill group is gated behind >= 10 competitive wins - below that the UI
    // hides the rank no matter what the score says.
    static constexpr std::int32_t kMinPremierWins = 10;

    inline static cs2::C_BaseEntity* cachedController{nullptr};
    inline static int cachedRankingOffset{0};
    inline static int cachedRankTypeOffset{0};
    inline static int cachedWinsOffset{0};
    inline static std::int32_t originalRanking{};
    inline static std::int8_t originalRankType{};
    inline static std::int32_t originalWins{};
    inline static bool hasOriginal{false};

    inline static void* cachedRankingSub{nullptr};
    inline static std::int32_t blockOriginalRanking{};
    inline static std::int32_t blockOriginalWins{};
    inline static bool blockOriginalHadFlag{false};
    inline static bool hasBlockOriginal{false};

    HookContext& hookContext;
};
