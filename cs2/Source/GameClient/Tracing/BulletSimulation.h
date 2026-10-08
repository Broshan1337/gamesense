#pragma once

#include <Features/Combat/Autowall/EnginePenetration.h>
#include <GameClient/ClientBuildProfile.h>
#include <GameClient/Tracing/TracingSigs.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/StatusReport.h>

namespace bullet_simulation {

using penetration::engine::TraceData;
using penetration::engine::BulletState;
using penetration::engine::Update;
using penetration::engine::read;

// These are SysV Linux calls, not Windows __fastcall signatures. CreateTrace
// returns a Vector in xmm0/xmm1 and receives start/delta/filter by pointer.
struct Binding {
    using Create = cs2::Vector (*)(TraceData*, const cs2::Vector*, const cs2::Vector*, void*, int);
    using Trace = void (*)(void*, TraceData*, const cs2::Vector*, const cs2::Vector*, void*, float);
    using Rebuild = void (*)(TraceData*, const cs2::Vector*);
    using Handle = bool (*)(TraceData*, BulletState*, Update*, int, void*);
    using ToHandle = std::uint32_t (*)(void*);
    using Collision = std::uint16_t (*)(void*);
    Create create{};
    Trace trace{};
    Rebuild rebuild{};
    Handle handle{};
    ToHandle toHandle{};
    Collision collision{};
    std::uintptr_t base{};
    void** managerSlot{};
    void** allocatorSlot{};
    const char* status{"CS2 penetration: unsupported client build"};
    explicit operator bool() const noexcept { return create != nullptr; }
};

inline const Binding& binding() noexcept
{
    // This module deliberately links without libstdc++; avoid C++ local-static
    // guard calls and publish a completed binding with acquire/release ordering.
    static Binding result;
    static std::atomic<int> state{0};
    static constexpr Binding pending{};
    if (state.load(std::memory_order_acquire) == 2) return result;
    int expected = 0;
    if (!state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return pending;
    result = [] {
        Binding out;
        const LinuxDynamicLibrary client{cs2::CLIENT_DLL};
        const auto* map = client.getLinkMap();
        if (!map || !map->l_addr) return out;
        const auto base = static_cast<std::uintptr_t>(map->l_addr);
        if (!client_build_profile::supported(base)) return out;
        const auto code = client.getCodeSection().raw();
        const auto matches = [&](std::uintptr_t rva, const char* signature) {
            const auto sig = tracing_sigs::detail::parse(signature);
            const auto at = base + rva;
            const auto begin = reinterpret_cast<std::uintptr_t>(code.data());
            if (at < begin || at + sig.length > begin + code.size()) return false;
            return tracing_sigs::detail::scan(std::span{reinterpret_cast<const std::byte*>(at), sig.length}, sig) != nullptr;
        };
        // Independent ABI anchors, plus the game's calls and trace record stride.
        if (!matches(0x155a700, "55 48 89 E5 41 57 49 89 FF 41 56 41 55 41 54 53 48 89 F3")
            || !matches(0x1542f80, "55 66 0F EF D2 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4")
            || !matches(0x1538760, "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB")
            || !matches(0x16f4f00, "55 48 8D 05 60 67 52 FF 48 89 E5 41 57 49 89 FF")
            || !matches(0x16f2340, "48 85 FF 74 3B 48 8B 57 10 B8 FF FF FF FF 48 85")
            || !matches(0x16f23a0, "48 85 FF 0F 84 8B 00 00 00 55 48 89 E5")
            || !matches(0x156e060, "E8 9B C6 FE FF")
            || !matches(0x156e205, "E8 76 4D FD FF")
            || !matches(0x1542fe8, "48 6B C0 38")) {
            out.status = "CS2 penetration: ABI validation failed";
            return out;
        }
        out.allocatorSlot = client.getFunctionAddress("g_pMemAlloc").as<void**>();
        if (!out.allocatorSlot || !*out.allocatorSlot) return out;
        out.base = base;
        out.managerSlot = reinterpret_cast<void**>(base + 0x467dc80);
        out.create = reinterpret_cast<Binding::Create>(base + 0x155a700);
        out.trace = reinterpret_cast<Binding::Trace>(base + 0x16f4f00);
        out.rebuild = reinterpret_cast<Binding::Rebuild>(base + 0x1538760);
        out.handle = reinterpret_cast<Binding::Handle>(base + 0x1542f80);
        out.toHandle = reinterpret_cast<Binding::ToHandle>(base + 0x16f2340);
        out.collision = reinterpret_cast<Binding::Collision>(base + 0x16f23a0);
        out.status = "CS2 penetration: engine ABI validated";
        return out;
    }();
    StatusReport::record(result.status, bool(result));
    state.store(2, std::memory_order_release);
    return result;
}

template <typename T>
void write(void* destination, std::size_t offset, const T& value) noexcept
{
    std::memcpy(static_cast<std::byte*>(destination) + offset, &value, sizeof(T));
}

class TraceOwner {
public:
    explicit TraceOwner(const Binding& binding) noexcept : native{binding} {}
    TraceData data;
    ~TraceOwner()
    {
        release(data.elements, data.storage.data(), data.growth);
        release(data.updates, data.updateStorage.data(), data.updateGrowth);
    }
private:
    void release(void* pointer, void* embedded, int growth) noexcept
    {
        // CUtlMemory may migrate from embedded to engine-allocated storage.
        // Free through the same IMemAlloc vtable as FireBullet's cleanup.
        if (!pointer || pointer == embedded || static_cast<std::uint32_t>(growth) > 0x3fffffffu) return;
        void* allocator = *native.allocatorSlot;
        auto free = reinterpret_cast<void (*)(void*, void*)>(read<void*>(read<void*>(allocator, 0), 0x20));
        free(allocator, pointer);
    }
    const Binding& native;
};

template <typename Entity, typename CanPenetrate>
Optional<penetration::Impact> simulate(const cs2::Vector& start, const cs2::Vector& point,
    void* skip, void* target, penetration::Bullet bullet, penetration::Limits limits,
    penetration::TraceBudget& budget, int shooterTeam, Entity&& entity, CanPenetrate&& canPenetrate) noexcept
{
    if (!target || !skip || !bullet.valid() || bullet.maxRange > 65536 || !limits.valid()
        || !hitbox_geometry::finite(start) || !hitbox_geometry::finite(point)) return {};
    const auto difference = hitbox_geometry::subtract(point, start);
    const float distance = std::hypot(difference.x, difference.y, difference.z);
    if (!std::isfinite(distance) || distance <= 0 || distance > bullet.maxRange + 0.1f) return {};
    const auto& native = binding();
    // CreateTrace can perform five world queries and four exit queries.
    if (!native || budget.remaining < 9) return {};
    budget.remaining -= 9;
    void* const manager = *native.managerSlot;
    if (!manager || reinterpret_cast<std::uintptr_t>(manager) != native.base + 0x4917fa0
        || !read<void*>(manager, 0)) return {};
    const cs2::Vector delta{difference.x / distance * bullet.maxRange,
        difference.y / distance * bullet.maxRange, difference.z / distance * bullet.maxRange};
    alignas(16) std::array<std::byte, 0x50> filter{};
    write(filter.data(), 0, native.base + 0x43e8518);
    write<std::uint64_t>(filter.data(), 8, 0x1c3009);
    write<std::uint64_t>(filter.data(), 0x10, 1ull << 38);
    std::memset(filter.data() + 0x20, 0xff, 0x10);
    write(filter.data(), 0x20, native.toHandle(skip));
    // FireBullet also excludes the shooter's owner entity from this query.
    std::uint32_t ownerHandle = 0xffffffff;
    if (LinuxPlatformApi::safeRead(static_cast<const std::byte*>(skip) + 0x698, &ownerHandle, sizeof(ownerHandle))) {
        if (void* owner = entity(ownerHandle)) write(filter.data(), 0x28, native.toHandle(owner));
    }
    write<std::uint64_t>(filter.data(), 0x30, 0x0000ffff00000000ull);
    write(filter.data(), 0x30, native.collision(skip));
    write<std::uint16_t>(filter.data(), 0x37, 0x0301);
    write<std::uint8_t>(filter.data(), 0x39, 0x4b);
    TraceOwner owner{native};
    owner.data.start = start;
    const auto tracedDelta = native.create(&owner.data, &start, &delta, filter.data(), limits.maxPenetrations);
    if (!hitbox_geometry::finite(tracedDelta)) return {};
    // Match FireBullet's two-pass flow: world/solid trace followed by entities.
    // Without this pass an otherwise plausible wall sim can miss player hits.
    if (!budget.take()) return {};
    write<std::uint64_t>(filter.data(), 8, 0x1c300b);
    write<std::uint8_t>(filter.data(), 0x37, 0x0e);
    const int oldCount = owner.data.count;
    native.trace(manager, &owner.data, &start, &tracedDelta, filter.data() + 8, -1.0f);
    if (owner.data.count != oldCount) native.rebuild(&owner.data, &tracedDelta);
    return penetration::engine::walk(owner.data, bullet, limits, target, budget,
        [&](auto& data, auto& state, auto& update) { return native.handle(&data, &state, &update, shooterTeam, nullptr); },
        entity, [](const auto& element) {
            if (element.kind != 1) return -1;
            const auto* hitbox = read<void*>(element.opaque.data(), 0x10);
            int group = -1;
            if (hitbox) LinuxPlatformApi::safeRead(static_cast<const std::byte*>(hitbox) + 0x38, &group, sizeof(group));
            return group;
        }, canPenetrate);
}
}
