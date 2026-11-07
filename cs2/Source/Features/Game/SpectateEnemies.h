#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Game/SpectateEnemiesConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/VerifyConsole.h>

















namespace spectate_enemies
{
inline constexpr int kMaxListedPlayers = 64;



struct CycleState {
    int enemyIndex = -1;
    std::uint32_t ourHandle = 0;
    std::uint32_t lastForeignHandle = 0;
};

inline CycleState cycleState;
}

template <typename HookContext>
class SpectateEnemies {
public:
    explicit SpectateEnemies(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        if (!GET_CONFIG_VAR(spectate_vars::Enabled)) {
            reset();
            return;
        }

        
        
        
        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pObserverServices");
        const auto targetOffset = hookContext.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_hObserverTarget");
        const auto healthOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_iHealth");
        if (!servicesOffset.has_value() || !targetOffset.has_value() || !healthOffset.has_value()) {
            VerifyConsole::write(30.0f, "spec", "schema offsets unresolved - spectate enemies inactive");
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn) {
            // also fires (throttled) at the menu - useful signal if the game ever
            // clears the controller's pawn handle on death, which would silently
            // park this feature in reset() forever.
            VerifyConsole::write(30.0f, "spec", "spectate enemies: no local pawn");
            reset();
            return;
        }

        const auto* const localEntity = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity()));
        int localHealth{};
        std::memcpy(&localHealth, localEntity + *healthOffset, sizeof(localHealth));
        if (localHealth > 0) {
            reset();
            return;
        }

        
        
        std::uint32_t enemies[spectate_enemies::kMaxListedPlayers]{};
        std::uint32_t teammates[spectate_enemies::kMaxListedPlayers]{};
        int enemyCount = 0;
        int teammateCount = 0;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (enemyCount == spectate_enemies::kMaxListedPlayers && teammateCount == spectate_enemies::kMaxListedPlayers)
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = baseEntity.template as<PlayerPawn>();
            if (!pawn || pawn.isControlledByLocalPlayer())
                return;
            if (pawn.isAlive() != true)
                return;

            const auto handleValue = pawn.baseEntity().handle().value;
            if (pawn.isEnemy() == true) {
                if (enemyCount < spectate_enemies::kMaxListedPlayers)
                    enemies[enemyCount++] = handleValue;
            } else if (pawn.isEnemy() == false) {
                if (teammateCount < spectate_enemies::kMaxListedPlayers)
                    teammates[teammateCount++] = handleValue;
            }
        });

        if (enemyCount == 0) {
            VerifyConsole::write(30.0f, "spec", "spectate enemies: no alive enemy pawns found");
            reset();
            return;
        }

        void* services{};
        std::memcpy(&services, localEntity + *servicesOffset, sizeof(services));
        if (!services) {
            VerifyConsole::write(30.0f, "spec", "spectate enemies: observer services missing on corpse");
            return;
        }

        auto* const servicesBytes = reinterpret_cast<std::byte*>(services);
        std::uint32_t currentTarget{};
        std::memcpy(&currentTarget, servicesBytes + *targetOffset, sizeof(currentTarget));

        auto& state = spectate_enemies::cycleState;

        // Already riding our pick and the write sticks - nothing to do.
        if (currentTarget == state.ourHandle && state.enemyIndex >= 0 && state.enemyIndex < enemyCount
            && enemies[state.enemyIndex] == currentTarget)
            return;

        // A target we did not choose means the spectate moved under us: the user
        // pressed the spec keys, the picked enemy died, or a server snapshot /
        // the game's own observer update reverted m_hObserverTarget (it is a
        // networked field - anyone may rewrite it after our last CreateMove).
        // Remember what we saw and step our index so manual cycling keeps working.
        bool moved = false;
        if (currentTarget != state.ourHandle && currentTarget != state.lastForeignHandle) {
            const int direction = directionOf(state.lastForeignHandle, currentTarget, teammates, teammateCount);
            if (state.enemyIndex < 0) {
                state.enemyIndex = 0;
                VerifyConsole::write(30.0f, "spec", "spectate enemies: takeover, %d alive enemies", enemyCount);
            } else {
                state.enemyIndex = (state.enemyIndex + direction + enemyCount) % enemyCount;
                VerifyConsole::write(5.0f, "spec", "spectate enemies: riding enemy %d of %d alive (dir %d)",
                    state.enemyIndex + 1, enemyCount, direction);
            }
            state.lastForeignHandle = currentTarget;
            moved = true;
        } else if (state.enemyIndex < 0) {
            state.enemyIndex = 0;
            state.lastForeignHandle = currentTarget;
            VerifyConsole::write(30.0f, "spec", "spectate enemies: takeover, %d alive enemies", enemyCount);
            moved = true;
        }
        if (state.enemyIndex >= enemyCount)
            state.enemyIndex = 0;

        // (Re)assert the pick EVERY tick until it sticks. The old event-driven write
        // fired exactly once per state change and then assumed success: the moment
        // anything reverted the field, the observed target came back equal to
        // lastForeignHandle, no branch matched again, and the feature stayed dead
        // after a single clobber - that one-shot design was the bug. CreateMove runs
        // after the frame's snapshot apply, so this write is what the camera reads
        // on the following render.
        if (!moved && currentTarget != state.ourHandle)
            VerifyConsole::write(5.0f, "spec", "spectate enemies: target reverted (0x%x), re-asserting enemy %d",
                currentTarget, state.enemyIndex + 1);
        state.ourHandle = enemies[state.enemyIndex];
        std::memcpy(servicesBytes + *targetOffset, &state.ourHandle, sizeof(state.ourHandle));
    }

private:
    
    
    
    [[nodiscard]] static int directionOf(std::uint32_t previousForeign, std::uint32_t current,
        const std::uint32_t* teammates, int teammateCount) noexcept
    {
        if (previousForeign == 0 || previousForeign == current || teammateCount < 2)
            return 1;

        int previousPos = -1;
        int currentPos = -1;
        for (int i = 0; i < teammateCount; ++i) {
            if (teammates[i] == previousForeign)
                previousPos = i;
            if (teammates[i] == current)
                currentPos = i;
        }
        if (previousPos < 0 || currentPos < 0)
            return 1;

        int delta = currentPos - previousPos;
        if (delta > teammateCount / 2)
            delta -= teammateCount;
        if (delta < -(teammateCount / 2))
            delta += teammateCount;
        return delta < 0 ? -1 : 1;
    }

    static void reset() noexcept
    {
        auto& state = spectate_enemies::cycleState;
        state.enemyIndex = -1;
        state.ourHandle = 0;
        state.lastForeignHandle = 0;
    }

    HookContext& hookContext;
};