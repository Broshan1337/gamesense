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

// skeet's quick peek / auto peek, ported to the Linux client (memory reference_pastoskeet_dump).
// skeet arms a peek point when the local player stands still (velocity ~ 0, a traced peek
// destination + a halo particle marker - the marker is deferred here until the world-to-screen
// overlay lands), then, while the player is peeked out, projects their velocity onto the peek
// axis and DRIVES THEM BACK: subtick analog movement written with negated signs plus the
// movement button bits flipped (IN_FORWARD/IN_BACK 0x8/0x10, IN_MOVELEFT/IN_MOVERIGHT
// 0x200/0x400). The player releases their keys and is walked back onto the anchor.
//
// Our port anchors at the standstill position (re-anchoring whenever the player comes to rest,
// so the marker follows their latest standing spot) and counter-drives along the straight line
// back to the anchor once they are more than kRetractRadius away. The counter-drive goes through
// both channels the movement system reads: the raw movement buttons (banks + buttons_pb - slot 6
// rebuilds forwardmove/sidemove from them, the autoStop path) and an explicit subtick analog step
// at when=0 covering the whole tick (the server applies the analog deltas from the step's moment,
// overriding the key-derived movement for the tick).
//
// Like the other cross-command features the anchor state is `inline static`: feature objects are
// rebuilt per hook call. A curtime rewind (map change / reconnect) disarms - a stale anchor from
// another world must never drive the player.
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
            armed = false; // time went backwards: new world, old anchor is garbage
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
            armedOrigin = origin; // standing again: re-anchor for the next peek
            return;
        }

        const float dx = armedOrigin.x - origin.x;
        const float dy = armedOrigin.y - origin.y;
        if (dx * dx + dy * dy < kRetractRadius * kRetractRadius)
            return; // close enough to the anchor - let the player settle

        // Counter-drive direction: straight back to the anchor, expressed in the view frame
        // (yaw 0 faces +X, +Y is left - the autoStop convention). Only the components the analog
        // step can carry: clamped to the [-1, 1] analog range.
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
        // forward = d . (cos, sin); left = d . (-sin, cos)
        const float forward = clampAnalog(dirX * cosYaw + dirY * sinYaw);
        const float left = clampAnalog(-dirX * sinYaw + dirY * cosYaw);

        // Raw button half: press the movement buttons that counter the CURRENT velocity (the
        // autoStop table) so the key-derived base movement also drives back.
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

        // Subtick analog half: one step at when=0 carrying the back-to-anchor direction for the
        // whole tick (later features may still append their own steps after it; sortByWhen in the
        // WriteMoveCrc tail keeps the timeline ordered).
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

    // Below this horizontal speed the player counts as standing (armed/re-anchored); above the
    // retract radius away from the anchor, the counter-drive engages.
    static constexpr float kArmSpeed = 15.0f;
    static constexpr float kRetractRadius = 10.0f;
    // Counter-strafe button threshold (autoStop's).
    static constexpr float kCounterSpeedThreshold = 60.0f;

    static constexpr std::uint64_t kInForward = std::uint64_t{1} << 3;
    static constexpr std::uint64_t kInBack = std::uint64_t{1} << 4;
    static constexpr std::uint64_t kInMoveLeft = std::uint64_t{1} << 9;
    static constexpr std::uint64_t kInMoveRight = std::uint64_t{1} << 10;

    // Cross-tick anchor state (feature objects are rebuilt per hook call).
    inline static bool armed{false};
    inline static cs2::Vector armedOrigin{};
    inline static float armedAtCurtime{0.0f};

    HookContext& hookContext;
};
