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

    
    
    
    
    
    
    [[nodiscard]] bool conVarTypeIs(cs2::ConVar* conVar, cs2::ConVarValueType type) const noexcept
    {
        constexpr std::uintptr_t kTypeOffset = 0x28;
        const auto value = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(conVar) + kTypeOffset);
        if (value > static_cast<std::uint32_t>(cs2::ConVarValueType::string))
            return false; 
        return value == static_cast<std::uint32_t>(type);
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

    
    
    
    [[nodiscard]] std::optional<float> readFloatConVar(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return {};
        if (!conVarTypeIs(conVar, cs2::ConVarValueType::float32))
            return {};
        return readValueAs<float>(conVar);
    }

    
    
    [[nodiscard]] std::optional<bool> readBoolConVar(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return {};
        if (!conVarTypeIs(conVar, cs2::ConVarValueType::boolean))
            return {};
        return readValueAs<bool>(conVar);
    }

    
    
    [[nodiscard]] std::optional<int> readIntConVar(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return {};
        if (!conVarTypeIs(conVar, cs2::ConVarValueType::int32))
            return {};
        return readValueAs<int>(conVar);
    }

    
    
    
    
    
    
    [[nodiscard]] bool forceBoolConVar(const char* name, bool value) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;
        if (!conVarTypeIs(conVar, cs2::ConVarValueType::boolean))
            return false;

        const auto pointerToValue = hookContext.patternSearchResults().template get<OffsetToConVarValue>().of(conVar).get();
        if (!pointerToValue)
            return false;

        const bool written = value;
        std::memcpy(pointerToValue, &written, sizeof(written));
        return true;
    }

    
    
    
    
    [[nodiscard]] bool forceFloatConVar(const char* name, float value) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;
        if (!conVarTypeIs(conVar, cs2::ConVarValueType::float32))
            return false;

        const auto pointerToValue = hookContext.patternSearchResults().template get<OffsetToConVarValue>().of(conVar).get();
        if (!pointerToValue)
            return false;

        const float written = value;
        std::memcpy(pointerToValue, &written, sizeof(written));
        return true;
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool patchUserInfoFlag(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;

        constexpr std::uintptr_t kTypeOffset = 0x28;
        constexpr std::uintptr_t kFlagsOffset = 0x30;
        const auto* type = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(conVar) + kTypeOffset);
        if (*type != static_cast<std::uint32_t>(cs2::ConVarValueType::string))
            return false; 

        auto* flags = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(conVar) + kFlagsOffset);
        constexpr std::uint32_t kFlagDevelopmentOnly = 0x2;  
        constexpr std::uint32_t kFlagProtected = 0x20;       
        constexpr std::uint32_t kFlagUserInfo = 0x200;       
        *flags |= kFlagUserInfo;
        *flags &= ~(kFlagDevelopmentOnly | kFlagProtected);
        return true;
    }

    
    
    
    
    
    [[nodiscard]] bool patchUserInfoFlagAny(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;

        constexpr std::uintptr_t kTypeOffset = 0x28;
        constexpr std::uintptr_t kFlagsOffset = 0x30;
        const auto* type = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(conVar) + kTypeOffset);
        if (*type > static_cast<std::uint32_t>(cs2::ConVarValueType::string))
            return false; 

        auto* flags = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(conVar) + kFlagsOffset);
        constexpr std::uint32_t kFlagDevelopmentOnly = 0x2;  
        constexpr std::uint32_t kFlagProtected = 0x20;       
        constexpr std::uint32_t kFlagUserInfo = 0x200;       
        *flags |= kFlagUserInfo;
        *flags &= ~(kFlagDevelopmentOnly | kFlagProtected);
        return true;
    }

    
    
    
    [[nodiscard]] bool hasUserInfoFlag(const char* name) const noexcept
    {
        const auto conVar = findConVar(name);
        if (!conVar)
            return false;
        constexpr std::uintptr_t kFlagsOffset = 0x30;
        const auto flags = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(conVar) + kFlagsOffset);
        return (flags & 0x200) != 0;
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
        if (conVarTypeIs(conVar, conVarValueTypeForType<typename ConVarType::ValueType>()))
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
