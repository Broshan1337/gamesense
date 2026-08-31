#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/PlayerRankingData.h>
#include <Features/Game/FakeLevelConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Makes OUR OWN client display a chosen profile rank.
//
// Local and cosmetic, exactly like FakePrime: it edits the client's own copy of its ranking data so
// the client's own UI reads it back. Nothing is sent anywhere, and the server keeps its own record -
// this cannot change what anyone else sees.
//
// Rewritten every frame because the game coordinator refreshes this block, which would otherwise
// quietly revert it.
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

        // The experience bar is set to the start of the chosen level rather than left alone,
        // because a level and an XP total from different ranks render as a bar that disagrees with
        // the badge next to it.
        write(rankingData, cs2::PlayerRankingData::kExperienceOffset, kExperienceBase);

        // Both present-flags, without which the game reports zero for these regardless of what the
        // fields hold. This is the step the reference snippet leaves out.
        std::uint32_t flags{};
        std::memcpy(&flags, rankingData + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
        flags |= cs2::PlayerRankingData::kLevelPresentFlag | cs2::PlayerRankingData::kExperiencePresentFlag;
        std::memcpy(rankingData + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
    }

    // Saved so that switching the feature off puts the real rank back rather than leaving the last
    // spoofed one sitting there. Captured only when what we are looking at is not already our own
    // value, so a refresh from the game coordinator updates it instead of us saving our own lie.
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

    // A round figure rather than a computed per-level threshold: the exact XP curve is not worth
    // reversing for a cosmetic bar, and any value inside a level renders the same badge.
    static constexpr std::uint32_t kExperienceBase = 0;

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static std::uint32_t originalLevel{};
    inline static std::uint32_t originalExperience{};
    inline static std::uint32_t originalFlags{};
    inline static bool hasOriginal{false};

    HookContext& hookContext;
};
