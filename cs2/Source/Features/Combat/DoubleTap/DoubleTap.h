#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <Features/Combat/Aimbot/RageGroups.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>

// Doubletap (velocity rage.cpp process_doubletap): a charge/fire machine that
// holds every attack while the weapon is on cooldown, then fires the moment
// client_tick >= m_nNextPrimaryAttackTick with the attack stamped onto the
// weapon's next-attack tick - the server processes the shot at the retarded
// tick, so the follow-up lands nearly one tick later (2 shots, 1 window).
// Stage 0 research: stock servers are vulnerable (HvH.gg ships a server-side
// fix), HvH-configured ones mitigate.
namespace double_tap
{

inline constexpr std::uint64_t kAttackButton = 1ull << 0;
inline constexpr int kMaxEntries = 16;

inline int lastFireTick = 0;

// Weapon cooldown state for the gate: m_nNextPrimaryAttackTick, m_bInReload, clip.
struct WeaponGate {
    int nextPrimaryAttackTick{0};
    bool inReload{false};
    bool clipPositive{false};
};

template <typename HookContext, typename Weapon>
[[nodiscard]] WeaponGate weaponGate(HookContext& hookContext, Weapon&& weapon) noexcept
{
    WeaponGate gate{};
    auto&& schema = hookContext.schemaSystem();
    const auto nextOffset = schema.getFieldOffset("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick");
    const auto reloadOffset = schema.getFieldOffset("C_CSWeaponBase", "m_bInReload");
    const auto* bytes = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(weapon.baseEntity()));
    if (nextOffset.has_value() && *nextOffset > 0)
        std::memcpy(&gate.nextPrimaryAttackTick, bytes + *nextOffset, sizeof(gate.nextPrimaryAttackTick));
    if (reloadOffset.has_value() && *reloadOffset > 0)
        std::memcpy(&gate.inReload, bytes + *reloadOffset, sizeof(gate.inReload));
    if (const auto clip = weapon.clipAmmo(); clip.hasValue())
        gate.clipPositive = clip.value() > 0;
    return gate;
}

// velocity's can_attack: client tick past the next-attack tick, two ticks past
// our own last DT fire, not reloading, ammo left.
[[nodiscard]] inline bool canAttack(const WeaponGate& gate, int tickBase) noexcept
{
    return tickBase >= gate.nextPrimaryAttackTick
        && tickBase >= lastFireTick + 2
        && !gate.inReload
        && gate.clipPositive;
}

// Stamp EVERY history entry's player tick with the weapon's next-attack tick
// (velocity: entry->set_player_tick_count(next_primary)) so the fire-time
// check passes at the retarded tick. Mirrors SubtickShotWriter's field math.
inline void stampEntriesAtNextAttack(cs2::CUserCmd* cmd, int nextPrimaryAttackTick) noexcept
{
    if (!cmd || nextPrimaryAttackTick <= 0)
        return;
    auto* const cmdBytes = reinterpret_cast<std::byte*>(cmd);

    int size{};
    std::memcpy(&size, cmdBytes + cs2::CUserCmd::kInputHistorySizeOffset, sizeof(size));
    if (size <= 0 || size > cs2::CUserCmd::kMaxInputHistoryEntries)
        return;
    std::byte* rep = nullptr;
    std::memcpy(&rep, cmdBytes + 56, sizeof(rep));
    if (!rep)
        return;

    for (int i = 0; i < size && i < kMaxEntries; ++i) {
        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + 8 + static_cast<std::ptrdiff_t>(i) * static_cast<std::ptrdiff_t>(sizeof(entry)), sizeof(entry));
        if (!entry)
            continue;
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + 16, sizeof(hasBits));
        hasBits |= 0x800;  // player_tick_count has-bit (SubtickShotWriter's kPlayerTickCountHasBit)
        std::memcpy(entry + 16, &hasBits, sizeof(hasBits));
        std::memcpy(entry + 104, &nextPrimaryAttackTick, sizeof(nextPrimaryAttackTick));
    }
}

// Charging: strip the attack from every bank so the held fire does not waste
// the charge (velocity's not-should_attack path).
inline void suppressAttack(cs2::CUserCmd* cmd) noexcept
{
    if (!cmd)
        return;
    UserCmd{cmd}.replaceButtons(kAttackButton, 0);
}

inline void reset() noexcept
{
    lastFireTick = 0;
}

}
// The per-tick gate. Runs at WriteMoveCrc AFTER the aimbot/triggerbot writers
// (it must see every fire intent, including the aimbot's force-shot press)
// and BEFORE the anti-aim apply (hide-shots reads the final fire state).
// Charging strips the attack from the command - the tick the weapon is ready,
// the attack flows through as a fresh server-side edge with the entries
// stamped at the retarded tick.
template <typename HookContext>
class DoubleTapGate {
public:
    explicit DoubleTapGate(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(aimbot_vars::Enabled) || !cmd)
            return;

        auto&& pawn = hookContext.activeLocalPlayerPawn();
        if (!pawn || pawn.isAlive() != true)
            return;
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        const auto group = rage_groups::active(hookContext, pawn.getActiveWeapon());
        if (!group.valid || !group.doubletap)
            return;

        const auto tick = hookContext.localPlayerController().tickBase().valueOr(0);
        const auto gate = double_tap::weaponGate(hookContext, pawn.getActiveWeapon());
        const bool attacking = UserCmd{cmd}.isButtonDown(double_tap::kAttackButton);
        if (attacking && double_tap::canAttack(gate, tick)) {
            double_tap::stampEntriesAtNextAttack(cmd, gate.nextPrimaryAttackTick);
            double_tap::lastFireTick = tick;
        } else {
            double_tap::suppressAttack(cmd);
        }
    }

    void onUnload() const noexcept
    {
        double_tap::reset();
    }

private:
    HookContext& hookContext;
};
