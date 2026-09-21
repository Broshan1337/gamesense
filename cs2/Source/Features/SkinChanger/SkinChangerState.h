#pragma once

#include <optional>

#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Econ/PaintKitIndex.h>

// Remembers, per weapon entity handle, which skin state was last actually written - paint kit,
// pattern seed, wear (permille) and the StatTrak counter - so SkinChanger only calls into the
// (real, composite-material-rebuilding) regenerate function once per weapon+state combination
// instead of every single frame. Persistent process-lifetime state (see
// HookContext::skinChangerState()), not per-frame local state, since SkinChanger itself is a
// cheap throwaway wrapper constructed fresh every frame like the rest of this codebase's
// feature classes.
class SkinChangerState {
public:
    static constexpr auto kMaxTracked = 32;

    struct OriginalState {
        int paintKit{};
        int seed{};
        int wearPermille{};
    };

    [[nodiscard]] bool alreadyApplied(cs2::CEntityHandle handle, int paintKit, int seed, int wearPermille, int statTrak) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return entry.paintKit == paintKit && entry.seed == seed && entry.wearPermille == wearPermille && entry.statTrak == statTrak;
        }
        return false;
    }

    // Records what a weapon had BEFORE this feature ever touched it, the first time we see that
    // entity - and does nothing on every later call, so the real original can't be overwritten
    // by one of our own writes. The full triple (kit + seed + wear) is captured because the
    // revert path ("None") must restore all three: rewriting a real inventory skin with
    // neutral seed/wear destroys its pattern and look (live-confirmed - the seed is what makes
    // e.g. Case Hardened patterns, and writing 0.010 wear onto a Factory New skin visibly
    // corrupts the composite material).
    //
    // Seeding the entry with the original also makes the no-op case fall out for free: with
    // nothing configured, desired == original == what's already recorded, so alreadyApplied()
    // returns true and the weapon is never touched at all.
    void rememberOriginalPaintKit(cs2::CEntityHandle handle, int originalPaintKit, int originalSeed, int originalWearPermille) noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return;
        }
        for (auto& entry : entries) {
            if (!entry.tracked) {
                entry = Entry{handle, originalPaintKit, originalSeed, originalWearPermille, 0, originalPaintKit, originalSeed, originalWearPermille, true};
                return;
            }
        }
    }

    [[nodiscard]] std::optional<OriginalState> originalState(cs2::CEntityHandle handle) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return OriginalState{entry.originalPaintKit, entry.originalSeed, entry.originalWearPermille};
        }
        return std::nullopt;
    }

    void markApplied(cs2::CEntityHandle handle, int paintKit, int seed, int wearPermille, int statTrak) noexcept
    {
        for (auto& entry : entries) {
            if (entry.tracked && entry.handle == handle) {
                entry.paintKit = paintKit;
                entry.seed = seed;
                entry.wearPermille = wearPermille;
                entry.statTrak = statTrak;
                return;
            }
        }
        for (auto& entry : entries) {
            if (!entry.tracked) {
                // Fallback path only: in normal operation rememberOriginalPaintKit() has already
                // created this entry with the real original. If we somehow get here first, the
                // best available guess for the original is what we're writing - which at worst
                // makes "None" a no-op for this entity rather than restoring something wrong.
                entry = Entry{handle, paintKit, seed, wearPermille, statTrak, paintKit, seed, wearPermille, true};
                return;
            }
        }
    }

    // Must be called with a real, engine-backed liveness check (e.g.
    // EntitySystem::getEntityFromHandle(handle) != nullptr) - a "no longer in this player's
    // inventory" heuristic isn't the same thing (a dropped weapon is still a live entity).
    template <typename IsEntityHandleAlive>
    void releaseDeadEntities(IsEntityHandleAlive&& isEntityHandleAlive) noexcept
    {
        for (auto& entry : entries) {
            if (entry.tracked && !isEntityHandleAlive(entry.handle))
                entry.tracked = false;
        }
    }

private:
    struct Entry {
        cs2::CEntityHandle handle{};
        // What we last WROTE to this weapon.
        int paintKit{};
        int seed{};
        int wearPermille{};
        int statTrak{};
        // What the weapon had before this feature first touched it - never overwritten after
        // the entry is created, so "None" can restore it. See rememberOriginalPaintKit().
        int originalPaintKit{};
        int originalSeed{};
        int originalWearPermille{};
        bool tracked{false};
    };

    Entry entries[kMaxTracked]{};
};
