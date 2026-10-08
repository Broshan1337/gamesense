#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <Features/Game/AutoPeekConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>




















template <typename HookContext>
class AutoPeek {
public:
    explicit AutoPeek(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(autopeek_vars::Enabled)) {
            armed = false;
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true) {
            armed = false;
            return;
        }

        UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue())
            return;
        if (armed && curtime.value() < armedAtCurtime) {
            armed = false; 
            return;
        }
        armedAtCurtime = curtime.value();

        auto* const entity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        cs2::Vector velocity{};
        cs2::Vector origin{};
        if (!readVector(entity, "C_BaseEntity", "m_vecVelocity", velocity) || !readVector(entity, "CGameSceneNode", "m_vecOrigin", origin))
            return;

        const float speed = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);

        if (!armed) {
            if (speed < kArmSpeed) {
                armed = true;
                armedOrigin = origin;
            }
            return;
        }

        if (speed < kArmSpeed) {
            armedOrigin = origin; 
            return;
        }

        const float dx = armedOrigin.x - origin.x;
        const float dy = armedOrigin.y - origin.y;
        if (dx * dx + dy * dy < kRetractRadius * kRetractRadius)
            return; 

        
        
        
        const auto yaw = userCmd.viewYaw();
        if (!yaw.hasValue())
            return;
        const float length = trig::squareRoot(dx * dx + dy * dy);
        if (length < 1.0f)
            return;
        const float dirX = dx / length;
        const float dirY = dy / length;
        const float yawRad = yaw.value() * trig::kDegreesToRadians;
        const float cosYaw = trig::cosine(yawRad);
        const float sinYaw = trig::sine(yawRad);
        
        const float forward = clampAnalog(dirX * cosYaw + dirY * sinYaw);
        const float left = clampAnalog(-dirX * sinYaw + dirY * cosYaw);

        
        
        std::uint64_t counterButtons = 0;
        if (velocity.x * cosYaw + velocity.y * sinYaw > kCounterSpeedThreshold)
            counterButtons |= kInBack;
        else if (velocity.x * cosYaw + velocity.y * sinYaw < -kCounterSpeedThreshold)
            counterButtons |= kInForward;
        if (velocity.x * sinYaw - velocity.y * cosYaw > kCounterSpeedThreshold)
            counterButtons |= kInMoveLeft;
        else if (velocity.x * sinYaw - velocity.y * cosYaw < -kCounterSpeedThreshold)
            counterButtons |= kInMoveRight;
        if (counterButtons)
            (void)userCmd.pressButtonsBothBanks(counterButtons);

        
        
        
        auto* const baseMessage = userCmd.baseMessage();
        if (!baseMessage)
            return;
        auto&& subtick = hookContext.template make<SubtickMoves>();
        if (auto* const step = subtick.add(baseMessage, 0.0f))
            SubtickMoves<HookContext>::setAnalogDeltas(step, forward, left);
    }

private:
    [[nodiscard]] static float clampAnalog(float value) noexcept
    {
        if (value > 1.0f)
            return 1.0f;
        if (value < -1.0f)
            return -1.0f;
        return value;
    }

    [[nodiscard]] bool readVector(cs2::C_BaseEntity* entity, const char* className, const char* field, cs2::Vector& out) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, field);
        if (!offset.has_value() || *offset <= 0)
            return false;
        std::memcpy(&out, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(out));
        return true;
    }

    
    
    static constexpr float kArmSpeed = 15.0f;
    static constexpr float kRetractRadius = 10.0f;
    
    static constexpr float kCounterSpeedThreshold = 60.0f;

    static constexpr std::uint64_t kInForward = std::uint64_t{1} << 3;
    static constexpr std::uint64_t kInBack = std::uint64_t{1} << 4;
    static constexpr std::uint64_t kInMoveLeft = std::uint64_t{1} << 9;
    static constexpr std::uint64_t kInMoveRight = std::uint64_t{1} << 10;

    
    inline static bool armed{false};
    inline static cs2::Vector armedOrigin{};
    inline static float armedAtCurtime{0.0f};

    HookContext& hookContext;
};
