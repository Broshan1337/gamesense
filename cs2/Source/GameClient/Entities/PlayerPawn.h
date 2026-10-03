#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include <CS2/Classes/Color.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <CS2/Classes/ConVarTypes.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/PawnSettle.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <GameClient/Entities/TeamNumber.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <MemoryPatterns/PatternTypes/PlayerPawnPatternTypes.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <Utils/ColorUtils.h>

#include "BaseEntity.h"
#include "C4.h"
#include "HostageServices.h"
#include "WeaponServices.h"

class EntityFromHandleFinder;

template <typename HookContext>
class PlayerController;

template <typename HookContext>
class PlayerPawn {
public:
    using RawType = cs2::C_CSPlayerPawn;

    PlayerPawn(HookContext& hookContext, cs2::C_CSPlayerPawn* playerPawn) noexcept
        : hookContext{hookContext}
        , playerPawn{playerPawn}
    {
    }

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(playerPawn);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return playerPawn != nullptr;
    }

    // Raw pawn identity for the pawn-settle session gate (CLOCK_MONOTONIC identity tracking).
    [[nodiscard]] cs2::C_CSPlayerPawn* rawPawn() const noexcept
    {
        return playerPawn;
    }

    template <template <typename...> typename EntityType>
    [[nodiscard]] decltype(auto) cast() const noexcept
    {
        if (baseEntity().template is<EntityType>())
            return hookContext.template make<EntityType<HookContext>>(static_cast<typename EntityType<HookContext>::RawType*>(playerPawn));
        return hookContext.template make<EntityType<HookContext>>(nullptr);
    }

    [[nodiscard]] decltype(auto) weaponServices() const noexcept
    {
        return hookContext.template make<WeaponServices>(hookContext.patternSearchResults().template get<OffsetToWeaponServices>().of(playerPawn).valueOr(nullptr));
    }

    // The player's current aim punch (recoil kick) as {pitch, yaw, roll} degrees. CS2 fires the bullet
    // along (view_angles + aim punch), so the aimbot subtracts this from the angle it writes into
    // input_history to keep the shot on target as recoil kicks the view up - the same step velocity-cs2
    // does. Uses the game's OWN aim-punch getter (sub_14D31A0, see PointerToGetAimPunchFunction), which
    // interpolates the predictable + unpredictable base punch to the current tick - i.e. the exact value
    // the game will add to the shot, not just the base-angle field. The services pointer is schema-
    // resolved (m_pAimPunchServices). Returns {} if the services pointer or the getter is unavailable.
    // Aim punch is ~0 before the first shot of a burst, so subtracting it never disturbs a fresh or
    // standing shot - only the ongoing spray we are trying to correct.
    [[nodiscard]] Optional<cs2::Vector> aimPunchAngle() const noexcept
    {
        if (!playerPawn)
            return {};

        // MAP-TRANSITION SESSION GATE (AGENTS.md rule 0; the 2026-09-27 map-load crash):
        // GetAimPunch walks the pawn's aim-punch history at [services+0x28], which is not
        // built yet on a freshly-spawned pawn - the game itself only calls it after the
        // entity is fully constructed. Covers every reader (Removals view-punch, Rcs,
        // Aimbot, Triggerbot x2) in one place. CLOCK_MONOTONIC pawn-settle, NOT curtime -
        // curtime is blind during the join window (old map's value until the GlobalVars swap).
        if (!pawn_settle::ready(playerPawn))
            return {};

        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_pAimPunchServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return {};

        void* services = nullptr;
        std::memcpy(&services, reinterpret_cast<const std::byte*>(playerPawn) + *servicesOffset, sizeof(services));
        if (!services)
            return {};

        const auto getAimPunch = hookContext.patternSearchResults().template get<PointerToGetAimPunchFunction>();
        if (!getAimPunch)
            return {};

        // 2026-09-27 signature correction: the update added a caller-supplied accumulator
        // pair in rsi (read unconditionally, no null guard). A zeroed pair = zero extra
        // accumulation = the base predictable punch, which is what a standalone read wants.
        // The old call left rsi = an uncontrolled register - the mid-match SEGV class.
        PunchAccumulator zeroAccumulator{0, 0.0f};
        const auto punch = getAimPunch(services, &zeroAccumulator, 0.0f);
        return cs2::Vector{punch.pitch, punch.yaw, punch.roll};
    }

    // How many shots into the current spray this player is (m_iShotsFired). 0 before firing, 1 on the
    // first shot, growing as the spray continues - the recoil-control system uses it to know a spray is
    // underway (velocity-cs2 gates its RCS on this being > 1). Schema-resolved by name. {} if the field
    // is unavailable.
    [[nodiscard]] Optional<int> shotsFired() const noexcept
    {
        if (!playerPawn)
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_iShotsFired");
        if (!offset.has_value() || *offset <= 0)
            return {};

        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(playerPawn) + *offset, sizeof(value));
        return value;
    }

    // Eye position = the pawn origin plus its view offset (m_vecViewOffset), the point shots and traces
    // originate from. Resolved by name through the schema system, so a game update that moves the field
    // does not silently make callers aim from the feet. {} if the origin or the field is unavailable.
    [[nodiscard]] Optional<cs2::Vector> eyePosition() const noexcept
    {
        const auto origin = baseEntity().absOrigin();
        if (!origin.hasValue())
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_vecViewOffset");
        if (!offset.has_value() || *offset <= 0)
            return {};

        cs2::Vector viewOffset{};
        std::memcpy(&viewOffset, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(baseEntity())) + *offset, sizeof(viewOffset));
        return cs2::Vector{origin.value().x + viewOffset.x, origin.value().y + viewOffset.y, origin.value().z + viewOffset.z};
    }

    // The local player's first-person "arms" entity - owns the actual rendered viewmodel
    // weapon clones as scene-node children (see SkinChanger::findHudWeapon()). Resolved via
    // the schema system (m_hHudModelArms confirmed as a real field name via a direct string
    // search of libclient.so), not a byte pattern.
    [[nodiscard]] decltype(auto) hudModelArms() const noexcept
    {
        const auto handle = hookContext.hudModelArmsOffset().hudModelArms.of(playerPawn).valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX});
        return hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(hookContext.template make<EntitySystem>().getEntityFromHandle(handle)));
    }

    [[nodiscard]] decltype(auto) weapons() const noexcept
    {
        return weaponServices().weapons();
    }

    [[nodiscard]] TeamNumber teamNumber() const noexcept
    {
        return baseEntity().teamNumber();
    }

    [[nodiscard]] std::optional<bool> isAlive() const noexcept
    {
        return baseEntity().isAlive();
    }

    [[nodiscard]] decltype(auto) playerController() const noexcept
    {
        const auto playerControllerHandle = hookContext.patternSearchResults().template get<OffsetToPlayerController>().of(playerPawn).get();
        if (!playerControllerHandle)
            return hookContext.template make<PlayerController>(nullptr);
        return hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(hookContext.template make<EntitySystem>().getEntityFromHandle(*playerControllerHandle)));
    }

    [[nodiscard]] auto health() const noexcept
    {
        return baseEntity().health();
    }

    [[nodiscard]] auto hasImmunity() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToPlayerPawnImmunity>().of(playerPawn).toOptional();
    }

    [[nodiscard]] decltype(auto) absOrigin() const noexcept
    {
        return baseEntity().absOrigin();
    }

    [[nodiscard]] bool isControlledByLocalPlayer() const noexcept
    {
        return playerController() == hookContext.localPlayerController();
    }

    [[nodiscard]] std::optional<bool> isEnemy() const noexcept
    {
        return teamNumber() != hookContext.localPlayerController().teamNumber() || teammatesAreEnemies();
    }

    [[nodiscard]] bool isTTorCT() const noexcept
    {
        const auto _teamNumber = teamNumber();
        return _teamNumber == TeamNumber::TT || _teamNumber == TeamNumber::CT;
    }

    [[nodiscard]] auto isPickingUpHostage() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToIsPickingUpHostage>().of(playerPawn).toOptional();
    }

    [[nodiscard]] auto isDefusing() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToIsDefusing>().of(playerPawn).toOptional();
    }

    [[nodiscard]] auto isScoped() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToIsScoped>().of(playerPawn).toOptional();
    }

    [[nodiscard]] bool isRescuingHostage() const noexcept
    {
        return hostageServices().hasCarriedHostage();
    }

    [[nodiscard]] bool isCarryingC4() const noexcept
    {
        return weapons().template has<C4>();
    }

    [[nodiscard]] decltype(auto) carriedC4() const noexcept
    {
        return weapons().template get<C4>();
    }

    [[nodiscard]] float getRemainingFlashBangTime() const noexcept
    {
        const auto curTime = hookContext.globalVars().curtime();
        if (!curTime.hasValue())
            return 0.0f;
        const auto flashBangEndTime = hookContext.patternSearchResults().template get<OffsetToFlashBangEndTime>().of(playerPawn).get();
        if (!flashBangEndTime)
            return 0.0f;
        if (*flashBangEndTime <= curTime.value())
            return 0.0f;
        return *flashBangEndTime - curTime.value();
    }

    [[nodiscard]] decltype(auto) getActiveWeapon() const noexcept
    {
        return weaponServices().getActiveWeapon();
    }

    // Whether the player is standing on the ground (m_fFlags & FL_ONGROUND). {} if the field did not
    // resolve. Used by the rage force-shot to pick the ground vs air toggle.
    [[nodiscard]] Optional<bool> isOnGround() const noexcept
    {
        const auto flagsOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!flagsOffset.has_value() || *flagsOffset <= 0)
            return {};
        std::uint32_t flags{};
        std::memcpy(&flags, reinterpret_cast<const std::byte*>(playerPawn) + *flagsOffset, sizeof(flags));
        return (flags & (1u << 0)) != 0;
    }

    // True if the currently-equipped weapon is at its MINIMUM possible inaccuracy for this player's
    // current stance (velocity-cs2's is_max_accuracy). Reads the player's own m_fFlags (on-ground /
    // ducking) and m_vecAbsVelocity (2D speed) and hands them to BaseWeapon::isMaxAccuracy along with the
    // live, penalty-updated inaccuracy. False if the active weapon or any required field can't be read
    // (cannot confirm max accuracy -> do not claim it). Shared by the triggerbot's "Only Shoot At Max
    // Accuracy" gate and the rage force-shot's fire gate.
    [[nodiscard]] bool isAtMaxAccuracy() const noexcept
    {
        auto&& weapon = getActiveWeapon();
        weapon.updateAccuracyPenalty();
        const auto inaccuracy = weapon.inaccuracy();
        if (!inaccuracy.hasValue())
            return false;

        const auto flagsOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        const auto velocityOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!flagsOffset.has_value() || *flagsOffset <= 0 || !velocityOffset.has_value() || *velocityOffset <= 0)
            return false;

        auto* const bytes = reinterpret_cast<const std::byte*>(playerPawn);
        std::uint32_t flags{};
        std::memcpy(&flags, bytes + *flagsOffset, sizeof(flags));
        cs2::Vector velocity{};
        std::memcpy(&velocity, bytes + *velocityOffset, sizeof(velocity));

        // FL_ONGROUND is bit 0; FL_DUCKING is bit 1 - the Source FL_ layout (and velocity-cs2's own
        // entity_flags enum) skips bit 2, so ducking is 1<<1.
        const bool onGround = (flags & (1u << 0)) != 0;
        const bool ducking = (flags & (1u << 1)) != 0;
        const float speed2d = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
        return weapon.isMaxAccuracy(inaccuracy.value(), onGround, ducking, isScoped().valueOr(false), speed2d);
    }

    [[nodiscard]] auto getSceneObjectUpdater() const noexcept
    {
        return reinterpret_cast<std::uint64_t(*)(cs2::C_CSPlayerPawn*, void*, bool)>(sceneObjectUpdaterHandle() ? sceneObjectUpdaterHandle()->updaterFunction : nullptr);
    }

    void setSceneObjectUpdater(auto x) const noexcept
    {
        if (sceneObjectUpdaterHandle())
            sceneObjectUpdaterHandle()->updaterFunction = reinterpret_cast<std::uint64_t(*)(void*, void*, bool)>(x);
    }

    [[nodiscard]] decltype(auto) isUsingSniperRifle() const
    {
        return getActiveWeapon().isSniperRifle();
    }

private:
    [[nodiscard]] auto sceneObjectUpdaterHandle() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToPlayerPawnSceneObjectUpdaterHandle>().of(playerPawn).valueOr(nullptr);
    }

    [[nodiscard]] decltype(auto) hostageServices() const noexcept
    {
        return hookContext.template make<HostageServices>(hookContext.patternSearchResults().template get<OffsetToHostageServices>().of(playerPawn).valueOr(nullptr));
    }

    [[nodiscard]] bool teammatesAreEnemies() const noexcept
    {
        return hookContext.cvarSystem().template getConVarValue<cs2::mp_teammates_are_enemies>().value_or(true);
    }

    HookContext& hookContext;
    cs2::C_CSPlayerPawn* playerPawn;
};
