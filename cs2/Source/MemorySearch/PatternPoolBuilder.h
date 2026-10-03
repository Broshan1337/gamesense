#pragma once

#include <Utils/StrongTypeAlias.h>
#include <Utils/TypeList.h>

#include "CodePattern.h"
#include "CodePatternOperation.h"
#include "PatternStringWildcard.h"

template <typename TempPatternPool, typename PatternTypesList = TypeList<>>
class PatternPoolBuilder {
public:
    TempPatternPool tempPool;

    using PatternTypes = PatternTypesList;

    [[nodiscard]] consteval auto addPatterns(auto f) noexcept
    {
        return f(*this);
    }

    template <typename PatternType, CodePattern Pattern>
    [[nodiscard]] consteval auto addPattern() noexcept
    {
        static_assert(sizeof(UnpackStrongTypeAliasT<PatternType>) == 8 || Pattern.operation == CodePatternOperation::Read || Pattern.operation == CodePatternOperation::Read8, "Incorrect result size, missing .read()/.read8() in pattern declaration?");
        static_assert(hasWildcardsAtReadOffset(Pattern, sizeof(UnpackStrongTypeAliasT<PatternType>)), "Pattern does not have wildcards at the offset to read, incorrect offset in .add() ?");
        tempPool.template addPattern<Pattern>();
        return PatternPoolBuilder<TempPatternPool, typename PatternTypesList::template add<PatternType>>{tempPool};
    }

private:
    [[nodiscard]] static constexpr bool hasWildcardsAtReadOffset(const auto& pattern, std::size_t typeSize)
    {
        if (pattern.operation != CodePatternOperation::Read && pattern.operation != CodePatternOperation::Read8)
            return true;

        const auto bytesToCheck = pattern.operation == CodePatternOperation::Read8 ? std::size_t{1} : typeSize;
        for (std::size_t i = 0; i < bytesToCheck; ++i) {
            if (pattern.storage.pattern[pattern.offset + i] != kPatternStringWildcard)
                return false;
        }
        return true;
    }
};
