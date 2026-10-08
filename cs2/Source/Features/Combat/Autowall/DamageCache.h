#pragma once

#include <array>
#include <cstddef>

#include <CS2/Classes/Vector.h>
#include <Utils/Optional.h>

namespace penetration {
// Scoped to a single CreateMove: multipoint selection asks about the same point
// repeatedly. Cache failures as well, but never reuse visibility across commands.
class DamageCache {
public:
    void reset() noexcept { size = 0; next = 0; }
    Optional<float> find(const cs2::Vector& start, const cs2::Vector& end, const void* target, int hitgroup) const noexcept
    {
        for (std::size_t i = 0; i < size; ++i) {
            const auto& entry = entries[i];
            if (entry.target == target && entry.hitgroup == hitgroup
                && same(entry.start, start) && same(entry.end, end))
                return entry.damage;
        }
        return {};
    }
    void store(const cs2::Vector& start, const cs2::Vector& end, const void* target, int hitgroup, float damage) noexcept
    {
        entries[next] = {start, end, target, hitgroup, damage};
        next = (next + 1) % entries.size();
        if (size < entries.size()) ++size;
    }
private:
    static bool same(const cs2::Vector& a, const cs2::Vector& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    struct Entry {
        cs2::Vector start, end;
        const void* target;
        int hitgroup;
        float damage;
    };
    std::array<Entry, 64> entries{};
    std::size_t size{}, next{};
};
}
