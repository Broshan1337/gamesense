#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CAttributeList.h>
#include <CS2/Classes/CEconItemAttribute.h>
#include <CS2/Classes/CModelState.h>
#include <CS2/Classes/Entities/C_CSWeaponBase.h>
#include <CS2/Classes/CCSWeaponBaseVData.h>
#include <CS2/Classes/Vector.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <MemoryPatterns/PatternTypes/WeaponVDataPatternTypes.h>
#include <Utils/RetAddrSpoofer.h>
#include "BaseEntity.h"
#include "EntityClassifier.h"

template <typename HookContext>
class BaseWeapon {
public:
    BaseWeapon(HookContext& hookContext, cs2::C_CSWeaponBase* baseWeapon) noexcept
        : hookContext{hookContext}
        , baseWeapon{baseWeapon}
    {
    }

    using RawType = cs2::C_CSWeaponBase;

    template <template <typename...> typename EntityType>
    [[nodiscard]] bool is() const noexcept
    {
        return baseEntity().template is<EntityType>();
    }

    template <template <typename...> typename EntityType>
    [[nodiscard]] decltype(auto) cast() const noexcept
    {
        return baseEntity().template cast<EntityType>();
    }

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(baseWeapon);
    }

    [[nodiscard]] bool isSniperRifle() const noexcept
    {
        switch (baseEntity().classify().typeIndex) {
        case EntityTypeInfo::indexOf<cs2::C_WeaponSSG08>():
        case EntityTypeInfo::indexOf<cs2::C_WeaponAWP>():
        case EntityTypeInfo::indexOf<cs2::C_WeaponG3SG1>():
        case EntityTypeInfo::indexOf<cs2::C_WeaponSCAR20>(): return true;
        default: return false;
        }
    }

    [[nodiscard]] auto bulletInaccuracy() const noexcept
    {
        return inaccuracy() + spread();
    }

    [[nodiscard]] auto getName() const noexcept
    {
        const auto vData = static_cast<cs2::CCSWeaponBaseVData*>(hookContext.template make<BaseEntity>(baseWeapon).vData().valueOr(nullptr));
        return hookContext.patternSearchResults().template get<OffsetToWeaponName>().of(vData).valueOr(nullptr);
    }

    [[nodiscard]] auto clipAmmo() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToClipAmmo>().of(baseWeapon).toOptional();
    }

    [[nodiscard]] auto paintKit() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackPaintKit.of(baseWeapon).toOptional();
    }

    void setPaintKit(int paintKit) const noexcept
    {
        hookContext.econEntityOffsets().fallbackPaintKit.of(baseWeapon) = paintKit;
    }

    [[nodiscard]] auto seed() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackSeed.of(baseWeapon).toOptional();
    }

    void setSeed(int seed) const noexcept
    {
        hookContext.econEntityOffsets().fallbackSeed.of(baseWeapon) = seed;
    }

    [[nodiscard]] auto wear() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackWear.of(baseWeapon).toOptional();
    }

    void setWear(float wear) const noexcept
    {
        hookContext.econEntityOffsets().fallbackWear.of(baseWeapon) = wear;
    }

    [[nodiscard]] auto statTrak() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackStatTrak.of(baseWeapon).toOptional();
    }

    void setStatTrak(int statTrak) const noexcept
    {
        hookContext.econEntityOffsets().fallbackStatTrak.of(baseWeapon) = statTrak;
    }

    // The real, currently-equipped item definition index (weapon type) for this specific
    // weapon entity - used by SkinChanger to look up whether this weapon has a configured
    // skin. Reads through the same offset chain as setSkin().
    [[nodiscard]] auto itemDefinitionIndex() const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        return offsets.itemDefinitionIndex.of(item).toOptional();
    }

    // EXPERIMENTAL, never exercised before this project - a raw write to the same
    // schema-resolved m_iItemDefinitionIndex field itemDefinitionIndex() reads. Unlike
    // setSkin()'s attribute writes (which go through SetAttributeValueByName's own
    // bookkeeping/cache-invalidation), this bypasses that entirely - it's the direct-write
    // approach already known to be risky (see setSkin()'s comment on the crash that pattern
    // caused for skins). Exists to answer one open question: does overwriting this field alone
    // (plus the existing regenerate call) change which knife model actually renders, or is the
    // model resolved/cached elsewhere at spawn time. Not for production use until answered.
    void setItemDefinitionIndex(int defIndex) const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        if (!item)
            return;
        offsets.itemDefinitionIndex.of(item) = defIndex;
    }

    // EXPERIMENTAL - matches the reference implementation's item->m_iEntityQuality() = 3
    // (QUALITY_UNUSUAL) for knives specifically (guns use 0). setSkin() below unconditionally
    // writes 0 to this same field, so this must be called AFTER setSkin() to stick for knives.
    void setEntityQuality(int quality) const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        if (!item)
            return;
        offsets.entityQuality.of(item) = quality;
    }

    // EXPERIMENTAL - raw write to C_BaseEntity::m_nSubclassID (a CUtlStringToken hash, modeled
    // as a plain uint32 - see EntitySubclassOffsets.h). A reference implementation for this
    // exact feature (knife model swap, Windows build) sets this field then calls a real
    // UpdateSubclass() method we have not yet located on Linux - this write alone is being
    // tried first, cheaply, to see whether it's sufficient on its own before spending more
    // effort hunting for that method.
    void setSubclassID(std::uint32_t hash) const noexcept
    {
        hookContext.entitySubclassOffsets().subclassID.of(baseWeapon) = hash;
    }

    // Reads back the same field setSubclassID() writes. Added so the skin changer can tell
    // whether a weapon is ALREADY in its desired state and skip re-applying - the reference
    // implementation gates its whole apply block on exactly this kind of comparison, and this
    // project never had a getter for this field at all.
    [[nodiscard]] auto subclassID() const noexcept
    {
        return hookContext.entitySubclassOffsets().subclassID.of(baseWeapon).toOptional();
    }

    // EXPERIMENTAL - see ResolveSubclassData's declaration comment (C_CSWeaponBase.h) and the
    // pattern's comment (WeaponPatternsLinux.h) for the full RE trail. Call after setSubclassID()
    // to see if it alone is enough to refresh the entity's cached subclass data (and, if we're
    // right about what that data drives, the rendered model) without needing the rest of the
    // real subclass-resolution path this bypasses.
    void resolveSubclassData() const noexcept
    {
        if (const auto resolve = hookContext.patternSearchResults().template get<PointerToResolveSubclassData>(); resolve && baseWeapon)
            resolve(baseWeapon);
    }

    // The real UpdateWeaponData(). See UpdateWeaponData's declaration comment (C_CSWeaponBase.h)
    // for the full RE trail - it's not just a HUD-icon cleanup helper, it unconditionally
    // re-derives and (if needed) applies the weapon's currently-desired model regardless of
    // changeType. changeType=1 additionally destroys a stale hudmodel sub-entity when its
    // cached vdata no longer matches - only meaningful after a genuine subclass swap (knives);
    // changeType=0 skips that but still runs the model-refresh check, safe for any weapon.
    void updateWeaponData(unsigned int changeType) const noexcept
    {
        if (const auto update = hookContext.patternSearchResults().template get<PointerToUpdateWeaponData>(); update && baseWeapon)
            update(baseWeapon, changeType);
    }

    // EXPERIMENTAL - the real UpdateSubclass(). See UpdateSubclass's declaration comment
    // (C_CSWeaponBase.h) for the full RE trail. Passes nullptr for the spawn-data argument -
    // safe here specifically because our own setSubclassID() write already means the one code
    // path that would dereference it (computing the hash from scratch) gets skipped.
    void updateSubclass() const noexcept
    {
        if (const auto update = hookContext.patternSearchResults().template get<PointerToUpdateSubclass>(); update && baseWeapon) {
            // Guard against a mis-anchored pattern resolution: a pointer landing on the
            // 0xCC padding before a function executes int3 as its first instruction and
            // SIGTRAPs the whole game. Skipping the call turns that into a cosmetic
            // "subclass not updated" instead of a map-load crash (see the PointerToUpdateSubclass
            // pattern comment in WeaponPatternsLinux.h for the 2026-08-25 incident).
            if (*static_cast<const unsigned char*>(update.rawAddress()) == 0xCC)
                return;
            update(baseWeapon, nullptr);
        }
    }

    // Writes CModelState::m_MeshGroupMask (1 = default, 2 = legacy paint kit - matches the
    // reference's IsLegacyPaintKit() check, see SkinChangerData::isLegacyPaintKit()) through
    // the fully schema-resolved chain: weapon -> m_CBodyComponent -> m_skeletonInstance ->
    // m_modelState -> m_MeshGroupMask. 2026-09-27: this replaces the pre-update vtable+112
    // virtual call ("slot 14, identified empirically") - the 5GB update reshuffled that
    // vtable, slot 14 became a STATIC-POINTER getter (returns 0x46798A0 on build 68f386a6),
    // and the mask write through it (+0x248) clobbered the CBaseAnimGraph_API registry at
    // 0x4679AE8 with the value 2 -> the game's map-load animgraph walk called the poisoned
    // entry -> the rip=2 crashes (caught with a gdb hardware watchpoint on the slot). All
    // schema offsets = update-proof; no virtual call, no empirical vtable slots.
    void setMeshGroupMask(std::uint64_t mask) const noexcept
    {
        const auto& offsets = hookContext.modelStateOffsets();
        const auto bodyComponent = offsets.bodyComponent.of(baseWeapon).valueOr(nullptr);
        if (!bodyComponent)
            return;

        // A weapon's body component is a CBodyComponentSkeletonInstance (the schema class
        // whose m_skeletonInstance the next step reads).
        const auto skeletonInstance = offsets.skeletonInstance.of(reinterpret_cast<cs2::CBodyComponentSkeletonInstance*>(bodyComponent)).get();
        if (!skeletonInstance)
            return;
        const auto modelState = offsets.modelState.of(skeletonInstance).get();
        if (!modelState)
            return;
        offsets.meshGroupMask.of(modelState) = mask;
    }

    // Reads back CModelState::m_hModel (the u64 resource handle at +0xA0 - the same field
    // AgentChanger captures/restores) through the same body-component vtable path as
    // setMeshGroupMask(). Lets callers skip SetModel when the model is ALREADY the one they
    // set - every SetModel rebuilds model state, and each rebuild is a standing race window
    // against the skeleton/animation worker jobs (bone arrays read NULL mid-rebuild).
    [[nodiscard]] std::uint64_t modelHandle() const noexcept
    {
        const auto& offsets = hookContext.modelStateOffsets();
        const auto bodyComponent = offsets.bodyComponent.of(baseWeapon).valueOr(nullptr);
        if (!bodyComponent)
            return 0;

        // 2026-09-27: same schema-chain replacement as setMeshGroupMask (the old vtable+112
        // slot reshuffled in the 5GB update - see that comment for the crash trail).
        const auto skeletonInstance = offsets.skeletonInstance.of(reinterpret_cast<cs2::CBodyComponentSkeletonInstance*>(bodyComponent)).get();
        if (!skeletonInstance)
            return 0;
        const auto modelState = offsets.modelState.of(skeletonInstance).get();
        if (!modelState)
            return 0;

        std::uint64_t handle = 0;
        return offsets.modelHandle.of(modelState).valueOr(handle);
    }

    // The real, schema+RE-confirmed replacement for SkinChangerData::isLegacyPaintKit()'s
    // earlier guess. Calls the game's own sub_1FD4AE0 (see PointerToGetPaintKitDefinition's
    // comment for the full RE trail) on this weapon's embedded item-view pointer to resolve
    // the actual currently-equipped paint-kit definition, then reads its use_legacy_model bool
    // directly at offset 174 - the same field the reference IsLegacyPaintKit()/UsesOldModel()
    // check reads on Windows. Must be called AFTER setSkin() - it reads whatever paint kit is
    // presently attached to the item, not a value passed in directly.
    [[nodiscard]] bool isEquippedPaintKitLegacy() const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        if (!item)
            return false;

        const auto getPaintKitDefinition = hookContext.patternSearchResults().template get<PointerToGetPaintKitDefinition>();
        if (!getPaintKitDefinition)
            return false;

        const auto paintKitDefinition = getPaintKitDefinition(item);
        if (!paintKitDefinition)
            return false;

        return *(reinterpret_cast<const unsigned char*>(paintKitDefinition) + 174) != 0;
    }

    // EXPERIMENTAL - candidate SetModel(). See SetModel's declaration comment (C_CSWeaponBase.h)
    // for the full RE trail.
    void setModel(const char* modelPath) const noexcept
    {
        if (const auto setModelFn = hookContext.patternSearchResults().template get<PointerToSetModel>(); setModelFn && baseWeapon && modelPath)
            setModelFn(baseWeapon, modelPath);
    }

    // Uses the game's own internal attribute-write path (C_EconItemView::SetAttributeValueByName,
    // found via string xref - see WeaponPatternsLinux.h) instead of hand-writing a fake
    // CEconItemAttribute array into m_Attributes. A raw write was confirmed working for the
    // visual result, but skips real bookkeeping the proper path performs: resolving the
    // attribute definition by name, updating an existing entry or growing the item's own
    // array through its own allocator, and invalidating a per-item cached object whenever any
    // attribute changes. That gap is the leading suspect for a real, symbol-confirmed crash
    // (a near-null pointer read deep in the scene system's LOD-selection code, on a later,
    // unrelated frame - never synchronous with our own write) that a raw array write could
    // never fully explain. See project notes for the full RE trail.
    //
    // Split from regenerateSkin() (below) specifically so callers can set the mesh group mask
    // in between the two - the composite material rebuild needs to happen AFTER the mesh mask
    // is set, matching the reference's exact order (SetMeshGroupMask, then
    // UpdateCompositeMaterial/UpdateCompositeMaterialSet/UpdateSkin). Rebuilding before the
    // mask was set (this project's original single-function order) rebuilds the composite
    // material against whatever mesh group was already active - for a freshly-equipped weapon
    // that's always the default group, even when the paint kit is legacy - leaving legacy-only
    // submeshes (confirmed live: the stock on an Asiimov M4A4) with no properly-bound material
    // at all, while the submeshes shared with the default group still paint correctly.
    bool applySkinAttributes(int paintKit, int seed, float wear) const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();

        const auto setAttribute = hookContext.patternSearchResults().template get<PointerToSetAttributeValueByName>();
        if (!setAttribute || !item)
            return false;

        // Item metadata fields the reference always sets alongside the skin attributes -
        // added after two cheaper animation-fix hypotheses (SetModel presence, an
        // ownership-toggle nudge) were both tested live and disproven; the reference is
        // confirmed to render both the skin AND the holding animation correctly, so porting
        // its full field set faithfully - not just the 3 skin attributes - is the next real
        // step. The reference's item id is a FAUX id (high 0xf0000000 / low 0x10), not
        // 0xFFFFFFFF: the HUD (weapon-select row icon/name) resolves item details through the
        // item id, and an invalid/zero id leaves the row on the generic fallback ("Knife") -
        // m_bInitialized=true is required for the same reason (the reference sets it on every
        // apply; leaving the view "uninitialized" makes the HUD treat it as having no item).
        offsets.itemIDHigh.of(item) = 0xF0000000u;
        offsets.itemIDLow.of(item) = 0x10u;
        offsets.initialized.of(item) = true;
        offsets.accountID.of(item) = offsets.originalOwnerXuidLow.of(baseWeapon).valueOr(0u);
        offsets.disallowSOC.of(item) = true;
        offsets.restoreCustomMaterialAfterPrecache.of(item) = true;

        offsets.entityQuality.of(item) = 0;

        setAttribute(item, "set item texture prefab", static_cast<float>(paintKit));
        setAttribute(item, "set item texture wear", wear);
        setAttribute(item, "set item texture seed", static_cast<float>(seed));

        // The reference's CURRENT code writes the real paint kit/wear/seed values here, not -1
        // - a genuine discrepancy from ours found by re-diffing after three animation-fix
        // hypotheses (SetModel presence, ownership-toggle nudge, item metadata fields) were all
        // tested live and disproven. An EARLIER version of this project deliberately reverted
        // this same write from real-value back to -1 (see project notes,
        // "matching the reference implementation's post-build cleanup") - that matched an
        // OLDER version of the reference at the time, not the confirmed-working current one.
        // Phase 1 of this project separately confirmed fallback fields alone don't drive the
        // live MESH render path - that's still true and unaffected by this change - but that
        // finding never tested whether animation/pose selection reads this field.
        hookContext.econEntityOffsets().fallbackPaintKit.of(baseWeapon) = paintKit;
        hookContext.econEntityOffsets().fallbackSeed.of(baseWeapon) = seed;
        hookContext.econEntityOffsets().fallbackWear.of(baseWeapon) = wear;
        return true;
    }

    // The real UpdateCompositeMaterial (sub_40F1590) - see UpdateCompositeMaterial's declaration
    // comment (C_CSWeaponBase.h) for what it does and the full RE trail. Flushes the weapon's
    // embedded CCompositeMaterialOwner's pending composite-material job queue before
    // regenerateSkin() rebuilds it. Must be called immediately BEFORE regenerateSkin(): that's
    // both the reference implementation's own order and, more importantly, exactly what the
    // game's own regenerate_weapon_skins ConCommand handler does per weapon.
    //
    // This project has only ever reached this function indirectly, through regenerateSkin()'s own
    // internal conditional fallback branch, which our call path never actually satisfies - so the
    // flush has never once happened on the path this feature uses. The bool is passed true,
    // matching both the ConCommand handler and the reference; the step it gates is guarded
    // against the base class's default no-op inside the function itself, so true is safe here.
    void updateCompositeMaterial() const noexcept
    {
        const auto update = hookContext.patternSearchResults().template get<PointerToUpdateCompositeMaterial>();
        if (!update || !baseWeapon)
            return;

        update(reinterpret_cast<unsigned char*>(baseWeapon) + cs2::C_CSWeaponBase::kCompositeMaterialOwnerOffset, true);
    }

    // Triggers the actual composite material rebuild (RegenerateWeaponSkin). Call this AFTER
    // applySkinAttributes(), setMeshGroupMask() and updateCompositeMaterial() - see
    // applySkinAttributes()'s comment for why the order matters.
    //
    // NOTE for symptom archaeology: between the 2026-09-23 re-anchor and the 2026-09-27 fix,
    // PointerToRegenerateWeaponSkin resolved to the regenerate_weapon_skins ConCommand
    // HANDLER (an all-weapons iterator taking only a bool in dil) instead of this per-weapon
    // fn - so calls in that window ran the iterator with the weapon pointer's low byte as
    // forceHighRes (usually nonzero = the high-res/async-heavy path). Any "skins flaky /
    // weird perf on apply / teardown-adjacent SEGVs" report from that window maps here.
    // Pattern re-anchored on the true per-weapon fn 2026-09-27 (see WeaponPatternsLinux.h).
    //
    // Returns false if the regenerate call had to be skipped - a real crash fix, confirmed via
    // a coredump: RegenerateWeaponSkin (sub_1466900) unconditionally reads
    // *(m_pSubclassVData + 1312) near its very start (crash site libclient.so+0x146698a matches
    // exactly "n6 = *(a1[159]+1312)" from this project's own earlier decompile of the function -
    // a1[159] is m_pSubclassVData, offset 1272). On a freshly spawned weapon (e.g. die -> weapon
    // changes hands -> rebuy the same weapon), the engine hasn't resolved that field yet by the
    // time our own per-frame SkinChanger loop reaches it, so it's null and this read segfaults.
    // Skip the regenerate call entirely until VData is ready - callers should not mark this
    // weapon "applied" when this returns false, so the next frame's pass retries it.
    bool regenerateSkin() const noexcept
    {
        if (!baseEntity().vData().valueOr(nullptr))
            return false;

        // forceHighRes stays FALSE. The reference implementation calls UpdateSkin(true), and this
        // was briefly changed to true to match it - but that was reverted after a death-triggered
        // SEGV (null indirect call, PC=0, crash site NOT inside any function this code calls
        // directly - the signature of a deferred async job running against an already-torn-down
        // entity, the same crash class this project hit twice before). true makes
        // RegenerateWeaponSkin queue strictly MORE async composite-material work than false does,
        // which is the wrong direction to push while an entity-teardown race is unresolved.
        //
        // false is also what the game itself uses for its own regenerate_weapon_skins ConCommand
        // (the thunk does `xor edi, edi` before jumping into the handler), and it is what this
        // project ran with stably for many sessions. Do not flip this back to true without a
        // specific reason and a death/respawn stress test.
        if (const auto regenerate = hookContext.patternSearchResults().template get<PointerToRegenerateWeaponSkin>(); regenerate && baseWeapon)
            regenerate(baseWeapon, false);

        return true;
    }

    [[nodiscard]] auto getSceneObjectUpdater() const noexcept
    {
        return reinterpret_cast<std::uint64_t(*)(cs2::C_CSWeaponBase*, void*, bool)>(sceneObjectUpdaterHandle() ? sceneObjectUpdaterHandle()->updaterFunction : nullptr);
    }

    void setSceneObjectUpdater(auto x) const noexcept
    {
        if (sceneObjectUpdaterHandle())
            sceneObjectUpdaterHandle()->updaterFunction = reinterpret_cast<std::uint64_t(*)(void*, void*, bool)>(x);
    }

    // Forces this weapon's accuracy penalty and recoil index up to the current tick. CS2 decays both
    // lazily (only when it next needs them), so reading inaccuracy()/spread()/recoilIndex() without
    // this first returns a value stale from the last game update - which throws the predicted bullet
    // cone off, badly for fast-firing spray weapons (a MAC-10 spray missed entirely until this was
    // added). Call it right before reading those for a prediction. The underlying function is
    // idempotent within a tick (see PointerToUpdateAccuracyPenaltyFunction, WeaponPatternTypes.h), so
    // calling it from the input hook does not desync from the game's own call at fire time.
    void updateAccuracyPenalty() const noexcept
    {
        if (const auto updateFn = hookContext.patternSearchResults().template get<PointerToUpdateAccuracyPenaltyFunction>(); updateFn && baseWeapon)
            updateFn(baseWeapon);
    }

    // The weapon's live per-shot inaccuracy (movement/air/jump penalty) and base spread, read through
    // the game's own GetInaccuracy/GetSpread so they match what FireBullet will use. Public because the
    // spread predictor (SpreadSolver) needs them SEPARATELY - unlike bulletInaccuracy() above, which
    // sums them for the triggerbot's simple geometric gate. Call updateAccuracyPenalty() first for a
    // current (non-stale) read.
    [[nodiscard]] Optional<float> inaccuracy() const noexcept
    {
        const auto getInaccuracyFn = hookContext.patternSearchResults().template get<PointerToGetInaccuracyFunction>();
        if (baseWeapon && getInaccuracyFn)
            return getInaccuracyFn(baseWeapon, nullptr, nullptr);
        return {};
    }

    // velocity-cs2's get_inaccuracy_at_velocity: the inaccuracy this weapon WOULD have if the given pawn
    // were moving at `velocity`, rather than at its real current velocity. Used to predict the bullet
    // cone at the velocity the shot will actually fire at - e.g. after a counter-strafe stop, or at the
    // velocity a moving target-tracking shot leaves the barrel - instead of the instantaneous one.
    //
    // Mechanism (faithful to velocity): CS2 folds movement into inaccuracy inside UpdateAccuracyPenalty,
    // which reads the pawn's m_vecAbsVelocity and the EFL_DIRTY_ABSVELOCITY flag (0x1000 in m_iEFlags -
    // set, the engine recomputes velocity from position deltas and ignores what we wrote). So: back up
    // the weapon's whole accuracy-state block, temporarily write the hypothetical velocity and clear the
    // dirty flag on the pawn, run UpdateAccuracyPenalty, read GetInaccuracy, then restore the pawn's
    // velocity + eflags AND the weapon's accuracy state byte-for-byte. Nothing observable is left changed.
    // {} if any required function/offset did not resolve (caller must treat as "cannot predict").
    [[nodiscard]] Optional<float> inaccuracyAtVelocity(cs2::C_BaseEntity* pawn, const cs2::Vector& velocity) const noexcept
    {
        const auto getInaccuracyFn = hookContext.patternSearchResults().template get<PointerToGetInaccuracyFunction>();
        const auto updateFn = hookContext.patternSearchResults().template get<PointerToUpdateAccuracyPenaltyFunction>();
        if (!baseWeapon || !pawn || !getInaccuracyFn || !updateFn)
            return {};

        auto&& schema = hookContext.schemaSystem();
        // Accuracy state runs from m_flTurningInaccuracyDelta through the end of m_flRecoilIndex - the
        // exact span velocity backs up (its hardcoded 40 bytes on the 2026-08 build). Resolve via schema
        // so it survives layout shifts across game updates rather than baking in a size.
        const auto turningDeltaOffset = schema.getFieldOffset("C_CSWeaponBase", "m_flTurningInaccuracyDelta");
        const auto recoilIndexOffset = schema.getFieldOffset("C_CSWeaponBase", "m_flRecoilIndex");
        const auto velocityOffset = schema.getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        const auto eflagsOffset = schema.getFieldOffset("C_BaseEntity", "m_iEFlags");
        if (!turningDeltaOffset.has_value() || !recoilIndexOffset.has_value() || !velocityOffset.has_value() || !eflagsOffset.has_value())
            return {};
        if (*turningDeltaOffset <= 0 || *recoilIndexOffset < *turningDeltaOffset || *velocityOffset <= 0 || *eflagsOffset <= 0)
            return {};

        const std::size_t accuracyStateSize = static_cast<std::size_t>(*recoilIndexOffset - *turningDeltaOffset) + sizeof(float);
        if (accuracyStateSize > kMaxAccuracyStateSize)
            return {};

        auto* const weaponBytes = reinterpret_cast<std::byte*>(baseWeapon);
        auto* const pawnBytes = reinterpret_cast<std::byte*>(pawn);

        std::byte backup[kMaxAccuracyStateSize];
        std::memcpy(backup, weaponBytes + *turningDeltaOffset, accuracyStateSize);

        cs2::Vector oldVelocity{};
        std::memcpy(&oldVelocity, pawnBytes + *velocityOffset, sizeof(oldVelocity));
        std::uint32_t oldEflags{};
        std::memcpy(&oldEflags, pawnBytes + *eflagsOffset, sizeof(oldEflags));

        const std::uint32_t newEflags = oldEflags & ~0x1000u;
        std::memcpy(pawnBytes + *eflagsOffset, &newEflags, sizeof(newEflags));
        std::memcpy(pawnBytes + *velocityOffset, &velocity, sizeof(velocity));

        updateFn(baseWeapon);
        const float predicted = getInaccuracyFn(baseWeapon, nullptr, nullptr);

        std::memcpy(pawnBytes + *velocityOffset, &oldVelocity, sizeof(oldVelocity));
        std::memcpy(pawnBytes + *eflagsOffset, &oldEflags, sizeof(oldEflags));
        std::memcpy(weaponBytes + *turningDeltaOffset, backup, accuracyStateSize);

        return predicted;
    }

    [[nodiscard]] Optional<float> spread() const noexcept
    {
        const auto getSpreadFn = hookContext.patternSearchResults().template get<PointerToGetSpreadFunction>();
        if (baseWeapon && getSpreadFn)
            return getSpreadFn(baseWeapon);
        return {};
    }

    // Pellets fired per shot (1 for rifles/pistols, many for shotguns), from the weapon's VData
    // (m_nNumBullets, offset 0x730 - schema-confirmed against the live cs2-dumper on the
    // 10-03 (dce58989) and 10-06 (11087116) builds; the old 0x738 predates the 09-23 layout
    // shift). The cone
    // generator advances the RNG stream once per bullet, so the predictor must pass this. {} if VData
    // is not resolved yet (freshly spawned weapon).
    [[nodiscard]] Optional<int> numBullets() const noexcept
    {
        const auto vData = baseEntity().vData().valueOr(nullptr);
        if (!vData)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(vData) + kNumBulletsOffset, sizeof(value));
        return value;
    }

    // A float field on this weapon's VData (CCSWeaponBaseVData), by schema name. {} if VData is not
    // resolved yet or the field name did not resolve. Used for the per-weapon accuracy constants
    // (m_flMaxSpeed, m_flInaccuracyJumpApex) the max-accuracy fire gate needs.
    [[nodiscard]] Optional<float> vDataFloat(const char* fieldName) const noexcept
    {
        const auto vData = baseEntity().vData().valueOr(nullptr);
        if (!vData)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSWeaponBaseVData", fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(vData) + *offset, sizeof(value));
        return value;
    }

    // Base run speed for this weapon (CCSWeaponBaseVData::m_flMaxSpeed). The ground max-accuracy gate is
    // speed <= maxSpeed * 0.34.
    [[nodiscard]] Optional<float> maxSpeed() const noexcept
    {
        return vDataFloat("m_flMaxSpeed");
    }

    // Speed multiplier while attacking with this weapon (CCSWeaponBaseVData::m_flAttackMovespeedFactor,
    // schema-confirmed at vdata +0x7E4; 1.0 for most guns, < 1 for heavies like the Negev/AUG). The
    // game's movement setup multiplies the max speed by this while IN_ATTACK is held.
    [[nodiscard]] Optional<float> attackMovespeedFactor() const noexcept
    {
        return vDataFloat("m_flAttackMovespeedFactor");
    }

    // The in-air inaccuracy floor at jump apex (CCSWeaponBaseVData::m_flInaccuracyJumpApex). The air
    // max-accuracy gate is inaccuracy <= accuracyPenalty + this.
    [[nodiscard]] Optional<float> inaccuracyJumpApex() const noexcept
    {
        return vDataFloat("m_flInaccuracyJumpApex");
    }

    // Bullet damage inputs from VData, for the min-damage estimate (velocity's penetration::prepare):
    // base damage (m_nDamage, an int), the per-500u range falloff multiplier (m_flRangeModifier), the
    // weapon's armor penetration ratio (m_flArmorRatio) and the headshot multiplier (m_flHeadshotMultiplier).
    [[nodiscard]] Optional<float> baseDamage() const noexcept
    {
        const auto vData = baseEntity().vData().valueOr(nullptr);
        if (!vData)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSWeaponBaseVData", "m_nDamage");
        if (!offset.has_value() || *offset <= 0)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(vData) + *offset, sizeof(value));
        return static_cast<float>(value);
    }

    [[nodiscard]] Optional<float> rangeModifier() const noexcept
    {
        return vDataFloat("m_flRangeModifier");
    }

    // Penetration power (CCSWeaponBaseVData::m_flPenetration) - the autowall loss formula's
    // "penetrationPower" input (higher = less damage lost per wall layer; rifles ~1-3, awp high).
    [[nodiscard]] Optional<float> penetrationPower() const noexcept
    {
        return vDataFloat("m_flPenetration");
    }

    [[nodiscard]] Optional<float> armorRatio() const noexcept
    {
        return vDataFloat("m_flArmorRatio");
    }

    [[nodiscard]] Optional<float> headshotMultiplier() const noexcept
    {
        return vDataFloat("m_flHeadshotMultiplier");
    }

    // The weapon's live accumulated accuracy penalty (C_CSWeaponBase::m_fAccuracyPenalty). Call
    // updateAccuracyPenalty() first for a current read.
    [[nodiscard]] Optional<float> accuracyPenalty() const noexcept
    {
        if (!baseWeapon)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSWeaponBase", "m_fAccuracyPenalty");
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(baseWeapon) + *offset, sizeof(value));
        return value;
    }

    // velocity-cs2's shared::is_max_accuracy: is this weapon currently at its MINIMUM possible inaccuracy,
    // i.e. would a shot taken right now be as accurate as this weapon can ever be? The pawn-state scalars
    // (onGround/isDucking/isScoped/speed2d) are read by the caller and passed in so this stays decoupled
    // from PlayerPawn; `inaccuracy` is the weapon's current GetInaccuracy (call updateAccuracyPenalty
    // first). Faithful to velocity: on ground, snipers need to be scoped and either fully still or ducked
    // (a floor-rounding test that the tiny residual inaccuracy has bottomed out), everything else needs
    // speed <= maxSpeed*0.34; in air, inaccuracy must have decayed to the jump-apex floor. Returns false
    // whenever a needed constant can't be read (cannot confirm max accuracy -> do not claim it).
    [[nodiscard]] bool isMaxAccuracy(float inaccuracy, bool onGround, bool isDucking, bool isScoped, float speed2d) const noexcept
    {
        if (onGround) {
            if (isSniperRifle()) {
                if (!isScoped)
                    return false;

                // floorf on a non-negative value is a plain truncation - no libm needed here.
                if (isDucking) {
                    const float rounded = static_cast<float>(static_cast<int>(inaccuracy * 300.0f)) / 300.0f;
                    return rounded < inaccuracy;
                }
                if (speed2d <= 0.1f) {
                    const float rounded = static_cast<float>(static_cast<int>(inaccuracy * 170.0f)) / 170.0f;
                    return rounded < inaccuracy;
                }
                return false;
            }

            const auto weaponMaxSpeed = maxSpeed();
            if (!weaponMaxSpeed.hasValue())
                return false;
            return speed2d <= weaponMaxSpeed.value() * 0.34f;
        }

        const auto jumpApex = inaccuracyJumpApex();
        const auto penalty = accuracyPenalty();
        if (!jumpApex.hasValue() || !penalty.hasValue())
            return false;
        constexpr float tolerance = 0.001f;
        return inaccuracy <= penalty.value() + jumpApex.value() + tolerance;
    }

    // The weapon's current recoil index (m_flRecoilIndex, offset 0x28B8 - schema-confirmed
    // against the live cs2-dumper on the 10-03 (dce58989) and 10-06 (11087116) builds; the
    // old 0x2690 predates the 09-23 layout shift): how many shots into the current spray this
    // is. The cone generator
    // factors it in, so the predictor must pass the live value.
    [[nodiscard]] Optional<float> recoilIndex() const noexcept
    {
        if (!baseWeapon)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(baseWeapon) + kRecoilIndexOffset, sizeof(value));
        return value;
    }

private:
    // cs2-dumper offsets (2026-08-20 build). m_nNumBullets is on CCSWeaponBaseVData; m_flRecoilIndex
    // is on C_CSWeaponBase (read straight off the weapon entity).
    static constexpr std::ptrdiff_t kNumBulletsOffset = 0x730;
    static constexpr std::ptrdiff_t kRecoilIndexOffset = 0x28B8;

    // Upper bound for the accuracy-state block inaccuracyAtVelocity backs up on the stack. velocity's
    // real span is 40 bytes (m_flTurningInaccuracyDelta..m_flRecoilIndex); 64 leaves headroom if a game
    // update grows it, and the actual size is recomputed from schema each call and bounds-checked here.
    static constexpr std::size_t kMaxAccuracyStateSize = 64;

    [[nodiscard]] auto sceneObjectUpdaterHandle() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToWeaponSceneObjectUpdaterHandle>().of(baseWeapon).valueOr(nullptr);
    }

    HookContext& hookContext;
    cs2::C_CSWeaponBase* baseWeapon;
};
