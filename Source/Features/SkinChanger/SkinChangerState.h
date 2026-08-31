#pragma once

#include <optional>

#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Econ/PaintKitIndex.h>

// Remembers, per weapon entity handle, which paint kit was last actually written - so
// SkinChanger only calls into the (real, composite-material-rebuilding) regenerate function
// once per weapon+skin combination instead of every single frame. Persistent process-lifetime
// state (see HookContext::skinChangerState()), not per-frame local state, since SkinChanger
// itself is a cheap throwaway wrapper constructed fresh every frame like the rest of this
// codebase's feature classes.
class SkinChangerState {
public:
    static constexpr auto kMaxTracked = 32;

    [[nodiscard]] bool alreadyApplied(cs2::CEntityHandle handle, cs2::PaintKitIndex paintKit) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return entry.paintKit == paintKit;
        }
        return false;
    }

    // Records the paint kit a weapon had BEFORE this feature ever touched it, the first time we
    // see that entity - and does nothing on every later call, so the real original can't be
    // overwritten by one of our own writes.
    //
    // This is what makes selecting "None" actually revert. Without a remembered original there
    // is nothing to go back TO, which is why "None" used to just stop re-applying and leave the
    // last skin on the weapon permanently. Seeding the entry with the original also makes the
    // no-op case fall out for free: with nothing configured, desired == original == what's
    // already recorded, so alreadyApplied() returns true and the weapon is never touched at all.
    void rememberOriginalPaintKit(cs2::CEntityHandle handle, cs2::PaintKitIndex originalPaintKit) noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return;
        }
        for (auto& entry : entries) {
            if (!entry.tracked) {
                entry = Entry{handle, originalPaintKit, originalPaintKit, true};
                return;
            }
        }
    }

    [[nodiscard]] std::optional<cs2::PaintKitIndex> originalPaintKit(cs2::CEntityHandle handle) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return entry.originalPaintKit;
        }
        return std::nullopt;
    }

    void markApplied(cs2::CEntityHandle handle, cs2::PaintKitIndex paintKit) noexcept
    {
        for (auto& entry : entries) {
            if (entry.tracked && entry.handle == handle) {
                entry.paintKit = paintKit;
                return;
            }
        }
        for (auto& entry : entries) {
            if (!entry.tracked) {
                // Fallback path only: in normal operation rememberOriginalPaintKit() has already
                // created this entry with the real original. If we somehow get here first, the
                // best available guess for the original is what we're writing - which at worst
                // makes "None" a no-op for this entity rather than restoring something wrong.
                entry = Entry{handle, paintKit, paintKit, true};
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
        cs2::PaintKitIndex paintKit{};
        // What the weapon had before this feature first touched it - never overwritten after
        // the entry is created, so "None" can restore it. See rememberOriginalPaintKit().
        cs2::PaintKitIndex originalPaintKit{};
        bool tracked{false};
    };

    Entry entries[kMaxTracked]{};
};
