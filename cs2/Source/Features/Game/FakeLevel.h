#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/PlayerRankingData.h>
#include <Features/Game/FakeLevelConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>









template <typename HookContext>
class FakeLevel {
public:
    explicit FakeLevel(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        auto* const rankingData = findRankingData();
        if (!rankingData)
            return;

        if (GET_CONFIG_VAR(FakeLevelEnabled)) {
            captureOriginal(rankingData);
            apply(rankingData);
        } else if (hasOriginal) {
            restore(rankingData);
        }
    }

    void onUnload() const noexcept
    {
        if (!hasOriginal)
            return;

        if (auto* const rankingData = findRankingData())
            restore(rankingData);
    }

private:
    [[nodiscard]] std::byte* findRankingData() const noexcept
    {
        return static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
    }

    void apply(std::byte* rankingData) const noexcept
    {
        const std::uint32_t level = GET_CONFIG_VAR(FakeLevelValue);
        write(rankingData, cs2::PlayerRankingData::kLevelOffset, level);

        
        
        const std::uint32_t xp = GET_CONFIG_VAR(FakeLevelXp);
        write(rankingData, cs2::PlayerRankingData::kExperienceOffset, xp);

        
        
        std::uint32_t flags{};
        std::memcpy(&flags, rankingData + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
        flags |= cs2::PlayerRankingData::kLevelPresentFlag | cs2::PlayerRankingData::kExperiencePresentFlag;
        std::memcpy(rankingData + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
    }

    
    
    
    void captureOriginal(std::byte* rankingData) const noexcept
    {
        std::uint32_t level{};
        std::memcpy(&level, rankingData + cs2::PlayerRankingData::kLevelOffset, sizeof(level));
        if (hasOriginal && level == static_cast<std::uint32_t>(GET_CONFIG_VAR(FakeLevelValue)))
            return;

        originalLevel = level;
        std::memcpy(&originalExperience, rankingData + cs2::PlayerRankingData::kExperienceOffset, sizeof(originalExperience));
        std::memcpy(&originalFlags, rankingData + cs2::PlayerRankingData::kFlagsOffset, sizeof(originalFlags));
        hasOriginal = true;
    }

    void restore(std::byte* rankingData) const noexcept
    {
        write(rankingData, cs2::PlayerRankingData::kLevelOffset, originalLevel);
        write(rankingData, cs2::PlayerRankingData::kExperienceOffset, originalExperience);
        write(rankingData, cs2::PlayerRankingData::kFlagsOffset, originalFlags);
        hasOriginal = false;
    }

    static void write(std::byte* rankingData, int offset, std::uint32_t value) noexcept
    {
        std::memcpy(rankingData + offset, &value, sizeof(value));
    }

    
    inline static std::uint32_t originalLevel{};
    inline static std::uint32_t originalExperience{};
    inline static std::uint32_t originalFlags{};
    inline static bool hasOriginal{false};

    HookContext& hookContext;
};
