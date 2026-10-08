#pragma once

#include <cstdint>
#include <cstring>
#include <optional>

#include <CS2/Classes/C_CSGameRules.h>
#include <MemoryPatterns/PatternTypes/GameRulesPatternTypes.h>

template <typename HookContext>
class GameRules {
public:
    GameRules(HookContext& hookContext, cs2::C_CSGameRules* gameRules) noexcept
        : hookContext{hookContext}
        , gameRules{gameRules}
    {
    }

    [[nodiscard]] auto roundStartTime() const noexcept
    {
        return hookContext.patternSearchResults().template get<RoundStartTimeOffset>().of(gameRules).toOptional();
    }

    [[nodiscard]] auto roundRestartTime() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToRoundRestartTime>().of(gameRules).toOptional();
    }

    [[nodiscard]] bool hasScheduledRoundRestart() const noexcept
    {
        return roundRestartTime().greaterThan(0.0f).valueOr(false);
    }

    [[nodiscard]] auto timeToRoundRestart() const noexcept
    {
        return roundRestartTime() - hookContext.globalVars().curtime();
    }

    [[nodiscard]] auto roundEndTime() const noexcept
    {
        return roundStartTime() + roundLength();
    }

    [[nodiscard]] auto isRoundOver() const
    {
        return roundWinStatus().notEqual(cs2::RoundWinStatus::None);
    }

    
    
    
    [[nodiscard]] std::optional<bool> isValveDs() const noexcept
    {
        if (!gameRules)
            return {};
        std::uint8_t value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(gameRules) + cs2::C_CSGameRules::kIsValveDsOffset, sizeof(value));
        return value != 0;
    }

    
    
    [[nodiscard]] bool spoofValveDs(bool spoofedValue) const noexcept
    {
        if (!gameRules)
            return false;
        auto* const at = reinterpret_cast<std::byte*>(gameRules) + cs2::C_CSGameRules::kIsValveDsOffset;
        std::uint8_t current{};
        std::memcpy(&current, at, sizeof(current));
        const std::uint8_t target = spoofedValue ? 1 : 0;
        if (current == target)
            return false;
        std::memcpy(at, &target, sizeof(target));
        return true;
    }

    
    
    [[nodiscard]] std::optional<bool> isFreezePeriod() const noexcept
    {
        return schemaBool("m_bFreezePeriod");
    }

    [[nodiscard]] std::optional<bool> isWarmupPeriod() const noexcept
    {
        return schemaBool("m_bWarmupPeriod");
    }

private:
    [[nodiscard]] std::optional<bool> schemaBool(const char* fieldName) const noexcept
    {
        if (!gameRules)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSGameRules", fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        std::uint8_t value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(gameRules) + *offset, sizeof(value));
        return value != 0;
    }

    [[nodiscard]] auto roundWinStatus() const
    {
        return hookContext.patternSearchResults().template get<OffsetToRoundWinStatus>().of(gameRules).toOptional();
    }

    [[nodiscard]] auto roundLength() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToRoundLength>().of(gameRules).toOptional();
    }

    HookContext& hookContext;
    cs2::C_CSGameRules* gameRules;
};
