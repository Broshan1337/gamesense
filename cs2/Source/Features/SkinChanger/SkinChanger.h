#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/WeaponEntities.h>
#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Econ/ItemDefinitionIndex.h>
#include <CS2/Econ/PaintKitIndex.h>
#include <Features/SkinChanger/SkinChangerData.h>
#include <GameClient/Entities/BaseWeapon.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Utils/CrashLogger.h>
#include <Utils/MurmurHash2.h>

// Formats defIndex into a fixed stack buffer rather than using std::to_string(). This project
// links -nostdlib, so anything that can leave a non-inlined std::string behind pulls in
// operator delete(void*, size_t) and fails the link - a uint16 is at most 5 digits, so 6 bytes
// always suffices and there is no allocation to go wrong.
[[nodiscard]] inline std::uint32_t makeSubclassToken(std::uint16_t defIndex) noexcept
{
    char buffer[6];
    int length = 0;

    // Emit digits least-significant first, then reverse - avoids needing a separate
    // digit-count pass. Handles 0 correctly via the do/while.
    do {
        buffer[length++] = static_cast<char>('0' + defIndex % 10);
        defIndex /= 10;
    } while (defIndex != 0);

    for (int i = 0, j = length - 1; i < j; ++i, --j) {
        const char temp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = temp;
    }

    return murmurHash2Lower(buffer, length, 0x31415926);
}

// Applies the user's persisted per-weapon skin selections to every weapon the local player
// currently owns (not just the active one, and never other players' weapons - it only ever
// walks the local player's own m_hMyWeapons list). This is the "inventory changer" behavior:
// a skin choice is keyed by weapon type (ItemDefinitionIndex), not by a specific entity, so it
// gets (re-)applied automatically to any weapon of that type the player picks up, including a
// fresh one after dropping the old one.
template <typename HookContext>
class SkinChanger {
public:
    explicit SkinChanger(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        // Round/match teardown window: from round end through the map transition the game
        // frees and rebuilds model/skeleton state while skeleton worker jobs are in flight
        // (crash P265823/T265827 faulted in a bone-setup job on a libtier0 worker reading NULL
        // bone arrays right as a match ended). Skins need no reapplying during round end -
        // isRoundOver() pattern-fails to false (feature runs) if the offset is ever unresolved,
        // so the gate only ever fires on a confirmed round-over signal.
        if (hookContext.gameRules().isRoundOver().valueOr(false))
            return;

        // Real crash fix: the reference gates its entire weapon-processing block on
        // pawn->IsAlive() - we never did. A death-triggered crash (confirmed via coredump,
        // identical fault address to the earlier every-frame-reapply crash) is very plausibly
        // this: touching a weapon entity (queuing an async composite-material job via
        // regenerateSkin(), or any of the other writes) while it's mid-teardown from the
        // player dying. PlayerPawn::isAlive() already existed in this codebase, just never
        // wired into this gate.
        if (auto&& localPawn = hookContext.activeLocalPlayerPawn(); localPawn && localPawn.isAlive().value_or(false)) {
            // as<BaseWeapon>(), not cast<BaseWeapon>(): C_CSWeaponBase is an abstract base
            // class, not one of EntityClassifier's concrete leaf types, so the classified
            // cast<>() lookup can't resolve it (fails to compile, "T does not exist in
            // Tuple"). m_hMyWeapons only ever contains real weapon entities by construction,
            // so an unchecked reinterpret here is safe and matches how the rest of this
            // codebase gets a BaseWeapon from an already-known-to-be-a-weapon entity (e.g.
            // WeaponServices::getActiveWeapon()).
            localPawn.weaponServices().weapons().forEach([this, &localPawn](auto&& weaponEntity) {
                applyKnifeIfConfigured(localPawn, weaponEntity);
                applySkinIfConfigured(localPawn, weaponEntity.template as<BaseWeapon>());
            });
        }

        hookContext.skinChangerState().releaseDeadEntities([this](cs2::CEntityHandle handle) {
            return hookContext.template make<EntitySystem>().getEntityFromHandle(handle) != nullptr;
        });
    }

private:
    // sub_1FD2EA0 only ever reads a 16-bit index at offset+4290 of whatever pointer it's given,
    // so a zeroed local buffer with our desired index written at that one offset stands in for
    // a real item view - no live entity needed. Returns the resolved "model_player" path (read
    // at +328 per sub_1F85DD0's parsing code - see project notes) or nullptr if unresolved.
    [[nodiscard]] const char* resolveKnifeModelPath(std::uint16_t defIndex) const noexcept
    {
        const auto getItemDefinition = hookContext.patternSearchResults().template get<PointerToGetItemDefinitionByIndex>();
        if (!getItemDefinition)
            return nullptr;

        alignas(std::max_align_t) static unsigned char indexHolder[4300]{};
        *reinterpret_cast<std::uint16_t*>(indexHolder + 4290) = defIndex;

        const auto itemDefinition = getItemDefinition(indexHolder);
        if (!itemDefinition)
            return nullptr;

        return *reinterpret_cast<const char* const*>(reinterpret_cast<const unsigned char*>(itemDefinition) + 328);
    }

    // Mirrors the reference's GetHudWeapon(): walks the local player's first-person "arms"
    // entity's scene-node children for the one whose own m_hOwnerEntity points back at the
    // given weapon - a separate first-person viewmodel clone entity the reference updates
    // independently of the main weapon entity, which this project has never touched before
    // now. Invokes f with that BaseWeapon if a match is found; does nothing otherwise.
    template <typename F>
    void withHudWeapon(auto&& localPawn, cs2::CEntityHandle weaponHandle, F f) const noexcept
    {
        localPawn.hudModelArms().forEachChild([&](auto&& child) {
            if (child.ownerHandle().valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX}) == weaponHandle)
                f(child.template as<BaseWeapon>());
        });
    }

    // Impersonates the configured knife type (and optionally a finish) on the local player's
    // knife. Unlike applySkinIfConfigured(), this REPLACES the weapon's type rather than just
    // painting it, which is why it needs the model swap + subclass resolution below rather than
    // just an attribute write.
    void applyKnifeIfConfigured(auto&& localPawn, auto&& weaponEntity) const noexcept
    {
        if (weaponEntity.classify().typeIndex != EntityTypeInfo::indexOf<cs2::C_Knife>())
            return;

        const auto configuredKnife = SkinChangerData::configuredKnifeModel(hookContext);
        if (!configuredKnife.has_value())
            return;

        const auto desiredDefIndex = static_cast<int>(*configuredKnife);
        // 0 means "None" - a vanilla, unpainted knife. The model swap still applies; paint kit 0
        // is the schema's own "default"/no-finish kit, so writing it is correct rather than a
        // sentinel.
        const int configuredFinish = SkinChangerData::configuredKnifePaintKitId(hookContext);
        const auto desiredPaintKit = configuredFinish;

        // Validate the finish against the IMPERSONATED model's list (or the generic-knife set
        // when no model is selected) - per-model finish kits must never reach the engine on the
        // wrong knife type.
        if (desiredPaintKit != 0 && !SkinChangerData::isKnifePaintKitValid(configuredKnife, desiredPaintKit))
            return;

        const auto [configuredWearPermille, configuredSeed] = SkinChangerData::configuredKnifeWearAndSeed(hookContext);
        const int statTrak = GET_CONFIG_VAR(skin_changer_vars::StatTrakEnabled) ? GET_CONFIG_VAR(skin_changer_vars::StatTrakValue) : 0;

        const auto knife = weaponEntity.template as<BaseWeapon>();
        const auto handle = knife.baseEntity().handle();

        // CONFIRMED LIVE: removing the one-shot-per-entity gate and continuously re-applying
        // fixed the holding animation once - after rejoining, the knife rendered with the
        // correct mesh, the correct held/grip position, AND the HUD showed the real
        // "Karambit | Marble Fade" name. But a plain 1-second timer (tried next, after fixing
        // the crash this uncovered - see BaseEntity::isAlive() gate in run()) went back to
        // landing on the wrong animation consistently. That points at timing relative to an
        // actual equip transition mattering more than raw retry frequency - the one success was
        // right after a fresh rejoin, i.e. right after a real equip/spawn event, not from sheer
        // retry count. So: reapply immediately on a detected equip transition (the active
        // weapon handle just became this knife's handle), with the 1-second timer kept only as
        // a periodic fallback (e.g. if the mod loads mid-game with the knife already active, no
        // transition would ever be observed).
        static cs2::CEntityHandle lastActiveWeaponHandle{cs2::INVALID_EHANDLE_INDEX};
        const auto activeHandle = localPawn.weaponServices().activeWeaponHandle().valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX});
        const bool justEquipped = activeHandle == handle && lastActiveWeaponHandle != handle;
        lastActiveWeaponHandle = activeHandle;

        // THE STRUCTURAL DIFFERENCE FROM THE REFERENCE (found this round by re-diffing its
        // ProcessKnife() line by line, not by more RE). The reference gates its ENTIRE apply
        // block behind an "is this weapon already in the state I want?" check - item definition
        // index, entity quality, paint kit, wear, seed, subclass ID and model handle all
        // compared against the desired values - and returns immediately, doing nothing, when
        // they already match. This project has never had that check at all: it re-runs the whole
        // sequence (setModel, setItemDefinitionIndex, attribute writes, updateWeaponData,
        // updateSubclass, regenerateSkin, postDataUpdate, ...) ~6-7 times per second, forever,
        // on a weapon that is already fully correct.
        //
        // That is not a harmless inefficiency. updateWeaponData(1) destroys the first-person
        // hudmodel sub-entity whenever cached vdata doesn't match, and updateSubclass()
        // re-dispatches the subclass data object's own vtable+48 handler - doing either
        // repeatedly, several times a second, tears down and rebuilds animation-carrying state
        // continuously. An earlier round of this investigation proved exactly that failure mode
        // on a smaller scale: calling a single state-transition function every tick restarted its
        // transition forever and never let a settled pose play (it showed up as the player's arm
        // swinging back and forth). The same reasoning applies to the whole block.
        const auto desiredSubclassToken = makeSubclassToken(static_cast<std::uint16_t>(*configuredKnife));

        const bool alreadyInDesiredState =
            knife.itemDefinitionIndex().valueOr(0) == desiredDefIndex
            && knife.subclassID().valueOr(0) == desiredSubclassToken
            && knife.paintKit().valueOr(-1) == desiredPaintKit
            && knife.seed().valueOr(-1) == configuredSeed;

        // justEquipped still forces one pass, matching the reference's own forceUpdate argument -
        // a genuine deploy is exactly when re-applying is legitimate. Everything else falls
        // through to the throttle below only while the weapon is NOT yet in the desired state,
        // so the retry path that handles the real VData race (applySkinAttributes() returning
        // false on a freshly spawned weapon) is preserved untouched.
        if (!justEquipped && alreadyInDesiredState)
            return;

        // Unthrottled every-frame reapplication crashed the game after a while - two separate
        // coredumps, identical signature both times (a null indirect call deep in a generic
        // scene-node bounds-recompute pass inside libclient.so, while a DIFFERENT thread was
        // concurrently inside CThreadedJob::Abort) - a textbook async-job/dirty-node race.
        // regenerateSkin() queues an async composite-material rebuild job and marks scene state
        // dirty; calling it on literally every frame floods that queue. The isAlive() gate in
        // run() (which stops us touching the weapon during the death transition where the crash
        // was actually triggered) already closes off the specific trigger seen live, so this
        // interval is a second, independent safety margin on top of that, not the only thing
        // preventing the crash.
        //
        // 1x/sec (tried first, after fixing the crash) didn't reproduce the fix either, even
        // combined with equip-transition detection above. The one live success was with
        // unthrottled reapplication and took "some playtime" before it worked - i.e. it most
        // likely needed many repeated attempts, not just one well-timed one, consistent with
        // this being a race against some async completion (each attempt has a chance to "win"
        // it, not a guaranteed fix on the Nth try). Raised to ~6-7x/sec as a middle ground: fast
        // enough to plausibly win that race in a reasonable time, still far below the frequency
        // that caused the crash.
        static float lastReapplyAttemptTime = -1000.0f;
        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue())
            return;
        constexpr float kReapplyIntervalSeconds = 0.15f;
        const bool intervalElapsed = curtime.value() - lastReapplyAttemptTime >= kReapplyIntervalSeconds;
        if (!justEquipped && !intervalElapsed)
            return;
        lastReapplyAttemptTime = curtime.value();

        const char* modelPath = resolveKnifeModelPath(static_cast<std::uint16_t>(*configuredKnife));

        // Tested and disproven: skipping SetModel() entirely (mesh staying default) did NOT
        // restore the correct holding animation - it stayed wrong (and the HUD label stayed
        // generic "Knife" too), ruling out "SetModel() itself clobbers the animation" as the
        // explanation. Reverted back to always calling it.
        //
        // 2026-09-08 hardening on top of that: SetModel is now gated on the model handle
        // actually differing from the one our previous SetModel produced. The retry loop
        // (applySkinAttributes returning false while the weapon's VData is unresolved - which
        // is exactly the state a freshly-spawned pawn's knife is in during map load) used to
        // re-fire SetModel every ~150ms; each call rebuilds model state and races the
        // skeleton/animation worker jobs (observed live as a CSkeletonInstance bone-array
        // read from NULL on a worker thread, faulting at m_modelState+0x68 during
        // latched-parent-space generation - crash P265823/T265827). With the handle gate the
        // steady state and the attrs-retry loop both stop swapping models that are already
        // ours; a game-side model change (respawn/re-equip) naturally flips the handle and
        // lets the swap through.
        static std::uint64_t lastSetModelHandle = 0;
        const auto currentHandle = knife.modelHandle();
        if (currentHandle != 0 && currentHandle == lastSetModelHandle) {
            knife.setItemDefinitionIndex(desiredDefIndex);
        } else {
            CrashLogger::trace(0x340); // SetModel about to fire (0x341 = done) - attribution for
            knife.setModel(modelPath); // future skeleton-job crash traces
            CrashLogger::trace(0x341);
            lastSetModelHandle = knife.modelHandle();
            knife.setItemDefinitionIndex(desiredDefIndex);
        }
        // applySkinAttributes() returns false (without crashing) when the weapon's VData isn't
        // resolved yet - see BaseWeapon::regenerateSkin()'s crash-fix comment for the full RE
        // trail. Don't latch the one-shot guard in that case, so this retries next frame instead
        // of silently failing forever.
        if (!knife.applySkinAttributes(desiredPaintKit, configuredSeed, static_cast<float>(configuredWearPermille) / 1000.0f))
            return;

        // applySkinAttributes() unconditionally writes m_iEntityQuality=0; the reference wants
        // 3 (QUALITY_UNUSUAL) for knives specifically (StatTrak overrides with 9 = STRANGE), so
        // this must come after it.
        knife.setEntityQuality(statTrak ? 9 : 3);
        if (statTrak)
            knife.setStatTrak(statTrak);
        knife.updateWeaponData(1);
        knife.setSubclassID(desiredSubclassToken);

        // THE FIX THIS ROUND, and the first hypothesis in this whole investigation backed by a
        // diagnostic rather than by reasoning about symptoms.
        //
        // The live log proved m_pSubclassVData never leaves the GENERIC knife's VData
        // ("weapon_knife") even after itemDefinitionIndex correctly becomes 507. That is the
        // whole bug: the grip pose is chosen pawn-side by CCS2PawnGraphController, whose
        // weapon_type/weapon_category handler (sub_158AA40) reads a name string straight off the
        // weapon's VData and hashes it. Generic VData in, generic grip out - no matter how
        // correct the mesh (we force it via setModel), the econ item, or the subclass ID are.
        //
        // Why updateSubclass() alone was never going to do it - straight from sub_DDE400's
        // decompile, the function updateSubclass() actually calls:
        //     if ((*(uint8*)(entityIdentity + 48) & 0x10) == 0)
        //         ResolveSubclassData(this);          // sub_D9A070 - the ONLY writer of
        //                                             // m_pSubclassVData (weapon + 1272)
        //     if (m_pSubclassVData) vtable[6](m_pSubclassVData, spawnData);
        // ResolveSubclassData is GATED behind that 0x10 flag bit. On an entity whose subclass
        // data was already resolved once at spawn time, the flag is set, so every subsequent
        // updateSubclass() call skips re-resolution entirely and just re-dispatches the OLD,
        // generic VData's vtable+6. We have been calling a function that, for our use case,
        // structurally cannot update the field we need.
        //
        // This also finally explains the pattern that has confused this investigation for
        // sessions: every genuine success (a rejoin, a fresh console `give`) happened on a
        // BRAND NEW entity - one that had not resolved its subclass data yet, so the gate was
        // still open. It was never about deploy events or timing at all.
        //
        // resolveSubclassData() calls sub_D9A070 DIRECTLY, bypassing the gate, so the write to
        // m_pSubclassVData always happens. It has been wired up in BaseWeapon for a long time
        // but was dropped from this path in an early session in favour of updateSubclass(), on
        // the reasonable-at-the-time belief that updateSubclass() was a strict superset. It is
        // not - it is a superset only while that flag is clear.
        //
        // updateSubclass() is still called afterwards: its vtable+6 dispatch on the (now correct)
        // VData is a real step resolveSubclassData() does not perform.
        //
        // CONFIRMED LIVE: with this call in place the knife resolves to the real Karambit VData
        // ("weapon_knife_karambit", and a different VData pointer than the generic knife's) and
        // is held in the correct reverse grip. This closed the longest-running bug in the
        // project. Do not "simplify" this back down to updateSubclass() alone.
        knife.resolveSubclassData();
        knife.updateSubclass();

        // Order matters here - see BaseWeapon::applySkinAttributes()'s comment for the full RE
        // trail: the mesh group mask must be set BEFORE regenerateSkin() rebuilds the composite
        // material, or legacy-only submeshes never get a properly bound material at all.
        const std::uint64_t meshMask = knife.isEquippedPaintKitLegacy() ? 2 : 1;
        knife.setMeshGroupMask(meshMask);

        // The reference also calls set_model() on a completely separate entity: the local
        // player's first-person viewmodel clone of this weapon (found by walking
        // m_hHudModelArms' scene-node children for the one owned by this weapon).
        withHudWeapon(localPawn, handle, [modelPath, meshMask](auto&& hudWeapon) {
            hudWeapon.setModel(modelPath);
            hudWeapon.setMeshGroupMask(meshMask);
        });

        // REQUIRED, not optional - regenerateSkin() is a silent no-op without it on any weapon
        // that has already been skinned once. Proven from RegenerateWeaponSkin's (sub_1466900)
        // own disassembly, which bails to its epilogue before doing any work if either pending
        // composite-material counter is positive:
        //     mov ecx, [rbx+0A28h] / test ecx, ecx / jg <epilogue>
        //     mov edx, [rbx+0A40h] / test edx, edx / jg <epilogue>
        // Those are weapon+2600 and weapon+2624, which are exactly owner+672 and owner+696 for
        // the CCompositeMaterialOwner at weapon+1928 - the two counters updateCompositeMaterial()
        // (sub_40F1590) drains and zeroes. So it is the prerequisite that lets the regenerate run
        // at all on a reused entity, which is why the game's own regenerate_weapon_skins handler
        // pairs the two calls in exactly this order.
        //
        // This was briefly reverted on suspicion of contributing to a death-triggered crash;
        // that suspicion was never supported by evidence (the crash site was proven NOT to be in
        // any function this code calls) and removing it silently broke live skin updates - the
        // skin only changed after a death or re-pickup, i.e. only ever on a fresh entity whose
        // counters were still zero. Do not remove it again.
        knife.updateCompositeMaterial();

        if (!knife.regenerateSkin())
            return;
        hookContext.skinChangerState().markApplied(handle, desiredPaintKit, configuredSeed, configuredWearPermille, statTrak);

        // NEW this round - the reference's exact final step after the composite-material
        // rebuild (UpdateCompositeMaterial/Set -> UpdateSkin(true) -> scene->PostDataUpdate()),
        // never called on this project before. See GameSceneNode::postDataUpdate()'s comment
        // for the full RE trail (a vtable-slot candidate, not yet live-confirmed to fix the
        // animation - the leading untested lead going into this round, since none of the prior
        // 7 disproven hypotheses touched this call at all).
        knife.baseEntity().gameSceneNode().postDataUpdate();

        // REMOVED: cycleOwnerHandle() (a raw m_hOwnerEntity invalid-then-back toggle) used to sit
        // here. It was the last call in this block that the reference implementation does not
        // make, and it only survived this long because it happened to be active during the one
        // historical test that first produced a correct holding animation - i.e. correlation,
        // never a demonstrated mechanism. The actual cause of that success is now known and
        // fixed properly (resolveSubclassData() bypassing the ResolveSubclassData flag gate), so
        // the correlation is fully explained without it. m_hOwnerEntity is also a NetworkVar, and
        // this project has repeatedly established that raw writes to NetworkVar-backed fields do
        // not invoke the real change-notification path - the same reason the m_hActiveWeapon
        // toggle below was removed. BaseEntity::cycleOwnerHandle() itself is left in place,
        // unused, since it is correctly implemented if a real reason for it ever turns up.
        //
        // REMOVED earlier: the m_hActiveWeapon holster-then-redeploy toggle that lived here.
        // It was live-tested and did nothing (mesh/skin/HUD correct, grip pose still wrong), and
        // the reason is now understood - m_hActiveWeapon is a genuine schema NetworkVar with a
        // registered "OnActiveWeaponChanged" callback, so a raw write to it never invokes the
        // real change-notification path, matching every other NetworkVar-backed field in this
        // project (m_nSubclassID needed UpdateSubclass(), attributes needed
        // SetAttributeValueByName). It was also a call the reference implementation does not make
        // at all. Keeping a disproven raw NetworkVar write in the apply path is pure noise, and
        // it actively muddies this round's test - removed. WeaponServices::setActiveWeaponHandle()
        // itself is left in place, unused, since it's correctly implemented and cheap to re-try
        // if some future finding gives a real reason to.
        //
        // With both of those gone, this block is now a faithful port of the reference's
        // ProcessKnife() plus resolveSubclassData(), with no speculative extra writes left.
    }

    void applySkinIfConfigured(auto&& localPawn, auto&& weapon) const noexcept
    {
        const auto defIndex = weapon.itemDefinitionIndex();
        if (!defIndex.hasValue())
            return;

        // configuredPaintKitId() keeps the two cases the old selection enum collapsed apart:
        // nullopt means "this weapon has no config variable" - leave it completely alone.
        // Some(0) means the user chose "None", which must REVERT rather than be ignored.
        const auto configuredKit = SkinChangerData::configuredPaintKitId(hookContext, static_cast<cs2::ItemDefinitionIndex>(defIndex.value()));
        if (!configuredKit.has_value())
            return;

        // Defense in depth: a config value that isn't in the weapon's PaintKitDatabase list
        // (e.g. a stale value saved by an older build) must never reach the engine - an
        // incompatible weapon+kit combination SIGFPEs the composite-material system.
        if (*configuredKit != 0 && !SkinChangerData::isPaintKitValidFor(static_cast<cs2::ItemDefinitionIndex>(defIndex.value()), *configuredKit))
            return;

        const auto handle = weapon.baseEntity().handle();

        // Capture what this weapon looked like before we ever touched it (full kit/seed/wear
        // triple). No-op after the first call for a given entity, so our own writes can never
        // become the "original".
        const auto originalSeed = weapon.seed().valueOr(0);
        const auto originalWear = weapon.wear().valueOr(0.0f);
        hookContext.skinChangerState().rememberOriginalPaintKit(handle,
            static_cast<int>(weapon.paintKit().valueOr(0)),
            originalSeed,
            static_cast<int>(originalWear * 1000.0f + 0.5f));

        // Live updating falls straight out of this: the desired state is recomputed from config
        // every pass, so changing the selection mid-game makes desired != applied on the very next
        // frame and the weapon re-skins itself, with no equip/respawn needed. Choosing "None"
        // makes the CAPTURED ORIGINAL state the desired state (the real seed/wear the weapon
        // arrived with - not neutral values, which would corrupt real inventory skins), so it
        // reverts by the same mechanism rather than being a special case.
        const auto [configuredWearPermille, configuredSeed] = SkinChangerData::configuredWearAndSeed(hookContext, static_cast<cs2::ItemDefinitionIndex>(defIndex.value()));
        const int statTrak = GET_CONFIG_VAR(skin_changer_vars::StatTrakEnabled) ? GET_CONFIG_VAR(skin_changer_vars::StatTrakValue) : 0;

        const auto original = hookContext.skinChangerState().originalState(handle).value_or(SkinChangerState::OriginalState{0, 0, 10});
        const int desiredPaintKit = *configuredKit != 0 ? *configuredKit : original.paintKit;
        const int desiredSeed = *configuredKit != 0 ? configuredSeed : original.seed;
        const int desiredWearPermille = *configuredKit != 0 ? configuredWearPermille : original.wearPermille;

        if (hookContext.skinChangerState().alreadyApplied(handle, desiredPaintKit, desiredSeed, desiredWearPermille, statTrak))
            return;

        // applySkinAttributes() returns false (without crashing) when the weapon's VData isn't
        // resolved yet - a real race on freshly spawned weapons (e.g. die -> rebuy the same
        // weapon), see BaseWeapon::regenerateSkin()'s crash-fix comment for the full RE trail.
        // Don't mark this weapon "applied" in that case, so the next frame's pass retries it.
        if (!weapon.applySkinAttributes(desiredPaintKit, desiredSeed, static_cast<float>(desiredWearPermille) / 1000.0f))
            return;

        // StatTrak: m_iEntityQuality must be 9 (STRANGE) and the fallback counter set.
        if (statTrak) {
            weapon.setEntityQuality(9);
            weapon.setStatTrak(statTrak);
        }

        // The reference calls UpdateWeaponData()/UpdateVData() right after writing paint/wear/
        // seed, for every weapon (not just knives) - see UpdateWeaponData's declaration comment
        // (C_CSWeaponBase.h) for the full RE trail (found via the "Re-creating weapon hudmodel
        // due to vdata subclass model change." string, confirmed by full decompile to
        // unconditionally re-derive and, if needed, re-apply the weapon's model regardless of
        // changeType). Guns never had any call into this at all before. changeType=0 - guns
        // don't go through a subclass swap, so the changeType==1 stale-hudmodel cleanup doesn't
        // apply, but the unconditional model-refresh check underneath still runs.
        weapon.updateWeaponData(0);

        // Order matters here - see BaseWeapon::applySkinAttributes()'s comment for the full RE
        // trail: the mesh group mask must be set BEFORE regenerateSkin() rebuilds the composite
        // material (confirmed by diffing against the reference's exact call order), or
        // legacy-only submeshes (e.g. the stock on Asiimov M4A4) never get a properly bound
        // material at all - rebuilding while still in the default mesh group leaves them
        // pointing at whatever fallback material the model ships with.
        const std::uint64_t meshMask = weapon.isEquippedPaintKitLegacy() ? 2 : 1;
        weapon.setMeshGroupMask(meshMask);
        withHudWeapon(localPawn, handle, [meshMask](auto&& hudWeapon) {
            hudWeapon.setMeshGroupMask(meshMask);
        });

        // Required here for the same reason as in applyKnifeIfConfigured() - see that call site
        // for the full disassembly-level explanation. Without it, regenerateSkin() silently does
        // nothing on any weapon already skinned once, which is exactly what made gun skins only
        // change after a death or re-pickup instead of live.
        weapon.updateCompositeMaterial();

        if (!weapon.regenerateSkin())
            return;
        hookContext.skinChangerState().markApplied(handle, desiredPaintKit, desiredSeed, desiredWearPermille, statTrak);

        // NEW this round - see the matching call in applyKnifeIfConfigured() for the full
        // RE trail. The reference calls this after every weapon's skin apply, not just knives.
        weapon.baseEntity().gameSceneNode().postDataUpdate();
    }

    HookContext& hookContext;
};
