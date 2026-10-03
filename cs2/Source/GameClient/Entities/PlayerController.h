#pragma once

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

#include <Utils/Optional.h>

#include <CS2/Classes/Color.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <CS2/Constants/ColorConstants.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/TeamNumber.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <MemoryPatterns/PatternTypes/PlayerControllerPatternTypes.h>
#include "PlayerPawn.h"

template <typename HookContext>
class PlayerController {
public:
    PlayerController(HookContext& hookContext, cs2::CCSPlayerController* playerControllerPointer) noexcept
        : hookContext{hookContext}
        , playerControllerPointer{playerControllerPointer}
    {
    }

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(playerControllerPointer);
    }

    [[nodiscard]] bool operator==(const PlayerController& other) const noexcept
    {
        return playerControllerPointer != nullptr && playerControllerPointer == other.playerControllerPointer;
    }

    [[nodiscard]] TeamNumber teamNumber() const noexcept
    {
        return baseEntity().teamNumber();
    }

    [[nodiscard]] decltype(auto) pawn() const noexcept
    {
        const auto playerPawnHandle = hookContext.patternSearchResults().template get<OffsetToBasePawnHandle>().of(playerControllerPointer).get();
        if (!playerPawnHandle)
            return hookContext.template make<BaseEntity>(nullptr);
        return hookContext.template make<EntitySystem>().getEntityFromHandle2(*playerPawnHandle);
    }

    [[nodiscard]] decltype(auto) playerColorIndex() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToPlayerColor>().of(playerControllerPointer).toOptional();
    }

    // m_nTickBase - the server tick this controller is currently predicting. This is the tick CS2's
    // spread-seed generator hashes (velocity-cs2 reads exactly this field for its seed), so the
    // aimbot's spread compensation needs it to predict the same seed the game will use for the shot.
    // Resolved by field name through the schema system rather than a hardcoded offset, so a game
    // update that moves the field does not silently feed a wrong tick. {} if unavailable.
    [[nodiscard]] Optional<int> tickBase() const noexcept
    {
        if (!playerControllerPointer)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CBasePlayerController", "m_nTickBase");
        if (!offset.has_value() || *offset <= 0)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(playerControllerPointer) + *offset, sizeof(value));
        return value;
    }

    // The pointer inside the controller's m_sSanitizedClanTag (CUtlString) - the string the
    // game renders as our clan tag everywhere (scoreboard, #DecoratedPlayerName chat/death
    // notices). Resolved by field name through the schema, like m_iPing above. The 2026-09-23
    // update made client-visible clan tags real (GC SetMyClanId32BitEquipped / CSOPersonaDataPublic.
    // clan_tag on the server side); the field itself is display-local data, so spoofing it
    // (in-place rewrite - see ChatTools) changes what WE see. {} when unavailable.
    [[nodiscard]] Optional<char*> clanTagStringPointer() const noexcept
    {
        if (!playerControllerPointer)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_sSanitizedClanTag");
        if (!offset.has_value() || *offset <= 0)
            return {};
        char* value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(playerControllerPointer) + *offset, sizeof(value));
        if (!value)
            return {};
        return value;
    }

    // m_iPing - this controller's round-trip latency in milliseconds, as displayed on the scoreboard.
    // Used by the lag-comp record validity as velocity-cs2's one-way latency source (they read
    // INetChannel::GetLatency(FLOW_OUTGOING); halving the RTT gives the same quantity without needing
    // net-channel access). {} if unavailable.
    [[nodiscard]] Optional<int> ping() const noexcept
    {
        if (!playerControllerPointer)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iPing");
        if (!offset.has_value() || *offset <= 0)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(playerControllerPointer) + *offset, sizeof(value));
        return value;
    }

private:
    HookContext& hookContext;
    cs2::CCSPlayerController* playerControllerPointer;
};
