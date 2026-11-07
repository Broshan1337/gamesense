#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/Vector.h>
#include <CS2/Classes/ViewSetup.h>
#include <Features/Misc/Freecam/FreecamConfigVariables.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Trig.h>

// Freecam (misc): disengage the render camera from the player and fly it
// freely. The camera integrates once per render frame from the movement keys
// snapshotted at command-build time (W/S fly along the full view direction,
// A/D strafe on the horizontal plane, jump rises, duck descends); the view
// angles are left alone, so the camera keeps looking wherever the mouse
// points. While engaged, the player's body is frozen in onWriteMoveCrc - no
// walking, jumping, crouching, shooting or using - so the body stands guard
// while the camera roams.
template <typename HookContext>
class Freecam {
public:
    explicit Freecam(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(freecam_vars::Enabled)) {
            // Re-seed at the engine's camera on the next engagement so the
            // handoff is seamless instead of resuming a stale position.
            seeded = false;
            return;
        }

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        // Snapshot the user's movement intent BEFORE freezing it: the render
        // thread's overrideView has no command access and reads this instead.
        // Plain aligned 64-bit store - safe to read across threads.
        std::uint64_t buttons{};
        if (userCmd.isButtonDown(Buttons::kForward)) buttons |= Buttons::kForward;
        if (userCmd.isButtonDown(Buttons::kBack)) buttons |= Buttons::kBack;
        if (userCmd.isButtonDown(Buttons::kMoveLeft)) buttons |= Buttons::kMoveLeft;
        if (userCmd.isButtonDown(Buttons::kMoveRight)) buttons |= Buttons::kMoveRight;
        if (userCmd.isButtonDown(Buttons::kJump)) buttons |= Buttons::kJump;
        if (userCmd.isButtonDown(kDuckButton)) buttons |= kDuckButton;
        inputButtons = buttons;

        // Freeze the body: zero the command's movement + interaction buttons
        // and analog moves so the server keeps the player where it stands and
        // shooting, planting and jumping all stop with it. The banks alone are
        // not enough: a FRESH subtick press event fires mid-tick even with the
        // banks zeroed - that is why space still jumped and mouse1 still fired
        // through a bank-only freeze - so the samples' presses are rewritten
        // as releases too. Samples are never deleted: the replayed button
        // stream carries key state across ticks, and deleting a real
        // press/release pair strands the key (StrafeCommand::commit).
        userCmd.replaceButtons(kFreezeMask, 0);
        userCmd.setForwardMove(0.0f);
        userCmd.setLeftMove(0.0f);
        // Also clear attack1_start_history_index (the staged shot's timing
        // marker): a sent attack without it trips the server's "no attack
        // information present" warning, and a leftover marker from a cancelled
        // shot has no business surviving the freeze (Aimbot's spread gate
        // cancels a shot with this exact pair).
        userCmd.suppressAttack(Buttons::kAttack);
        SubtickMoves<HookContext>::releaseButtons(userCmd.baseMessage(), kFreezeMask);
    }

    void overrideView(ViewSetup* viewSetup) const noexcept
    {
        if (!viewSetup)
            return;
        if (!GET_CONFIG_VAR(freecam_vars::Enabled)) {
            seeded = false;
            return;
        }

        auto& cameraPosition = ViewSetup::cameraPosition(viewSetup);
        const auto& angles = ViewSetup::viewAngles(viewSetup);
        if (!looksLikeAngles(angles))
            return;

        // clock_gettime, not <chrono>: this project's toolchain does not link
        // std::chrono's steady_clock (undefined reference at link time).
        timespec timestamp{};
        if (::clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
            return;
        const double now = static_cast<double>(timestamp.tv_sec)
            + static_cast<double>(timestamp.tv_nsec) / 1.0e9;
        if (!seeded) {
            // First frame after engaging: pick the camera up exactly where
            // the engine left it (post-ForceThirdPerson/Removals - we run
            // last), then integrate from here.
            position = cameraPosition;
            seeded = true;
        } else {
            // Per-frame integration; the clamp keeps a hitch or a pause from
            // teleporting the camera across the map.
            const float dt = std::clamp(static_cast<float>(now - lastUpdate), 0.0f, 0.1f);
            const float pitch = angles.x * trig::kDegreesToRadians;
            const float yaw = angles.y * trig::kDegreesToRadians;
            const float pitchCosine = trig::cosine(pitch);
            // Forward includes pitch: looking up + W climbs, like noclip.
            const cs2::Vector forward{
                trig::cosine(yaw) * pitchCosine,
                trig::sine(yaw) * pitchCosine,
                -trig::sine(pitch),
            };
            // Strafe stays level regardless of pitch - A/D never dive. Same
            // right vector as Source's AngleVectors (roll = 0).
            const cs2::Vector right{trig::sine(yaw), -trig::cosine(yaw), 0.0f};

            const std::uint64_t buttons = inputButtons;
            const float forwardAxis = float((buttons & Buttons::kForward) != 0)
                                    - float((buttons & Buttons::kBack) != 0);
            const float rightAxis = float((buttons & Buttons::kMoveRight) != 0)
                                  - float((buttons & Buttons::kMoveLeft) != 0);
            const float verticalAxis = float((buttons & Buttons::kJump) != 0)
                                     - float((buttons & kDuckButton) != 0);
            const float step = GET_CONFIG_VAR(freecam_vars::Speed) * dt;
            position.x += (forward.x * forwardAxis + right.x * rightAxis) * step;
            position.y += (forward.y * forwardAxis + right.y * rightAxis) * step;
            position.z += (forward.z * forwardAxis + verticalAxis) * step;
        }
        lastUpdate = now;
        cameraPosition = position;
    }

    void onUnload() noexcept
    {
        seeded = false;
        inputButtons = 0;
    }

private:
    using Buttons = cs2::CCSGOInput::Buttons;

    [[nodiscard]] static bool looksLikeAngles(const cs2::Vector& angles) noexcept
    {
        constexpr float kMaxPitch = 90.1f;
        constexpr float kMaxYaw = 180.1f;
        return angles.x >= -kMaxPitch && angles.x <= kMaxPitch
            && angles.y >= -kMaxYaw && angles.y <= kMaxYaw
            && angles.z >= -kMaxPitch && angles.z <= kMaxPitch;
    }

    // CCSGOInput::Buttons only names attack/jump/move; the rest follow the
    // Source IN_ bits this enum is laid out on (duck/use/attack2 verified
    // against LastTickDefuse's kUseButton and Lua's kInAttack2).
    static constexpr std::uint64_t kDuckButton = 0x4;
    static constexpr std::uint64_t kUseButton = 0x20;
    static constexpr std::uint64_t kAttack2Button = 0x800;
    // Everything the body can DO to the world or itself: move, jump, crouch,
    // fire (both buttons) and interact (plant/defuse/pickup).
    static constexpr std::uint64_t kFreezeMask = cs2::CCSGOInput::Buttons::kAttack
        | cs2::CCSGOInput::Buttons::kJump | cs2::CCSGOInput::Buttons::kForward
        | cs2::CCSGOInput::Buttons::kBack | cs2::CCSGOInput::Buttons::kMoveLeft
        | cs2::CCSGOInput::Buttons::kMoveRight | kDuckButton | kUseButton
        | kAttack2Button;

    HookContext& hookContext;
    inline static cs2::Vector position{};
    inline static std::uint64_t inputButtons{};
    inline static bool seeded{};
    inline static double lastUpdate{};
};
