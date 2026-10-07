#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/EntitySystem/CEntityIndex.h>
#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/ShotGeometry.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <Features/Combat/SubtickShotWriter.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <GameClient/Bind.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/MouseState.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>


























template <typename HookContext>
class Triggerbot {
public:
    explicit Triggerbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    static void disarmStatics() noexcept
    {
        armed = false;
        armedTarget = nullptr;
        fireAtTime = 0.0f;
    }

    
    
    
    
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::Enabled) || !Bind::isDown(GET_CONFIG_VAR(triggerbot_vars::HoldKey))) {
            disarm();
            return;
        }

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue()) {
            disarm();
            return;
        }

        
        
        
        static float lastSeenCurtime = -1.0f;
        if (lastSeenCurtime > 0.0f && now.value() < lastSeenCurtime) {
            armed = false;
            fireAtTime = 0.0f;
        }
        lastSeenCurtime = now.value();

        auto&& target = crosshairTarget();
        if (!target) {
            disarm();
            return;
        }

        
        auto* const rawTarget = static_cast<cs2::C_BaseEntity*>(target.baseEntity());
        if (!armed || armedTarget != rawTarget) {
            armed = true;
            armedTarget = rawTarget;
            const float delay = delaySeconds();
            fireAtTime = now.value() + delay;
            return;
        }

        if (now.value() < fireAtTime)
            return;

        
        
        
        
        if (!wouldShotLand(target))
            return;
        if (!passesMaxAccuracyGate())
            return;
        if (!passesVisibility(target))
            return;
        if (!passesAimGates(target, cmd))
            return;
        if (!passesSeededFire(target, cmd))
            return;

        
        
        if (!hookContext.template make<AttackCommand>().press(cmd))
            return;

        
        
        
        
        
        
        
        
        
        
        
        if (GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation)) {
            auto&& localPawn = hookContext.activeLocalPlayerPawn();
            const UserCmd firedCmd{cmd};
            const auto aimPitch = firedCmd.viewPitch();
            const auto aimYaw = firedCmd.viewYaw();
            if (localPawn && aimPitch.hasValue() && aimYaw.hasValue()) {
                float punchPitch = 0.0f, punchYaw = 0.0f;
                const auto shots = localPawn.shotsFired();
                const bool rcsApplied = GET_CONFIG_VAR(rcs_vars::Enabled) && shots.hasValue() && shots.value() >= 1;
                if (const auto punch = localPawn.aimPunchAngle(); !rcsApplied && punch.hasValue()) {
                    punchPitch = punch.value().x;
                    punchYaw = punch.value().y;
                }
                
                
                
                static_cast<void>(hookContext.template make<SubtickShotWriter>().run(cmd, localPawn,
                                                                                   aimPitch.value(), aimYaw.value(),
                                                                                   punchPitch, punchYaw,
                                                                                   0.0f ,
                                                                                   true));
            }
        }
    }

    void onUnload() const noexcept
    {
        disarm();
    }

private:
    
    
    [[nodiscard]] PlayerPawn<HookContext> crosshairTarget() const noexcept
    {
        const auto none = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(nullptr));

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return none;

        if (localPawn.isAlive() != true)
            return none;

        
        
        
        const auto bullets = localPawn.getActiveWeapon().numBullets();
        if (bullets.hasValue() && bullets.value() <= 0)
            return none;

        const auto targetIndex = crosshairEntityIndex(localPawn);
        if (!targetIndex.hasValue())
            return none;

        auto* const entity = hookContext.template make<EntitySystem>().getEntityFromIndex(cs2::CEntityIndex{targetIndex.value()});
        if (!entity)
            return none;

        
        
        auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entity));
        if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
            return none;

        auto&& target = baseEntity.template as<PlayerPawn>();
        if (!target || target.isControlledByLocalPlayer())
            return none;

        
        
        if (target.isEnemy() != true)
            return none;

        const auto health = target.health();
        if (!health.hasValue() || health.value() <= 0)
            return none;

        
        if (const auto immune = target.hasImmunity(); immune.hasValue() && immune.value())
            return none;

        
        
        
        
        
        
        
        return target;
    }

    
    
    
    
    
    
    [[nodiscard]] bool wouldShotLand(auto&& target) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::AccuracyCheck))
            return true;

        const auto cone = hookContext.localPlayerBulletInaccuracy();
        if (!cone.hasValue())
            return true;

        const auto localOrigin = hookContext.activeLocalPlayerPawn().absOrigin();
        const auto targetOrigin = target.absOrigin();
        if (!localOrigin.hasValue() || !targetOrigin.hasValue())
            return true;

        const auto dx = targetOrigin.value().x - localOrigin.value().x;
        const auto dy = targetOrigin.value().y - localOrigin.value().y;
        const auto dz = targetOrigin.value().z - localOrigin.value().z;
        const auto distanceSquared = dx * dx + dy * dy + dz * dz;

        
        
        const auto radius = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::AccuracyRadius));
        const auto maxMissSquared = cone.value() * cone.value() * distanceSquared;
        return maxMissSquared <= radius * radius;
    }

    
    
    
    
    [[nodiscard]] bool passesAimGates(auto&& target, cs2::CUserCmd* cmd) const noexcept
    {
        const bool headOnly = GET_CONFIG_VAR(triggerbot_vars::HeadOnly);
        int hitchanceThreshold = GET_CONFIG_VAR(triggerbot_vars::Hitchance);

        
        
        
        
        
        

        if (!headOnly && hitchanceThreshold == 0)
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto eye = localPawn.eyePosition();
        const UserCmd userCmd{cmd};
        const auto pitch = userCmd.viewPitch();
        const auto yaw = userCmd.viewYaw();
        if (!eye.hasValue() || !pitch.hasValue() || !yaw.hasValue())
            return true;

        if (headOnly && !onHead(target, eye.value(), pitch.value(), yaw.value()))
            return false;

        if (hitchanceThreshold > 0 && !passesHitchance(target, localPawn, eye.value(), pitch.value(), yaw.value(), headOnly, hitchanceThreshold))
            return false;

        return true;
    }

    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool passesSeededFire(auto&& target, cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::SeededFire) || GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation))
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        const auto tick = hookContext.localPlayerController().tickBase();
        const UserCmd userCmd{cmd};
        const auto pitch = userCmd.viewPitch();
        const auto yaw = userCmd.viewYaw();
        const auto punch = localPawn.aimPunchAngle();
        const auto eye = localPawn.eyePosition();
        if (!params.hasValue() || !tick.hasValue() || !pitch.hasValue() || !yaw.hasValue() || !eye.hasValue())
            return true;

        float punchPitch = 0.0f, punchYaw = 0.0f;
        if (punch.hasValue()) {
            punchPitch = punch.value().x;
            punchYaw = punch.value().y;
        }
        const float writtenPitch = pitch.value() - punchPitch;
        const float writtenYaw = yaw.value() - punchYaw;

        const auto seed = solver.seed(typename SpreadSolver<HookContext>::Angles{writtenPitch, writtenYaw, 0.0f}, tick.value());
        if (!seed.hasValue())
            return true;
        const auto offset = solver.spreadOffset(seed.value(), params.value());
        if (!offset.hasValue()) return false;

        
        
        
        const auto basis = shot_geometry::angleVectors(writtenPitch, writtenYaw);
        const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
            basis.forward.x + basis.left.x * offset.value().x + basis.up.x * offset.value().y,
            basis.forward.y + basis.left.y * offset.value().x + basis.up.y * offset.value().y,
            basis.forward.z + basis.left.z * offset.value().x + basis.up.z * offset.value().y,
        });

        
        
        constexpr float kTraceRange = 8192.0f;
        const cs2::Vector end{eye.value().x + basis.forward.x * kTraceRange,
                              eye.value().y + basis.forward.y * kTraceRange,
                              eye.value().z + basis.forward.z * kTraceRange};
        const auto trace = Tracing::traceLine(eye.value(), end, static_cast<void*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())));
        if (!trace.didHit)
            return true;

        return shot_geometry::rayReachesSphere(eye.value(), direction, trace.endPos, kSeededFireRadius);
    }

    
    
    
    
    [[nodiscard]] bool passesMaxAccuracyGate() const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::MaxAccuracyOnly))
            return true;
        const bool pass = hookContext.activeLocalPlayerPawn().isAtMaxAccuracy();
        return pass;
    }

    
    
    
    
    
    
    
    
    [[nodiscard]] bool passesVisibility(auto&& target) const noexcept
    {
        const bool wallCheck = GET_CONFIG_VAR(triggerbot_vars::WallCheck);
        const int autowallThickness = GET_CONFIG_VAR(triggerbot_vars::Autowall) ? static_cast<int>(GET_CONFIG_VAR(triggerbot_vars::AutowallMaxThickness)) : 0;
        if (!wallCheck && autowallThickness <= 0)
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto eye = localPawn.eyePosition();
        if (!eye.hasValue())
            return true;

        auto&& node = target.baseEntity().gameSceneNode();
        auto bone = node.bonePosition(kHeadBone);
        if (!bone.hasValue())
            bone = node.bonePosition(kChestBone);
        if (!bone.hasValue())
            return true;

        void* const skip = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        void* const targetEntity = static_cast<cs2::C_BaseEntity*>(target.baseEntity());

        const auto forward = Tracing::traceLine(eye.value(), bone.value(), skip);
        
        if (!forward.didHit || forward.hitEntity == targetEntity)
            return true;

        
        if (autowallThickness <= 0)
            return false;

        
        if (forward.hitEntity != nullptr)
            return false;

        
        
        const auto back = Tracing::traceLine(bone.value(), eye.value(), skip);
        if (!back.didHit)
            return false;

        const float dx = forward.endPos.x - back.endPos.x;
        const float dy = forward.endPos.y - back.endPos.y;
        const float dz = forward.endPos.z - back.endPos.z;
        const float thickness = trig::squareRoot(dx * dx + dy * dy + dz * dz);
        const bool pass = thickness <= static_cast<float>(autowallThickness);
        return pass;
    }

    
    
    
    
    [[nodiscard]] bool onHead(auto&& target, const cs2::Vector& eye, float pitch, float yaw) const noexcept
    {
        const auto head = target.baseEntity().gameSceneNode().bonePosition(kHeadBone);
        if (!head.hasValue())
            return false;

        const auto forward = shot_geometry::angleVectors(pitch, yaw).forward;
        return shot_geometry::rayReachesSphere(eye, forward, head.value(), kHeadRadius);
    }

    
    
    
    
    [[nodiscard]] bool passesHitchance(auto&& target, auto&& localPawn, const cs2::Vector& eye, float pitch, float yaw, bool headOnly, int thresholdPercent) const noexcept
    {
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        if (!params.hasValue())
            return false;

        auto&& node = target.baseEntity().gameSceneNode();
        const auto bone = headOnly ? node.bonePosition(kHeadBone) : node.bonePosition(kChestBone);
        if (!bone.hasValue())
            return false;
        const auto radius = headOnly ? kHeadRadius : kBodyRadius;

        const auto basis = shot_geometry::angleVectors(pitch, yaw);

        int hits = 0;
        for (int sample = 0; sample < kHitchanceSamples; ++sample) {
            const auto spread = solver.estimatedSpreadOffset(static_cast<std::uint32_t>(sample), params.value());
            if (!spread.hasValue()) return false;
            const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
                basis.forward.x + basis.left.x * spread.value().x + basis.up.x * spread.value().y,
                basis.forward.y + basis.left.y * spread.value().x + basis.up.y * spread.value().y,
                basis.forward.z + basis.left.z * spread.value().x + basis.up.z * spread.value().y,
            });
            if (shot_geometry::rayReachesSphere(eye, direction, bone.value(), radius))
                ++hits;
        }

        return (hits * 100 / kHitchanceSamples) >= thresholdPercent;
    }

    
    
    [[nodiscard]] Optional<int> crosshairEntityIndex(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_iIDEntIndex");
        if (!offset.has_value() || *offset <= 0)
            return {};

        int index{};
        std::memcpy(&index, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(index));
        if (index <= 0 || index >= 32768)
            return {};
        return index;
    }

    
    
    
    
    
    [[nodiscard]] float delaySeconds() const noexcept
    {
        const auto a = static_cast<std::uint8_t>(GET_CONFIG_VAR(triggerbot_vars::DelayMilliseconds));
        const auto b = static_cast<std::uint8_t>(GET_CONFIG_VAR(triggerbot_vars::DelayMillisecondsMax));
        const auto lo = a < b ? a : b;
        const auto hi = a < b ? b : a;
        const auto span = static_cast<std::uint32_t>(hi - lo) + 1u; 
        const auto chosen = static_cast<std::uint8_t>(lo + static_cast<std::uint8_t>(nextRandom() % span));
        return static_cast<float>(chosen) / 1000.0f;
    }

    
    
    
    
    
    
    [[nodiscard]] std::uint64_t nextRandom() const noexcept
    {
        if (rngState == 0) {
            float seed = 1.0f;
            if (const auto now = hookContext.globalVars().curtime(); now.hasValue())
                seed = now.value();
            std::uint32_t seedBits{};
            std::memcpy(&seedBits, &seed, sizeof(seedBits));
            std::uint64_t z = (static_cast<std::uint64_t>(seedBits) << 1) | 1ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            rngState = z ^ (z >> 31);
            if (rngState == 0)
                rngState = 0x9E3779B97F4A7C15ull;
        }
        std::uint64_t x = rngState;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rngState = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    
    
    
    
    
    static constexpr int kHeadBone = 6;
    static constexpr int kChestBone = 4;
    static constexpr float kHeadRadius = 6.0f;
    static constexpr float kBodyRadius = 16.0f;
    static constexpr int kHitchanceSamples = 256;

    
    
    

    
    
    
    static constexpr float kSeededFireRadius = 6.0f;

    
    
    static void disarm() noexcept
    {
        armed = false;
        armedTarget = nullptr;
        fireAtTime = 0.0f;
    }

    
    
    

    
    inline static bool armed{false};
    inline static cs2::C_BaseEntity* armedTarget{nullptr};
    inline static float fireAtTime{0.0f};

    
    
    inline static std::uint64_t rngState{0};

    HookContext& hookContext;
};
