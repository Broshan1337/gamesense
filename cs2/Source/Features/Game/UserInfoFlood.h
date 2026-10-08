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































namespace userinfo_flood
{

struct FieldDef {
    const char* cvar;
    const char* label;
    int kind; 
};








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



inline constexpr std::size_t kHeavyNameBytes = 155;




inline constexpr const char* kZeroWidth[]{"\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D"};


inline std::atomic<int> pendingRestore{0}; 
inline bool engaged[kFieldCount]{};
inline bool engagedOnce = false;
inline int selectionFingerprint = -1; 
inline int tickCounter = 0;
inline int sendCounter = 0; 
inline int fieldCursor = 0;
inline char baseName[chat_tools::kBufferTextSize] = ""; 


inline bool hasSnapshot[kFieldCount]{};
inline int snapInt[kFieldCount]{};
inline float snapFloat[kFieldCount]{};
inline bool snapBool[kFieldCount]{};





inline bool directFailed = false;
inline int directCooldown = 0;
inline constexpr int kDirectCooldownTicks = 64;

inline net_messages::SetConVarEntry directEntries[kFieldCount]{};
inline char directValueBuffers[kFieldCount][64]{};
inline char directNameValue[chat_tools::kBufferTextSize]{};



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
            restoreAll(false); 

        
        
        
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        
        
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
            break; 
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
        
        return baseName[0] != '\0' && !GET_CONFIG_VAR(name_animator_vars::Enabled);
    }

    
    
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

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    void directSendTick() noexcept
    {
        if (directFailed)
            return;
        if (directCooldown > 0) {
            --directCooldown;
            return;
        }

        
        
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
                break; 
            ++messagesSent;
        }

        
        
        net_messages::commitChannel(channel);
        if (messagesSent > 0) {
            if (!net_messages::transmitChannel(channel, "Userinfo Flood"))
                directCooldown = kDirectCooldownTicks;
            VerifyConsole::write(2.0f, "uinfo", "direct: %d SetConVar message(s) sent", messagesSent);
        }
    }

    
    
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

    
    
    void buildChurnValue(int index, char (&value)[64]) const noexcept
    {
        const int i = sendCounter;
        switch (index) {
        case 1: std::snprintf(value, 64, "%d", i % 3); break;                            
        case 2: std::snprintf(value, 64, "%d", i % 6); break;                            
        case 3: std::snprintf(value, 64, "%d", i % 6); break;                            
        case 4: std::snprintf(value, 64, "%d", i % 6); break;                            
        case 5: std::snprintf(value, 64, "%.3f", 1.0f + 0.1f * (i % 120)); break;        
        case 6: std::snprintf(value, 64, "%.3f", -3.0f + 0.05f * (i % 200)); break;      
        case 7: std::snprintf(value, 64, "%.3f", 0.5f + 0.1f * (i % 50)); break;         
        case 8: std::snprintf(value, 64, "%d", i % 2); break;                            
        case 9: std::snprintf(value, 64, "%d", (i / 2) % 2); break;                      
        case 10: std::snprintf(value, 64, "%d", 100 + (i % 156)); break;                     
        case 11: std::snprintf(value, 64, "%d", 1 + (i % 3)); break;                         
        case 12: std::snprintf(value, 64, "%d", (i / 3) % 2); break;                         
        case 13: std::snprintf(value, 64, "%d", (i / 4) % 2); break;                         
        case 14: std::snprintf(value, 64, "%.3f", 0.5f + 0.1f * (i % 40)); break;            
        case 15: std::snprintf(value, 64, "%d", i % 256); break;                             
        case 16: std::snprintf(value, 64, "%d", (i * 7) % 256); break;                       
        default: std::snprintf(value, 64, "%d", (i * 13) % 256); break;                      
        }
    }

    
    
    
    
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

} 