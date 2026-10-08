#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <Features/Combat/Autowall/Penetration.h>

// Linux layout recovered from libclient ea57d8833ab297b622699925dbc83bf6aa1aa615.
// This differs from the older Windows UC layout (0x30-byte trace elements).
namespace penetration::engine {

struct Element {
    std::array<std::byte, 0x2c> opaque;
    std::uint32_t entityHandle;
    std::int16_t surface;
    std::uint8_t boundary;
    std::uint8_t kind;
    std::array<std::byte, 4> padding;
};
struct Update {
    float enter, exit, damage;
    int penetrations;
    std::uint16_t entryIndex, exitIndex;
    std::uint8_t flags;
    std::array<std::byte, 3> padding;
};
struct BulletState {
    float damage, power, rangeModifier, traceLength;
    int remaining;
    bool failed;
    std::array<std::byte, 3> padding;
};
struct alignas(16) TraceData {
    int count{};
    int reserved{};
    Element* elements{storage.data()};
    int capacity{128};
    int growth{static_cast<int>(0x80000000u)};
    std::array<Element, 128> storage{};
    std::array<std::byte, 8> padding{};
    int updateCount{};
    int updateReserved{};
    Update* updates{updateStorage.data()};
    int updateCapacity{8};
    int updateGrowth{static_cast<int>(0x80000000u)};
    std::array<Update, 8> updateStorage{};
    cs2::Vector start{};
    cs2::Vector delta{};
    float maxFraction{1.0f};
    TraceData() = default;
    TraceData(const TraceData&) = delete;
    TraceData& operator=(const TraceData&) = delete;
};
static_assert(sizeof(Element) == 0x38);
static_assert(sizeof(Update) == 0x18);
static_assert(sizeof(BulletState) == 0x18);
static_assert(offsetof(TraceData, storage) == 0x18);
static_assert(offsetof(TraceData, updateCount) == 0x1c20);
static_assert(offsetof(TraceData, updates) == 0x1c28);
static_assert(offsetof(TraceData, updateStorage) == 0x1c38);
static_assert(offsetof(TraceData, start) == 0x1cf8);
static_assert(offsetof(TraceData, delta) == 0x1d04);
static_assert(offsetof(TraceData, maxFraction) == 0x1d10);

template <typename T>
T read(const void* source, std::size_t offset) noexcept
{
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(source) + offset, sizeof(T));
    return value;
}

// Validate the complete list before handing indices back to engine code.
inline bool valid(const TraceData& data) noexcept
{
    constexpr int maxRecords = 4096;
    if (data.count < 0 || data.count > maxRecords || data.capacity < data.count
        || data.updateCount < 0 || data.updateCount > maxRecords || data.updateCapacity < data.updateCount
        || (data.count && !data.elements) || (data.updateCount && !data.updates)
        || !hitbox_geometry::finite(data.delta) || !std::isfinite(data.maxFraction)
        || data.maxFraction < 0 || data.maxFraction > 2)
        return false;
    float previous = 0.0f;
    for (int i = 0; i < data.updateCount; ++i) {
        const auto& update = data.updates[i];
        if (!std::isfinite(update.enter) || !std::isfinite(update.exit)
            || update.enter < 0 || update.exit < update.enter || update.exit > 1
            || update.enter + 0.00001f < previous
            || (update.entryIndex & 0x7fff) >= data.count || update.exitIndex >= data.count)
            return false;
        previous = update.exit;
    }
    return true;
}

// The engine applies range decay on air updates and material loss on solid
// updates. Never pre-decay damage or substitute a requested bone's hitgroup.
template <typename Handle, typename Entity, typename Hitgroup, typename CanPenetrate>
Optional<Impact> walk(TraceData& data, Bullet bullet, Limits limits, void* target,
    TraceBudget& budget, Handle&& handle, Entity&& entity, Hitgroup&& hitgroup,
    CanPenetrate&& canPenetrate) noexcept
{
    if (!target || !bullet.valid() || !limits.valid() || !valid(data)) return {};
    const float length = std::hypot(data.delta.x, data.delta.y, data.delta.z);
    if (!std::isfinite(length) || length <= 0 || length > 65536.0f) return {};
    BulletState state{bullet.damage, bullet.power, bullet.rangeModifier, length, limits.maxPenetrations, false, {}};
    float thickness = 0;
    int penetrations = 0;
    for (int i = 0; i < data.updateCount; ++i) {
        auto& update = data.updates[i];
        const bool solid = (update.flags & 1) != 0;
        auto& entry = data.elements[update.entryIndex & 0x7fff];
        auto& exit = data.elements[update.exitIndex];
        if (solid) {
            // Other players obstruct the shot. Do not spend power tunneling
            // through a teammate or a different target.
            if (void* blocker = entity(entry.entityHandle); blocker && !canPenetrate(blocker)) return {};
            const float wall = (update.exit - update.enter) * length;
            thickness += wall;
            if (bullet.power <= 0 || state.remaining <= 0 || wall > limits.maxWallThickness
                || thickness > limits.maxTotalThickness || update.exit > data.maxFraction) return {};
        }
        if (!budget.take() || handle(data, state, update) || state.failed
            || !std::isfinite(state.damage) || state.damage < 1 || state.damage > bullet.damage
            || state.remaining < 0 || state.remaining > limits.maxPenetrations) return {};
        if (solid) {
            ++penetrations;
            continue;
        }
        if (update.exit > data.maxFraction) return {};
        void* const struck = entity(exit.entityHandle);
        if (struck == target) {
            if (update.exit * length > bullet.maxRange) return {};
            const int group = hitgroup(exit);
            if (group < 1 || group > 8) return {};
            return Impact{state.damage, update.exit * length, thickness, penetrations, group};
        }
        if (struck && !canPenetrate(struck)) return {};
    }
    return {}; // An unobstructed endpoint is not proof of hitting a player.
}
}
