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
    std::uintptr_t manager{};      // address *managerSlot must hold
    std::uintptr_t filterVtable{}; // vtable FireBullet gives its trace filter
    bool resolved{};               // false only while another thread is still validating
    const char* status{"CS2 penetration: unsupported client build"};
    explicit operator bool() const noexcept { return create != nullptr; }
};

// One entry per inspected client build; every address is an RVA into
// libclient.so. Read out of FireBullet: it calls toHandle/collision to fill
// the filter, then create, trace, rebuild and handle in that order, loading
// the manager from managerSlot and the filter vtable by lea.
struct Profile {
    client_build_profile::BuildId buildId;
    std::uintptr_t create, handle, rebuild, trace, toHandle, collision;
    std::uintptr_t callCreate, callHandle, stride; // FireBullet's two calls, trace record stride in handle
    const char* traceSignature;                    // its prologue carries a build-specific rip displacement
    std::uintptr_t managerSlot, manager, filterVtable;
};

inline constexpr Profile kProfiles[]{
    // ea57d8833ab297b622699925dbc83bf6aa1aa615 (2026-10-06)
    {{0xea,0x57,0xd8,0x83,0x3a,0xb2,0x97,0xb6,0x22,0x69,0x99,0x25,0xdb,0xc8,0x3b,0xf6,0xaa,0x1a,0xa6,0x15},
        0x155a700, 0x1542f80, 0x1538760, 0x16f4f00, 0x16f2340, 0x16f23a0,
        0x156e060, 0x156e205, 0x1542fe8,
        "55 48 8D 05 60 67 52 FF 48 89 E5 41 57 49 89 FF",
        0x467dc80, 0x4917fa0, 0x43e8518},
    // 866bdc006498e2bd6c29f467f991d14ec969833a (2026-10-09): same code and
    // layouts (TraceData, BulletState, Update, Element, filter), shifted.
    {{0x86,0x6b,0xdc,0x00,0x64,0x98,0xe2,0xbd,0x6c,0x29,0xf4,0x67,0xf9,0x91,0xd1,0x4e,0xc9,0x69,0x83,0x3a},
        0x1556f40, 0x153f7c0, 0x1534fa0, 0x16f1800, 0x16eec40, 0x16eeca0,
        0x156a8a0, 0x156aa45, 0x153f828,
        "55 48 8D 05 B0 7D 52 FF 48 89 E5 41 57 49 89 FF",
        0x4676280, 0x4910520, 0x43e17d8},
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
        const Profile* profile = nullptr;
        for (const auto& candidate : kProfiles) {
            if (client_build_profile::is(base, candidate.buildId)) {
                profile = &candidate;
                break;
            }
        }
        if (!profile) return out;
        const auto code = client.getCodeSection().raw();
        const auto codeBegin = reinterpret_cast<std::uintptr_t>(code.data());
        const auto matches = [&](std::uintptr_t rva, const char* signature) {
            const auto sig = tracing_sigs::detail::parse(signature);
            const auto at = base + rva;
            if (at < codeBegin || at + sig.length > codeBegin + code.size()) return false;
            return tracing_sigs::detail::scan(std::span{reinterpret_cast<const std::byte*>(at), sig.length}, sig) != nullptr;
        };
        // Independent ABI anchors, plus the game's calls and trace record stride.
        if (!matches(profile->create, "55 48 89 E5 41 57 49 89 FF 41 56 41 55 41 54 53 48 89 F3")
            || !matches(profile->handle, "55 66 0F EF D2 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4")
            || !matches(profile->rebuild, "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB")
            || !matches(profile->trace, profile->traceSignature)
            || !matches(profile->toHandle, "48 85 FF 74 3B 48 8B 57 10 B8 FF FF FF FF 48 85")
            || !matches(profile->collision, "48 85 FF 0F 84 8B 00 00 00 55 48 89 E5")
            || !matches(profile->callCreate, "E8 9B C6 FE FF")
            || !matches(profile->callHandle, "E8 76 4D FD FF")
            || !matches(profile->stride, "48 6B C0 38")) {
            out.status = "CS2 penetration: ABI validation failed";
            return out;
        }
        // The filter vtable is data, so no signature covers it: its first
        // slot must at least be a function inside the client's code.
        std::uintptr_t firstFilterSlot{};
        if (!LinuxPlatformApi::safeRead(reinterpret_cast<void*>(base + profile->filterVtable),
                &firstFilterSlot, sizeof(firstFilterSlot))
            || firstFilterSlot < codeBegin || firstFilterSlot >= codeBegin + code.size()) {
            out.status = "CS2 penetration: ABI validation failed";
            return out;
        }
        out.allocatorSlot = client.getFunctionAddress("g_pMemAlloc").as<void**>();
        if (!out.allocatorSlot || !*out.allocatorSlot) return out;
        out.base = base;
        out.managerSlot = reinterpret_cast<void**>(base + profile->managerSlot);
        out.manager = base + profile->manager;
        out.filterVtable = base + profile->filterVtable;
        out.create = reinterpret_cast<Binding::Create>(base + profile->create);
        out.trace = reinterpret_cast<Binding::Trace>(base + profile->trace);
        out.rebuild = reinterpret_cast<Binding::Rebuild>(base + profile->rebuild);
        out.handle = reinterpret_cast<Binding::Handle>(base + profile->handle);
        out.toHandle = reinterpret_cast<Binding::ToHandle>(base + profile->toHandle);
        out.collision = reinterpret_cast<Binding::Collision>(base + profile->collision);
        out.status = "CS2 penetration: engine ABI validated";
        return out;
    }();
    result.resolved = true;
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
    if (!manager || reinterpret_cast<std::uintptr_t>(manager) != native.manager
        || !read<void*>(manager, 0)) return {};
    const cs2::Vector delta{difference.x / distance * bullet.maxRange,
        difference.y / distance * bullet.maxRange, difference.z / distance * bullet.maxRange};
    alignas(16) std::array<std::byte, 0x50> filter{};
    write(filter.data(), 0, native.filterVtable);
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
