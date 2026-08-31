# Vtable reference (libclient.so, CS2, Linux)

Consolidated list of every vtable slot this project's RE has actually pinned down, plus how to
get a live instance of the object each vtable belongs to. Kept separate from the byte-pattern
signatures (`Source/MemoryPatterns/Linux/*.h`) because the two survive game updates very
differently:

- **Byte-pattern signatures** anchor on a specific function's *compiled bytes*. A recompile
  (new CS2 build) can shift/reshuffle these completely even when nothing about the underlying
  mechanism changed - every signature in this codebase has needed re-deriving at least once
  already.
- **Vtable slot indices** anchor on a class's *virtual function table layout* - the order
  virtual methods were declared in the C++ source. This is far more stable across incremental
  updates: it only breaks if a method is added/removed/reordered in the class declaration
  itself, which is rare compared to routine recompilation shifting every function's address.

So if a future update breaks everything: **the slot numbers below are still the right slots to
look at first**. The actual bytes at those slots (i.e. which function address occupies them)
will have changed and need re-resolving, but you don't need to re-derive *which offset into
which vtable* matters - that's already known and documented here.

## How to re-derive a broken pattern quickly (using IDA Pro MCP)

1. If the object's class has an RTTI name (search `entity_query` with `kind: "names"` and a
   regex on the class name, e.g. `CEntitySubclassVDataBase`), its `_ZTV<mangled>` symbol *is*
   the vtable directly - no byte pattern needed at all, ever. RTTI symbol names are essentially
   immune to recompiles (they're derived from the C++ source's class name, not code layout).
   Read the vtable bytes directly (`get_bytes`) starting 16 bytes past the `_ZTV` symbol (the
   first 16 bytes are the Itanium ABI's offset-to-top + typeinfo pointer prefix), then index by
   `slot * 8`.
2. If there's no RTTI symbol (most of this project's classes - `C_CSWeaponBase`,
   `CEconItemView`, `CBodyComponent`/`CModelState` don't have one, or it wasn't looked for),
   you need a live instance to read `*(void**)instancePtr` at runtime, or a static anchor
   function that's known to construct/return one - then `make_signature_for_function` on that
   anchor to get a fresh, verified-unique byte pattern in one call (this is what closed out the
   whole legacy-paint-kit chain in a handful of tool calls, see `project_cs2_skinchanger.md`
   memory for the full trail).
3. Re-verify the slot's *purpose* hasn't shifted by decompiling whatever function now occupies
   it and checking it still does what's documented below (arguments, return shape, what it
   touches) - a stable slot number doesn't guarantee a stable meaning if the class itself grew
   a new virtual method inserted earlier in the list.

## `C_CSWeaponBase`'s own vtable (the weapon entity itself, `*(void**)weapon`)

No RTTI symbol found for this one - resolved via live decompiles of functions that already
dereference `*a1` on a weapon-entity-typed `a1`.

| Byte offset | Slot | Purpose | Used by our code? |
|---|---|---|---|
| `+3112` | 389 | Override-check inside `RegenerateWeaponSkin` (`sub_1466900`) - if this equals the module's own default implementation, falls back to reading a fixed count at `a1[159]+1312`; otherwise calls through it. Never independently named. | No - only ever observed inline inside `RegenerateWeaponSkin`, which we call as a whole via `PointerToRegenerateWeaponSkin`. |
| `+2400` | 300 | Same override-check pattern - if unoverridden, resolves to `a1+550` (byte offset 4400, i.e. the embedded `m_AttributeManager`). This is effectively "GetAttributeManager()" with a data-member fast path when not overridden. | No directly - but this is *why* `m_AttributeManager` sits at a fixed, reliable offset (4400) rather than needing its own virtual call; we just read the fixed offset. |
| `+1992` | 249 | Called twice inside `RegenerateWeaponSkin` with `(weapon, someBuffer)` - some kind of "attribute list changed, apply" notification. Not named, not traced further. | No. |

## `CEconItemView`'s own vtable (the embedded item-view object, `*(void**)item`)

Also no RTTI symbol. `item` here = `weapon + m_AttributeManager(4400) + m_Item(80)`, i.e. exactly
what `EconItemAttributeOffsets`/`offsets.item.of(attributeContainer).get()` already computes.

| Byte offset | Slot | Purpose | Used by our code? |
|---|---|---|---|
| `+32` | 4 | Returns the item's currently-equipped paint-kit index (`unsigned int`). Called inside `sub_1FD4AE0` (`PointerToGetPaintKitDefinition`) before the tree lookup. Functionally `GetSkinIndex()`/`GetPaintIndex()`. | Indirectly - we call `sub_1FD4AE0` as a whole, which calls this internally. |
| `+40` | 5 | Returns the item's random seed (`unsigned int`), used to build the `"g_nRandomSeed"` shader param in `RegenerateWeaponSkin`. Functionally `GetSeed()`. | No - we set seed via `SetAttributeValueByName("set item texture seed", ...)` instead of reading it back through this. |
| `+48` | 6 | Returns the item's wear value (`float`), used to build `"g_flWearAmount"`. Functionally `GetWear()`. | No - same as above, we write via `SetAttributeValueByName`. |

## `CBodyComponent`'s vtable (`*(void**)m_CBodyComponent`)

No RTTI symbol. Confirmed empirically by two independent decompiled callers both dispatching
through this exact slot before touching known `CModelState` fields.

| Byte offset | Slot | Purpose | Used by our code? |
|---|---|---|---|
| `+112` | 14 | Returns the owning entity's `CModelState*`. Functionally `GetSkeletonInstance()`/`GetModelState()`. | **Yes** - `BaseWeapon::setMeshGroupMask()`, the only raw vtable call in production (non-experimental) code in this project. |

## `CEntitySubclassVDataBase`'s vtable

**Has** an RTTI symbol: `_ZTV24CEntitySubclassVDataBase` at `0x42e1d10` (typeinfo
`_ZTI24CEntitySubclassVDataBase` at `0x42e1990`). No derived-class RTTI symbols were found for
any specific weapon type (searched for `knife`/`vdata` name patterns) - the working theory is
that subclassing here is entirely data-driven (KV3-parsed fields keyed by a `subclass_name`
hash, see `CEntitySubclassGameSystem` in project memory) rather than C++ inheritance, meaning
**this one vtable is shared by every weapon's resolved `m_pSubclassVData`**, not just a base
class default that real weapon types override.

| Byte offset | Slot | Purpose | Status |
|---|---|---|---|
| `+0x30` (48) | 6 | Dispatched at the tail of `UpdateSubclass()` (`sub_DDE400`), passing through the KV3 spawn-data argument. **Confirmed via direct decompile: `nullsub_424`, an empty function body - genuinely does nothing.** | **Dead end, ruled out** without needing a live test. `UpdateSubclass()`'s only real effect is refreshing the cached `m_pSubclassVData` pointer (via `sub_D9A070`) - this final dispatch is a no-op at the C++ level for every weapon type sharing this one vtable. |

Other slots on this vtable were not inspected - if the knife holding-animation investigation
resumes, dumping the remaining ~10-15 slots (`get_bytes` at `0x42e1d20 + 8*slot`, then
`decompile` each target) and reading their bodies is a pure-static, zero-risk next step, cheaper
than more live experiments now that this table is known to be a single shared vtable rather than
something requiring a live pointer per weapon type.

## Where the knife holding-animation investigation stands (as of this doc)

Still unresolved. What's been ruled out so far, all without a crash:
- Raw `m_nSubclassID` write + `UpdateSubclass()` alone: mesh/skin correct, animation still wrong.
- `notifySubclassVDataChanged()` (`sub_1494B00`, changeType=1): confirmed relevant only to the
  small HUD weapon-select icon, not the held viewmodel.
- Mirroring `setModel()`/mesh-mask onto the separate `m_hHudModelArms` first-person clone
  entity: inconclusive test, HUD label regressed, animation not confirmed fixed.
- `UpdateSubclass()`'s own vtable+0x30 tail dispatch: confirmed no-op (this document).

Not yet tried: the "force re-equip" empirical test (deselect/reselect the weapon slot after the
model/subclass swap, on the theory that animation/pose set is cached once at equip-time and
never re-evaluated by any of the writes above) - flagged as the leading hypothesis two sessions
ago, never actually executed. Also not yet tried: pure static search for wherever the *player
pawn's* animation-state system (not the weapon entity) picks a holding pose per weapon type -
everything traced so far has stayed on the weapon/item side, but grip/hold animation selection
plausibly lives on the pawn's animation graph/state machine instead, keyed by some simpler
per-weapon-type enum rather than the full subclass VData mechanism.
