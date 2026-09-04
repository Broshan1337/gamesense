#pragma once

#include <cstring>

#include <CS2/Classes/CCvar.h>
#include <CS2/Classes/ConVar.h>
#include <MemoryPatterns/PatternTypes/CvarPatternTypes.h>
#include <MemoryPatterns/PatternTypes/ConVarPatternTypes.h>

template <typename HookContext>
class CvarSystem {
public:
    explicit CvarSystem(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] cs2::ConVar* findConVar(const char* name) const noexcept
    {
        const auto conVarList = getConVarList();
        if (!conVarList)
            return nullptr;

        for (auto i = conVarList->m_Head; i != conVarList->kInvalidIndex;) {
            const auto& node = conVarList->memory[i];
            const auto conVar = node.m_Element;
            if (std::strcmp(conVar->name, name) == 0)
                return conVar;
            i = node.m_Next;
        }
        return nullptr;
    }

    // Reads a float cvar by name at runtime - for game-balance values a feature must MATCH but never
    // register or write (e.g. sv_maxunlag for the lag-comp validity budget). {} if the cvar does not
    // exist or is not a float32.
    [[nodiscard]] std::optional<float> readFloatConVar(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return {};
        if (!hookContext.patternSearchResults().template get<OffsetToConVarValueType>().of(conVar).toOptional().equal(cs2::ConVarValueType::float32).valueOr(false))
            return {};
        return readValueAs<float>(conVar);
    }

    // The bool counterpart, for gating on a server setting a feature must MATCH but never write
    // (e.g. sv_quantize_movement_input for the quantized strafer). {} if absent or not a bool.
    [[nodiscard]] std::optional<bool> readBoolConVar(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return {};
        if (!hookContext.patternSearchResults().template get<OffsetToConVarValueType>().of(conVar).toOptional().equal(cs2::ConVarValueType::boolean).valueOr(false))
            return {};
        return readValueAs<bool>(conVar);
    }

    // The int32 counterpart to readFloatConVar (e.g. game_type / game_mode for the Discord RPC's
    // match-kind line). {} if the cvar does not exist or is not an int32.
    [[nodiscard]] std::optional<int> readIntConVar(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return {};
        if (!hookContext.patternSearchResults().template get<OffsetToConVarValueType>().of(conVar).toOptional().equal(cs2::ConVarValueType::int32).valueOr(false))
            return {};
        return readValueAs<int>(conVar);
    }

    // Forces a bool cvar to `value` every call - the write counterpart to readBoolConVar, used for
    // the FVA-style per-tick suppression of analysis cvars (cl_showusercmd / cl_pred_print_every_cmd
    // dump the exact fields the view-angle chains rewrite, straight into a log a reviewer could
    // diff). Same flow as the reader: find by name, verify the type slot through the resolved
    // pattern, then write through the resolved value pointer - never a blind offset. False means
    // "not found / not a bool / value pointer unresolved", i.e. nothing was touched.
    [[nodiscard]] bool forceBoolConVar(const char* name, bool value) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;
        if (!hookContext.patternSearchResults().template get<OffsetToConVarValueType>().of(conVar).toOptional().equal(cs2::ConVarValueType::boolean).valueOr(false))
            return false;

        const auto pointerToValue = hookContext.patternSearchResults().template get<OffsetToConVarValue>().of(conVar).get();
        if (!pointerToValue)
            return false;

        const bool written = value;
        std::memcpy(pointerToValue, &written, sizeof(written));
        return true;
    }

    // The float counterpart of forceBoolConVar, for post-process knobs a visual feature owns
    // (r_csgo_render_post_bloom_strength for the Sky Bloom look). Same contract: find by name,
    // verify the type slot through the resolved pattern, write through the resolved value
    // pointer. False = not found / not a float32 / value pointer unresolved - nothing touched.
    [[nodiscard]] bool forceFloatConVar(const char* name, float value) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;
        if (!hookContext.patternSearchResults().template get<OffsetToConVarValueType>().of(conVar).toOptional().equal(cs2::ConVarValueType::float32).valueOr(false))
            return false;

        const auto pointerToValue = hookContext.patternSearchResults().template get<OffsetToConVarValue>().of(conVar).get();
        if (!pointerToValue)
            return false;

        const float written = value;
        std::memcpy(pointerToValue, &written, sizeof(written));
        return true;
    }

    template <typename ConVarType>
    [[nodiscard]] auto getConVarValue() const
    {
        return getValue<ConVarType>(hookContext.getConVarsBase().template getConVar<ConVarType>());
    }

private:
    template <typename ConVarType>
    [[nodiscard]] std::optional<typename ConVarType::ValueType> getValue(cs2::ConVar* conVar) const
    {
        if (hookContext.patternSearchResults().template get<OffsetToConVarValueType>().of(conVar).toOptional().equal(conVarValueTypeForType<typename ConVarType::ValueType>()).valueOr(false))
            return readValueAs<typename ConVarType::ValueType>(conVar);
        return {};
    }

    template <typename T>
    [[nodiscard]] static consteval auto conVarValueTypeForType() noexcept
    {
        if constexpr (std::is_same_v<T, bool>)
            return cs2::ConVarValueType::boolean;
        else if constexpr (std::is_same_v<T, float>)
            return cs2::ConVarValueType::float32;
        else if constexpr (std::is_same_v<T, int>)
            return cs2::ConVarValueType::int32;
        else
            static_assert(!std::is_same_v<T, T>, "Unsupported type");
    }

    template <typename T>
    [[nodiscard]] std::optional<T> readValueAs(cs2::ConVar* conVar) const
    {
        const auto pointerToValue = hookContext.patternSearchResults().template get<OffsetToConVarValue>().of(conVar).get();
        if (!pointerToValue)
            return {};

        T value;
        std::memcpy(&value, pointerToValue, sizeof(value));
        return value;
    }

    [[nodiscard]] cs2::CCvar::ConVarList* getConVarList() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToConVarList>().of(cvar()).get();
    }

    [[nodiscard]] cs2::CCvar* cvar() const noexcept
    {
        if (const auto cvarPointer = hookContext.patternSearchResults().template get<CvarPointer>())
            return *cvarPointer;
        return nullptr;
    }

    HookContext& hookContext;
};
