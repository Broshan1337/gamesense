#pragma once

#include <atomic>
#include <cctype>
#include <optional>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Econ/ItemDefDatabase.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <CS2/Constants/DllNames.h>
#include <Features/Game/ChatToolsConfigVariables.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <Features/Hud/SpectatorList/SpectatorSnapshot.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <GameClient/Bind.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Platform/DynamicLibrary.h>
#include <HookContext/HookContextMacros.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/CrashLogger.h>
#include <Utils/Random.h>
#include <Utils/VerifyConsole.h>




























namespace chat_tools
{






inline std::size_t sanitizeInto(const char* in, char* out, std::size_t outCapacity) noexcept
{
    constexpr char kLineSeparator[] = "\xE2\x80\xA8"; 
    std::size_t writeIndex = 0;
    for (std::size_t i = 0; in[i] != '\0' && writeIndex + 4 < outCapacity; ++i) {
        const auto c = in[i];
        if (c == '{' && std::strncmp(in + i, "{nl}", 4) == 0) {
            std::memcpy(out + writeIndex, kLineSeparator, 3);
            writeIndex += 3;
            i += 3;
            continue;
        }
        if (c == '\\' && in[i + 1] == 'n') {
            std::memcpy(out + writeIndex, kLineSeparator, 3);
            writeIndex += 3;
            ++i;
            continue;
        }
        if (c == '"' || c == ';' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
            continue;
        out[writeIndex++] = c;
    }
    out[writeIndex] = '\0';
    return writeIndex;
}


[[nodiscard]] inline bool equalsIgnoreCase(const char* a, const char* b) noexcept
{
    for (int i = 0; a[i] != '\0' || b[i] != '\0'; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return false;
        if (ca == '\0')
            return true;
    }
    return true;
}


inline std::atomic<int> pendingNameApply{0};



inline std::atomic<int> callerParity{0};




inline int spamTicksLeft = 0;
inline int spamLinesLeft = 0;
inline int spamCooldownLeft = 0;
inline int wheelTicksLeft = 0;
inline int pingTicksLeft = 0;
inline int colorTicksLeft = 0;


inline const char* const kNamesListFile = "/chat_names.txt";
inline int cycleIndex = 0;
inline int cycleCount = 0;
inline int cycleTicksLeft = 0;


inline bool kickWasDown = false;
inline int kickSendCooldown = 0;
inline int kickAttemptsRemaining = 0;


inline bool intelWasEnabled = false;
inline bool intelWasDown = false;
inline int intelSendCooldown = 0;


inline int streak = 0;


inline int lastSpectatorCount = 0;
inline int liveSayCooldown = 0;


inline constexpr const char* kServerPersona = "Server \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]";







struct RadioPhraseEntry {
    std::uint8_t index;
    const char* cwName;
    const char* display;
};
inline constexpr RadioPhraseEntry kRadioPhrases[] = {
    {0, "CW.EcoRound", "Eco round"},
    {1, "CW.SpendRound", "Spend round"},
    {2, "CW.NeedDrop", "Need a drop"},
    {3, "CW.NeedPlan", "Need a plan"},
    {4, "CW.NeedLeader", "Need a leader"},
    {5, "CW.GoGoGo", "Go go go!"},
    {6, "CW.OMW", "On my way"},
    {7, "CW.FollowMe", "Follow me"},
    {8, "CW.FollowingYou", "Following you"},
    {9, "CW.GoA", "Go to A"},
    {10, "CW.GoB", "Go to B"},
    {11, "CW.GoToLocMid", "Go to mid"},
    {12, "CW.Regroup", "Regroup"},
    {13, "CW.StickTogether", "Stick together"},
    {14, "CW.SpreadOut", "Spread out"},
    {15, "CW.TeamFallBack", "Team fall back"},
    {16, "CW.HoldPosition", "Hold this position"},
    {17, "CW.NeedQuiet", "Need quiet"},
    {18, "CW.ImAttacking", "I'm attacking"},
    {19, "CW.HeardNoise", "Heard noise"},
    {20, "CW.SeesEnemy", "Enemy spotted"},
};
inline constexpr int kRadioPhraseCount = static_cast<int>(sizeof(kRadioPhrases) / sizeof(kRadioPhrases[0]));



inline constexpr cs2::ItemDefEntry kWheelRadioEntries[kRadioPhraseCount] = {
    {1, "Eco round", ""}, {2, "Spend round", ""}, {3, "Need a drop", ""},
    {4, "Need a plan", ""}, {5, "Need a leader", ""}, {6, "Go go go!", ""},
    {7, "On my way", ""}, {8, "Follow me", ""}, {9, "Following you", ""},
    {10, "Go to A", ""}, {11, "Go to B", ""}, {12, "Go to mid", ""},
    {13, "Regroup", ""}, {14, "Stick together", ""}, {15, "Spread out", ""},
    {16, "Team fall back", ""}, {17, "Hold this position", ""}, {18, "Need quiet", ""},
    {19, "I'm attacking", ""}, {20, "Heard noise", ""}, {21, "Enemy spotted", ""},
};



















inline void* engineInterface = nullptr;
inline bool kickSymbolsResolved = false;

using KVNewFn = void* (*)(std::size_t);
using KVCtorFn = void* (*)(void*, const char*, void*, bool);
using KVSetIntFn = void (*)(void*, const char*, int);
using CreateInterfaceFn = void* (*)(const char*, int*);
using DispatchFn = void (*)(void*, void*);

inline KVNewFn kvNew = nullptr;
inline KVCtorFn kvCtor = nullptr;
inline KVSetIntFn kvSetInt = nullptr;
inline DispatchFn cmdKeyValues = nullptr;

inline bool resolveKickSymbols() noexcept
{
    if (kickSymbolsResolved)
        return engineInterface && cmdKeyValues;

    kickSymbolsResolved = true;
    DynamicLibrary tier0{cs2::TIER0_DLL};
    kvNew = tier0.getFunctionAddress("_ZN9KeyValuesnwEm").as<KVNewFn>();
    kvCtor = tier0.getFunctionAddress("_ZN9KeyValuesC1EPKcP16IKeyValuesSystemb").as<KVCtorFn>();
    kvSetInt = tier0.getFunctionAddress("_ZN9KeyValues6SetIntEPKci").as<KVSetIntFn>();
    if (!kvNew || !kvCtor || !kvSetInt)
        return false;

    DynamicLibrary engine2{cs2::ENGINE_DLL};
    const auto createInterface = engine2.getFunctionAddress("CreateInterface").as<CreateInterfaceFn>();
    if (!createInterface)
        return false;
    engineInterface = createInterface("Source2EngineToClient001", nullptr);
    if (!engineInterface)
        return false;

    const auto vtable = *reinterpret_cast<void***>(engineInterface);
    cmdKeyValues = reinterpret_cast<DispatchFn>(vtable[0x3B8 / 8]);
    return cmdKeyValues != nullptr;
}


inline void sendInvalidSteamLogon(int reason) noexcept
{
    if (!resolveKickSymbols())
        return;
    void* kv = kvNew(0x1C);
    if (kv)
        kvCtor(kv, "InvalidSteamLogon", nullptr, false);
    kvSetInt(kv, "reason", reason);
    cmdKeyValues(engineInterface, kv);
    
}

inline int kickBurstRemaining = 0;

















constexpr std::size_t kBufferTextSize2 = 160;

inline void* steamFriends = nullptr;
inline bool personaSymbolsResolved = false;

inline char personaRequested[kBufferTextSize2] = "";
inline int personaPollsLeft = 0;

using GetHSteamUserFn = int (*)();
using FindOrCreateUserInterfaceFn = void* (*)(int, const char*);
using GetPersonaNameFn = const char* (*)(void*);
using SetPersonaNameFn = bool (*)(void*, const char*);

inline bool resolvePersonaSymbols() noexcept
{
    if (personaSymbolsResolved)
        return steamFriends != nullptr;
    personaSymbolsResolved = true;

    VerifyConsole::write(0.0f, "persona", "resolving steam interface...");

    
    
    
    
    char steamApiPath[512] = "";
    {
        const int fd = LinuxPlatformApi::open("/proc/self/maps", 0 );
        if (fd >= 0) {
            constexpr std::size_t kCarry = 1024;
            char chunk[4096];
            char carry[kCarry];
            std::size_t carryLength = 0;
            off_t position = 0;
            bool found = false;
            while (!found) {
                const auto got = LinuxPlatformApi::pread(fd, chunk, sizeof(chunk), position);
                if (got <= 0)
                    break;
                position += static_cast<off_t>(got);

                std::size_t lineBegin = 0;
                for (std::size_t i = 0; i < static_cast<std::size_t>(got); ++i) {
                    if (chunk[i] != '\n')
                        continue;
                    char line[kCarry + 4096];
                    std::size_t lineLength = 0;
                    if (carryLength > 0) {
                        std::memcpy(line, carry, carryLength);
                        lineLength = carryLength;
                    }
                    const auto part = (i - lineBegin) < (sizeof(line) - lineLength - 1) ? (i - lineBegin) : (sizeof(line) - lineLength - 1);
                    std::memcpy(line + lineLength, chunk + lineBegin, part);
                    lineLength += part;
                    line[lineLength] = '\0';
                    carryLength = 0;

                    
                    
                    
                    
                    const char* path = line;
                    for (std::size_t j = 0; j < lineLength; ++j) {
                        if (line[j] == '/') {
                            path = line + j;
                            break;
                        }
                    }
                    if (std::strstr(path, "libsteam_api.so")) {
                        const auto rest = std::strlen(path);
                        std::memcpy(steamApiPath, path, rest < sizeof(steamApiPath) - 1 ? rest : sizeof(steamApiPath) - 1);
                        steamApiPath[rest < sizeof(steamApiPath) - 1 ? rest : sizeof(steamApiPath) - 1] = '\0';
                        found = true;
                        break;
                    }
                    lineBegin = i + 1;
                }

                if (found)
                    break;
                
                carryLength = 0;
                const std::size_t remaining = static_cast<std::size_t>(got) - lineBegin;
                if (remaining > 0 && remaining < kCarry) {
                    std::memcpy(carry, chunk + lineBegin, remaining);
                    carryLength = remaining;
                } else if (remaining >= kCarry) {
                    carryLength = 0; 
                }
            }
            LinuxPlatformApi::close(fd);
        }
    }
    if (steamApiPath[0] == '\0') {
        VerifyConsole::write(0.0f, "persona", "libsteam_api.so NOT in our maps - interface unavailable");
        return false;
    }
    VerifyConsole::write(0.0f, "persona", "steam_api at %s", steamApiPath);

    DynamicLibrary steamApi{steamApiPath};
    const auto getHUser = steamApi.getFunctionAddress("SteamAPI_GetHSteamUser").as<GetHSteamUserFn>();
    const auto findInterface = steamApi.getFunctionAddress("SteamInternal_FindOrCreateUserInterface").as<FindOrCreateUserInterfaceFn>();
    if (!getHUser || !findInterface) {
        VerifyConsole::write(0.0f, "persona", "steam_api exports missing");
        return false;
    }
    const int user = getHUser();
    steamFriends = findInterface(user, "SteamFriends017");
    if (!steamFriends) {
        VerifyConsole::write(0.0f, "persona", "SteamFriends017 unavailable (user %d)", user);
        return false;
    }
    VerifyConsole::write(0.0f, "persona", "SteamFriends017 ok");
    return true;
}








inline bool setSteamPersonaName(const char* name) noexcept
{
    if (!name || !resolvePersonaSymbols())
        return false;
    void* internal = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(steamFriends) + 0x8);
    if (!internal) {
        VerifyConsole::write(0.0f, "persona", "wrapper +0x8 is null - unexpected interface layout");
        return false;
    }
    const auto vtable = *reinterpret_cast<void***>(internal);
    const auto getPersonaName = reinterpret_cast<GetPersonaNameFn>(vtable[0]);
    const auto setPersonaName = reinterpret_cast<SetPersonaNameFn>(vtable[1]);
    if (!getPersonaName || !setPersonaName) {
        VerifyConsole::write(0.0f, "persona", "vtable slots 0/1 missing");
        return false;
    }

    
    
    
    
    const char* current = getPersonaName(internal);
    if (!current || current[0] == '\0') {
        VerifyConsole::write(0.0f, "persona", "GetPersonaName empty - wrong interface/slot?");
        return false;
    }
    {
        char safe[64];
        chat_tools::sanitizeInto(current, safe, sizeof(safe));
        VerifyConsole::write(0.0f, "persona", "current persona: %s", safe[0] ? safe : "?");
    }

    const bool submitted = setPersonaName(internal, name);
    if (!submitted) {
        VerifyConsole::write(0.0f, "persona", "SetPersonaName REFUSED (handle 0 - rate limited or bad state)");
        return false;
    }
    chat_tools::sanitizeInto(name, chat_tools::personaRequested, sizeof(chat_tools::personaRequested));
    
    
    chat_tools::personaPollsLeft = 30;
    VerifyConsole::write(0.0f, "persona", "SetPersonaName submitted - polling 30s...");
    return true;
}


inline bool teammateColorOffsetResolved = false;
inline std::optional<std::int32_t> teammateColorOffset;





inline constexpr cs2::ItemDefEntry kKickReasonEntries[] = {
    
    {163, "VACNet: Abnormal Behavior", ""},
    {164, "Insecure Client", ""},
    {162, "Input Automation", ""},
    {154, "Competitive Cooldown", ""},
    {153, "Convicted Account", ""},
    {152, "Untrusted Account", ""},
    {150, "Team Killing", ""},
    {151, "TK Round Start", ""},
    {155, "Team Hurting", ""},
    {156, "Hostage Killing", ""},
    {157, "Voted Off", ""},
    {158, "Idle (AFK)", ""},
    {159, "Suicide", ""},
    {160, "No Steam Login", ""},
    {161, "No Steam Ticket", ""},
    
    {13, "Steam VAC Ban State", ""},
    {6, "Steam Banned", ""},
    {9, "Steam Logon Invalid", ""},
    {12, "Steam Auth Invalid", ""},
    {11, "Steam Auth Already Used", ""},
    {10, "Steam Auth Cancelled", ""},
    {7, "Steam In Use", ""},
    {8, "Steam Ticket", ""},
    {14, "Steam Logged In Elsewhere", ""},
    {15, "Steam VAC Check Timed Out", ""},
    {16, "Steam Dropped", ""},
    {17, "Steam Ownership", ""},
    {68, "Steam Deny Bad Anti-Cheat", ""},
    {67, "Steam Deny Misc", ""},
    {66, "Server Requires Steam", ""},
    
    {39, "Kicked (Console)", ""},
    {40, "Ban Added", ""},
    {41, "Kick + Ban Added", ""},
    {84, "Unusual Activity", ""},
    {44, "Pure Server Mismatch", ""},
    {3, "Disconnected By Server", ""},
    {2, "Disconnected By User", ""},
    {69, "Server Shutdown", ""},
    {62, "Client Consistency Fail", ""},
    {49, "Bad Server Password", ""},
    {51, "Connection Failure", ""},
    {29, "Timed Out", ""},
    {4, "Connection Lost", ""},
    {5, "Connection Overflow", ""},
    {26, "Reliable Overflow", ""},
    {27, "Bad Delta Tick", ""},
    {1, "Shutdown", ""},
    {30, "Disconnected", ""},
};
inline constexpr int kKickReasonCount = static_cast<int>(sizeof(kKickReasonEntries) / sizeof(kKickReasonEntries[0]));



enum BufferKind { kNameBuffer = 0, kSpamBuffer = 1, kWheelBuffer = 2, kIntelBuffer = 3, kAnimatorBuffer = 4, kClanTagBuffer, kBufferKindCount };


constexpr std::size_t kBufferTextSize = 160; 

struct BufferState {
    char text[kBufferTextSize] = "";
    bool loaded = false;
};
inline BufferState buffers[kBufferKindCount]{};

inline constexpr const char* kFileNames[kBufferKindCount] = {
    "/chat_name.txt",
    "/chat_spam.txt",
    "/chatwheel.txt",
    "/chat_intel.txt",
    "/name_animator.txt",
    "/chat_clantag.txt",
};





inline constexpr const char* kFakeBanPersona = "{nl}Krypt0n disconnected [VAC banned from secure server]{nl}Krypt0n has been permanently banned from official servers.{nl}";



inline constexpr const char* kBadgeTail = " \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]";



inline constexpr const char* const kNamePresets[] = {
    "cat Enjoyer \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]",
    "s1mple \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]",
    "Valve Employee \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]",
    "VAC Live \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]",
    "VAC Live [VALVe]",
    kFakeBanPersona,
    "\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B",
    "\xEF\xBC\xB6\xEF\xBC\xA1\xEF\xBC\xA3 \xEF\xBC\xAC\xEF\xBC\x89\xEF\xBC\x96\xEF\xBC\x85",
    "\xE2\x80\xAE" "desiw taht txet yna",
    "{nl}[Server] This server will restart in 10 seconds{nl}[Server] Please finish your round{nl}",
    "{nl}You have received a competitive cooldown{nl}Reason: Griefing{nl}Expires in: 7 days{nl}",
    "{nl}Vote: Kick cat Enjoyer? (yes){nl}Vote passed 9-1 (90%){nl}cat Enjoyer was kicked from the match{nl}",
    "{nl}[ADMIN] cat Enjoyer is now the server owner{nl}[ADMIN] Type !rules in chat{nl}",
    "{nl}GOTV: Connecting to tournament relay...{nl}GOTV: Now spectating cat Enjoyer{nl}",
    
    "I'm not cheating my mom watches me play",
    "yes I bought this account from a guy on discord",
    "baiting is a legitimate strategy and I stand by it",
    "gg go next gg go next gg go next gg go next",
    
    "{nl}Thank you for your report.{nl}The suspect has been banned from official CS2 servers.{nl}",
    "{nl}Overwatch: The reviewed case resulted in a conviction.{nl}Thank you for contributing to community safety.{nl}",
    "{nl}Competitive Skill Group Updated{nl}Legendary Eagle Master{nl}",
    "{nl}CS Rating Update{nl}24,351 \xE2\x86\x92 14,203 (-10,148){nl}Your performance was below expectations{nl}",
    "{nl}Your Trust Factor has been significantly impacted{nl}by your recent in-game behavior{nl}",
    "{nl}You have received a rare item drop!{nl}\xE2\x98\x85 Karambit | Fade (Factory New){nl}",
    "{nl}Disconnection detected...{nl}Attempting to reconnect to the match{nl}",
    "{nl}Your account has been trade banned{nl}Reason: Fraudulent activity detected{nl}",
    
    "l\u200bol\u200Dol\u200Bol\u200Cl",                            
    "lol\u202Elol",                                                  
    "\u202E\u202D\u202A\u2066lol",                                
    "l\u0300o\u0301l\u0309o\u0302l\u0303",                       
    "lolol\u0301lolol\u0342olo",                                    
    "\u00B9\u2081\u2460\u00BD lol",                               
    "\uFF2C\uFF2F\uFF2C",                                          
    "1\u06F00 lol",                                                  
    "\u00A0l\u202Fol\u2009o\u00A0l",                              
    "${7*7} lol %{amount}",                                           
    "\\u00B9\\u2081\\u2460\\u00BD\\u06F0\\uFF11\\u00A0\\u00B9\\u2081\\u2460\\u00BD\\u06F0\\uFF11\\u00A0\\u00B9\\u2081\\u2460\\u00BD\\u06F0\\uFF11\\u00A0\\u00B9\\u2081\\u2460\\u00BD\\u06F0\\uFF11\\u00A0",
    
    "l\u0300\u0301\u0309\u0302\u0303\u0342\u0308\u030Cl\u0300\u0301\u0309\u0302\u0303\u0342\u0308\u030Cl\u0300\u0301\u0309\u0302\u0303\u0342\u0308\u030Cl\u0300\u0301\u0309\u0302\u0303\u0342\u0308\u030Cl\u0300\u0301\u0309\u0302\u0303\u0342\u0308\u030C",
    "\u202e\u202d\u202a\u2066\u2067\u202c\u202e\u202d\u202a\u2066\u2067\u202c\u202e\u202d\u202a\u2066\u2067\u202c\u202e\u202d\u202a\u2066\u2067\u202c",
    "\u200b\u200c\u200d\ufeff\u200e\u200b\u200c\u200d\ufeff\u200e\u200b\u200c\u200d\ufeff\u200e\u200b\u200c\u200d\ufeff\u200e\u200b\u200c\u200d\ufeff\u200e",
    "x\u200b\u202e\u0300x\u200d\u202d\u0301x\u200c\u202a\u0313x\u200b\u2066\u0342x\ufeff\u0308\u0309x\u200b\u202e\u0300x\u200d\u202d\u0301x\u200c\u202a\u0313",
    "\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F\uFF2C\uFF2F",
    "\u00B9\u2081\u2460\u00BD\u06F0\uFF11\u00A0\u00B9\u2081\u2460\u00BD\u06F0\uFF11\u00A0\u00B9\u2081\u2460\u00BD\u06F0\uFF11\u00A0\u00B9\u2081\u2460\u00BD\u06F0\uFF11\u00A0",
    
    "999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999",
    "NaN Infinity -Infinity NaN Infinity -Infinity NaN Infinity -Infinity NaN Infinity -Infinity NaN Infinity -Infinity",
    "1e309 9e999999999 1e-9999999999999999999 1E+2147483647 1e-2147483648 1e9999999999999999999 1e309 9e999999999",
    "2147483649 -9223372036854775809 18446744073709551616 100000000000000000000000000000 9007199254740993 0.30000000000000004",
    "1'; DROP TABLE players;-- 1' OR '1'='1 1; SELECT * FROM balances;-- 1\"; DROP TABLE economy;--",
    "%100d %.99999f %2147483647d %1$s {0} ${7*7} {{100}} %n %% %d %s %f ",
};
inline constexpr int kNamePresetCount = static_cast<int>(sizeof(kNamePresets) / sizeof(kNamePresets[0]));



inline char pendingNameText[kBufferTextSize] = "";


inline std::atomic<int> pendingCloneFragger{0};
inline std::atomic<int> pendingBanTheater{0};



inline int theaterTicksLeft = 0;


inline bool seeded = false;



inline constexpr const char* kDoppelgangerSuffix = "\xE2\x80\x8B\xE2\x80\x8B";


inline const char* kChatNamesScriptPack =
    "# Name Cycle / persona list - one name per line, '#' comments. {nl} = line break in chat.\n"
    "cat Enjoyer \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]\n"
    "s1mple \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]\n"
    "VAC Live \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]\n"
    "\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\n"
    "{nl}Krypt0n disconnected [VAC banned from secure server]{nl}Krypt0n has been permanently banned from official servers.{nl}\n"
    "{nl}[Server] This server will restart in 10 seconds{nl}[Server] Please finish your round{nl}\n"
    "{nl}You have received a competitive cooldown{nl}Reason: Griefing{nl}Expires in: 7 days{nl}\n"
    "{nl}Vote: Kick cat Enjoyer? (yes){nl}Vote passed 9-1 (90%){nl}cat Enjoyer was kicked from the match{nl}\n"
    "{nl}[ADMIN] cat Enjoyer is now the server owner{nl}[ADMIN] Type !rules in chat{nl}\n"
    "{nl}GOTV: Connecting to tournament relay...{nl}GOTV: Now spectating cat Enjoyer{nl}\n";


template <typename HookContext>
[[nodiscard]] bool sidecarPathLike(HookContext& hookContext, char (&path)[512], const char* fileName) noexcept
{
    const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
    if (!directoryPath)
        return false;
    const auto* dir = reinterpret_cast<const char*>(directoryPath.get());
    std::size_t length = 0;
    while (dir[length] != '\0' && length + 1 < sizeof(path) - 24)
        ++length;
    std::memcpy(path, dir, length);
    const auto fileLength = std::strlen(fileName);
    std::memcpy(path + length, fileName, fileLength + 1);
    return true;
}


template <typename HookContext>
[[nodiscard]] bool sidecarPathFor(HookContext& hookContext, char (&path)[512], BufferKind kind) noexcept
{
    return sidecarPathLike(hookContext, path, kFileNames[kind]);
}


inline int readFileSmall(const char* path, char* buffer, int capacity) noexcept
{
    const int fd = LinuxPlatformApi::open(path, 0 );
    if (fd < 0)
        return -1;
    const auto readBytes = LinuxPlatformApi::pread(fd, buffer, capacity, 0);
    LinuxPlatformApi::close(fd);
    if (readBytes > 0)
        buffer[readBytes] = '\0';
    return static_cast<int>(readBytes);
}



template <typename HookContext>
void seedSidecarIfMissing(HookContext& hookContext, const char* fileName, const char* content) noexcept
{
    char path[512];
    if (!sidecarPathLike(hookContext, path, fileName))
        return;
    const int probe = LinuxPlatformApi::open(path, 0 );
    if (probe >= 0) {
        LinuxPlatformApi::close(probe);
        return;
    }
    const int fd = LinuxPlatformApi::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    static_cast<void>(LinuxPlatformApi::write(fd, content, std::strlen(content)));
    LinuxPlatformApi::close(fd);
}


template <typename HookContext>
[[nodiscard]] bool readSidecar(HookContext& hookContext, BufferKind kind, char* out, std::size_t outCapacity) noexcept
{
    char path[512];
    if (!sidecarPathFor(hookContext, path, kind))
        return false;
    const int fd = LinuxPlatformApi::open(path, 0 );
    if (fd < 0)
        return false;
    char fileBuffer[512];
    const auto readBytes = LinuxPlatformApi::pread(fd, fileBuffer, sizeof(fileBuffer) - 1, 0);
    LinuxPlatformApi::close(fd);
    if (readBytes <= 0)
        return false;
    fileBuffer[readBytes] = '\0';
    std::size_t length = 0;
    while (length < static_cast<std::size_t>(readBytes) && fileBuffer[length] != '\n')
        ++length;
    fileBuffer[length] = '\0';
    if (length == 0 || fileBuffer[0] == '#')
        return false;
    return sanitizeInto(fileBuffer, out, outCapacity) != 0;
}




template <typename HookContext>
[[nodiscard]] int sidecarListCount(HookContext& hookContext, const char* fileName) noexcept
{
    char path[512];
    if (!sidecarPathLike(hookContext, path, fileName))
        return 0;
    char fileBuffer[4096];
    const auto readBytes = readFileSmall(path, fileBuffer, sizeof(fileBuffer) - 1);
    if (readBytes <= 0)
        return 0;
    int totalLines = 0;
    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(readBytes)) {
        std::size_t length = 0;
        while (offset + length < static_cast<std::size_t>(readBytes) && fileBuffer[offset + length] != '\n')
            ++length;
        fileBuffer[offset + length] = '\0';
        if (length > 0 && fileBuffer[offset] != '#')
            ++totalLines;
        offset += length + 1;
    }
    return totalLines;
}



template <typename HookContext>
[[nodiscard]] bool readSidecarListLine(HookContext& hookContext, const char* fileName, int lineIndex, char* out, std::size_t outCapacity) noexcept
{
    char path[512];
    if (!sidecarPathLike(hookContext, path, fileName))
        return false;
    char fileBuffer[4096];
    const auto readBytes = readFileSmall(path, fileBuffer, sizeof(fileBuffer) - 1);
    if (readBytes <= 0)
        return false;

    int enabled = -1;
    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(readBytes)) {
        std::size_t length = 0;
        while (offset + length < static_cast<std::size_t>(readBytes) && fileBuffer[offset + length] != '\n')
            ++length;
        fileBuffer[offset + length] = '\0';
        if (length > 0 && fileBuffer[offset] != '#') {
            if (++enabled == lineIndex) {
                return sanitizeInto(fileBuffer + offset, out, outCapacity) != 0;
            }
        }
        offset += length + 1;
    }
    return false;
}

} 

template <typename HookContext>
class ChatTools {
public:
    explicit ChatTools(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    static char* buffer(int kind) noexcept
    {
        return chat_tools::buffers[kind].text;
    }

    
    
    template <int Kind>
    void saveBufferToFile() noexcept
    {
        static_assert(Kind >= 0 && Kind < chat_tools::kBufferKindCount);
        char path[512];
        if (!chat_tools::sidecarPathFor(hookContext, path, static_cast<chat_tools::BufferKind>(Kind)))
            return;
        const int fd = LinuxPlatformApi::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
            return;
        char sanitized[chat_tools::kBufferTextSize];
        const auto length = chat_tools::sanitizeInto(chat_tools::buffers[Kind].text, sanitized, sizeof(sanitized) - 1);
        sanitized[length] = '\n';
        static_cast<void>(LinuxPlatformApi::write(fd, sanitized, length + 1));
        LinuxPlatformApi::close(fd);
    }

    
    void ensureBuffersLoaded() noexcept
    {
        for (int kind = 0; kind < chat_tools::kBufferKindCount; ++kind) {
            auto& state = chat_tools::buffers[kind];
            if (!state.loaded) {
                state.loaded = true;
                if (!readSidecar(hookContext, static_cast<chat_tools::BufferKind>(kind), state.text, sizeof(state.text)))
                    state.text[0] = '\0';
            }
        }
    }

    
    
    
    void run() noexcept
    {
        CrashLogger::trace(0x310); 
        ensureBuffersLoaded();
        seedScriptPacks();

        if (chat_tools::callerParity.fetch_add(1, std::memory_order_relaxed) % 2 != 0) {
            CrashLogger::trace(0x311);
            return; 
        }

        if (chat_tools::pendingNameApply.exchange(0, std::memory_order_acq_rel) != 0)
            applyFakeName();

        if (chat_tools::pendingCloneFragger.exchange(0, std::memory_order_acq_rel) != 0)
            cloneTopFragger();

        
        const bool theaterActive = runBanTheater(chat_tools::pendingBanTheater.exchange(0, std::memory_order_acq_rel) != 0);

        runIntel();
        runPersonaPoll();

        if (!theaterActive) {
            runSpam();
            runNameCycle();
        }
        runHudColorCycle();
        runWheel();
        runPingSpam();
        runLiveBadge();
        runFakeKick();
        runClanTag();
        runClanTagAnimation();
        CrashLogger::trace(0x311);
    }

    
    
    void onFireEventClientSide(cs2::IGameEvent* event) noexcept
    {
        if (!event)
            return;
        if (!GET_CONFIG_VAR(chat_vars::StreakRadioEnabled)) {
            chat_tools::streak = 0;
            return;
        }
        if (!game_events::is(event, "player_death"))
            return;
        CrashLogger::trace(0x320); 

        if (game_events::localPlayerIsAttacker(hookContext, event)) {
            ++chat_tools::streak;
            const char* line = nullptr;
            if (chat_tools::streak == 3)
                line = "Nice shot!";
            else if (chat_tools::streak == 5)
                line = "Excellent!";
            else if (chat_tools::streak == 8)
                line = "Everybody, get in here!";
            else if (chat_tools::streak == 12)
                line = "PEEK ME AGAIN I DARE YOU";
            if (line)
                sendWheel(line);
            return;
        }

        
        if (game_events::localPlayerIsSlot(hookContext, game_events::entityForKey(event, "userid")))
            chat_tools::streak = 0;
    }

private:
    
    void seedScriptPacks() noexcept
    {
        if (chat_tools::seeded)
            return;
        chat_tools::seeded = true;
        chat_tools::seedSidecarIfMissing(hookContext, "/chat_name.txt",
            "VAC Live \xE2\x81\xA7\xE2\x81\xA7[V\xCE\x91LV\xE1\xB4\xB1]\n");
        chat_tools::seedSidecarIfMissing(hookContext, "/chat_spam.txt", "gg go next\n");
        chat_tools::seedSidecarIfMissing(hookContext, "/chatwheel.txt", "playerchatwheel CW.GoGoGo test\n");
        chat_tools::seedSidecarIfMissing(hookContext, chat_tools::kNamesListFile, chat_tools::kChatNamesScriptPack);
        chat_tools::seedSidecarIfMissing(hookContext, "/chat_intel.txt",
            "Last enemy: B tunnels, low HP{nl}[Server] Intel packet delivered\n");
        chat_tools::seedSidecarIfMissing(hookContext, "/name_animator.txt", "name animator");
        chat_tools::seedSidecarIfMissing(hookContext, "/chat_clantag.txt", "CAT");
    }

    
    
    void cloneTopFragger() noexcept
    {
        const auto snap = player_list::snapshot();
        const player_list::Row* best = nullptr;
        for (int i = 0; i < snap.count; ++i) {
            const auto& row = snap.rows[i];
            if (row.isLocalPlayer || row.kills <= 0)
                continue;
            if (!best || row.kills > best->kills)
                best = &row;
        }
        if (!best) {
            VerifyConsole::write(30.0f, "chat", "clone: nobody else has kills yet (player list needs a live match)");
            return;
        }

        char clean[96];
        chat_tools::sanitizeInto(best->name, clean, sizeof(clean));
        if (clean[0] == '\0')
            return;

        char command[160]{"name "};
        std::size_t writeIndex = 5;
        const auto nameLength = std::strlen(clean);
        const auto suffixLength = std::strlen(chat_tools::kDoppelgangerSuffix);
        if (nameLength + suffixLength >= sizeof(command) - 5)
            return;
        std::memcpy(command + writeIndex, clean, nameLength);
        writeIndex += nameLength;
        std::memcpy(command + writeIndex, chat_tools::kDoppelgangerSuffix, suffixLength);
        writeIndex += suffixLength;
        command[writeIndex] = '\0';
        hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    
    
    
    
    [[nodiscard]] bool runBanTheater(bool requested) noexcept
    {
        if (requested) {
            char clean[chat_tools::kBufferTextSize];
            const auto length = chat_tools::sanitizeInto(chat_tools::kFakeBanPersona, clean, sizeof(clean));
            if (length > 0) {
                
                
                
                
                if (!nameCvarFlagsPatched) {
                    nameCvarFlagsPatched = hookContext.template make<CvarSystem>().patchUserInfoFlag("name");
                    if (!nameCvarFlagsPatched) {
                        VerifyConsole::write(0.0f, "theater", "could not patch the name cvar flags (offset unverifiable)");
                    }
                }
                if (nameCvarFlagsPatched) {
                    char command[196]{"setinfo name \""};
                    std::memcpy(command + 14, clean, length + 1);
                    const auto commandLength = std::strlen(command);
                    command[commandLength] = '"';
                    command[commandLength + 1] = '\0';
                    hookContext.template make<EngineCommandExecutor>().execute(command);
                    if (GET_CONFIG_VAR(chat_vars::NameForceReconnect))
                        chat_tools::setSteamPersonaName(clean);
                }
            }
            chat_tools::theaterTicksLeft = static_cast<int>(GET_CONFIG_VAR(chat_vars::TheaterDelay)) * 64;
            VerifyConsole::write(8.0f, "theater", "fake ban live - disconnecting in a moment");
        }
        
        
        
        
        
        if (chat_tools::theaterTicksLeft > 0 && --chat_tools::theaterTicksLeft == 0)
            hookContext.template make<EngineCommandExecutor>().execute("disconnect");
        return chat_tools::theaterTicksLeft > 0;
    }

    
    void sendSay(const char* text) noexcept
    {
        char command[128]{"say "};
        std::memcpy(command + 4, text, std::strlen(text) + 1);
        hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    
    
    
    
    
    struct WheelPhrase { const char* name; };
    static constexpr WheelPhrase kWheelPhrases[] = {
        {"CW.EcoRound"}, {"CW.SpendRound"}, {"CW.NeedDrop"}, {"CW.NeedPlan"},
        {"CW.NeedLeader"}, {"CW.GoGoGo"}, {"CW.OMW"}, {"CW.FollowMe"},
        {"CW.FollowingYou"}, {"CW.GoA"}, {"CW.GoB"}, {"CW.GoToLocMid"},
        {"CW.Regroup"}, {"CW.StickTogether"}, {"CW.SpreadOut"}, {"CW.TeamFallBack"},
        {"CW.HoldPosition"}, {"CW.NeedQuiet"}, {"CW.ImAttacking"}, {"CW.HeardNoise"},
        {"CW.SeesEnemy"},
    };

    

    void sendWheel(const char* cwName) noexcept
    {
        char command[96];
        const auto length = std::snprintf(command, sizeof(command), "playerchatwheel %s %s", cwName, cwName);
        if (length > 0)
            hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    
    
    void runIntel() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::IntelEnabled)) {
            if (chat_tools::intelWasEnabled) {
                chat_tools::intelWasEnabled = false;
                chat_tools::intelWasDown = false;
                chat_tools::pendingNameText[0] = '\0'; 
                applyFakeName();
            }
            return;
        }
        if (!chat_tools::intelWasEnabled) {
            chat_tools::intelWasEnabled = true;
            std::snprintf(chat_tools::pendingNameText, chat_tools::kBufferTextSize, "%s", chat_tools::kServerPersona);
            chat_tools::pendingNameApply.store(1, std::memory_order_release);
        }

        if (chat_tools::intelSendCooldown > 0)
            --chat_tools::intelSendCooldown;

        const auto key = GET_CONFIG_VAR(chat_vars::IntelBind);
        const bool down = key != 0 && Bind::isDown(key);
        if (down && !chat_tools::intelWasDown && chat_tools::intelSendCooldown == 0) {
            char text[96];
            if (chat_tools::readSidecar(hookContext, chat_tools::kIntelBuffer, text, sizeof(text))) {
                sendSay(text);
                chat_tools::intelSendCooldown = 128; 
            }
        }
        chat_tools::intelWasDown = down;
    }

    
    
    void runLiveBadge() noexcept
    {
        if (chat_tools::liveSayCooldown > 0)
            --chat_tools::liveSayCooldown;

        const auto snap = spectator_list::snapshot();
        const int count = snap.spectatingOthers ? 0 : snap.count; 
        if (count == chat_tools::lastSpectatorCount)
            return;

        if (GET_CONFIG_VAR(chat_vars::LiveBadgeEnabled) && count > chat_tools::lastSpectatorCount && count > 0
            && chat_tools::liveSayCooldown == 0) {
            char text[80];
            std::snprintf(text, sizeof(text), "\xF0\x9F\x94\xB4 LIVE - %d viewer(s) in stream", count);
            sendSay(text);
            chat_tools::liveSayCooldown = 640; 
        }
        chat_tools::lastSpectatorCount = count;
    }

    
    
    
    
    
    
    
    
    
    
    void runPersonaPoll() noexcept
    {
        if (chat_tools::personaPollsLeft <= 0)
            return;
        static int pollDivider = 0;
        if (++pollDivider < 64) 
            return;
        pollDivider = 0;
        --chat_tools::personaPollsLeft;

        if (!chat_tools::resolvePersonaSymbols())
            return;
        void* internal = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(chat_tools::steamFriends) + 0x8);
        const auto vtable = *reinterpret_cast<void***>(internal);
        const auto getPersonaName = reinterpret_cast<chat_tools::GetPersonaNameFn>(vtable[0]);
        const char* now = getPersonaName(internal);
        if (now && chat_tools::equalsIgnoreCase(now, chat_tools::personaRequested)) {
            char safe[64];
            chat_tools::sanitizeInto(now, safe, sizeof(safe));
            VerifyConsole::write(0.0f, "persona", "APPLIED - Steam persona is now: %s", safe[0] ? safe : "?");
            chat_tools::personaPollsLeft = 0;

            
            
            
            
            
            
            
            lastSentName[0] = '\0';
            sendName(chat_tools::personaRequested);
            VerifyConsole::write(0.0f, "name", "re-asserting in-game name");

            
            
            
            if (GET_CONFIG_VAR(chat_vars::NameForceReconnect)) {
                VerifyConsole::write(0.0f, "name", "reconnecting so the server picks up the new name");
                hookContext.template make<EngineCommandExecutor>().execute("retry");
            }
            return;
        }
        if (chat_tools::personaPollsLeft == 0)
            VerifyConsole::write(0.0f, "persona", "Steam did NOT apply in 30s - rate limit (wait 10+ min) or account restriction");
    }

    void runFakeKick() noexcept
    {
        const auto key = GET_CONFIG_VAR(chat_vars::KickKey);
        const bool down = key != 0 && Bind::isDown(key);
        if (down && !chat_tools::kickWasDown)
            chat_tools::kickAttemptsRemaining = 20;
        chat_tools::kickWasDown = down;

        if (chat_tools::kickAttemptsRemaining <= 0)
            return;
        if (chat_tools::kickSendCooldown > 0) {
            --chat_tools::kickSendCooldown;
            return;
        }
        chat_tools::kickSendCooldown = 256; 
        --chat_tools::kickAttemptsRemaining;

        chat_tools::sendInvalidSteamLogon(GET_CONFIG_VAR(chat_vars::KickReason));
    }

public:
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    inline static char clanTagOriginal[chat_tools::kBufferTextSize]{};
    inline static std::size_t clanTagOriginalLength{0};
    inline static const void* clanTagOriginalController{nullptr};
    inline static bool clanTagApplied{false};
    inline static bool clanTagLoggedEmpty{false};

    void resetClanTagState() noexcept
    {
        clanTagOriginalController = nullptr;
        clanTagOriginalLength = 0;
        clanTagOriginal[0] = '\0';
        clanTagApplied = false;
    }

    void restoreClanTag() noexcept
    {
        if (!clanTagApplied)
            return;
        if (const auto stringPtr = hookContext.localPlayerController().clanTagStringPointer();
            stringPtr.hasValue() && clanTagOriginalController == stringPtr.value()) {
            
            auto* const target = stringPtr.value();
            std::memcpy(target, clanTagOriginal, clanTagOriginalLength);
            target[clanTagOriginalLength] = '\0';
        }
        resetClanTagState();
    }

    void runClanTag() noexcept
    {
        const auto enabled = GET_CONFIG_VAR(chat_vars::ClanTagEnabled);
        
        
        const auto animate = GET_CONFIG_VAR(chat_vars::ClanTagAnimateEnabled);
        if (!enabled || animate) {
            if (!animate) 
                restoreClanTag();
            return;
        }

        
        
        if (!hookContext.localPlayerController().pawn())
            return;

        
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        const auto& bufferState = chat_tools::buffers[chat_tools::kClanTagBuffer];
        if (bufferState.text[0] == '\0') {
            restoreClanTag();
            return;
        }

        if (!ensureClanTagTarget())
            return;
        applyClanTagText(bufferState.text);
    }

    
    
    
    
    
    
    
    
    
    
    
    void runClanTagAnimation() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::ClanTagAnimateEnabled))
            return restoreClanTag();

        
        
        
        if (!hookContext.localPlayerController().pawn())
            return;

        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        const auto& bufferState = chat_tools::buffers[chat_tools::kClanTagBuffer];
        if (bufferState.text[0] == '\0')
            return restoreClanTag();

        
        
        
        
        
        if (!clanTagSeqCount && !buildClanTagSequences(bufferState.text))
            return restoreClanTag();

        
        
        const int ticksPerFrame = kClanTagSpeedMax - GET_CONFIG_VAR(chat_vars::ClanTagAnimateSpeed) + 1;
        if (clanTagTickCounter < ticksPerFrame) {
            ++clanTagTickCounter;
            return;
        }
        clanTagTickCounter = 0;

        if (!ensureClanTagTarget())
            return;

        char frame[chat_tools::kBufferTextSize]{};
        switch (GET_CONFIG_VAR(chat_vars::ClanTagAnimateMode)) {
        case 2:
            clanTagGlitchFrame(frame);
            break;
        case 3:
            clanTagMarqueeFrame(frame);
            break;
        case 4:
            clanTagWaveFrame(frame);
            break;
        case 5:
            clanTagStrobeFrame(frame);
            break;
        case 6:
            clanTagPulseFrame(frame);
            break;
        default:
            if (clanTagTypewriterFrame(frame))
                clanTagResetText(); 
            break;
        }
        applyClanTagText(frame);
    }

    void clanTagResetText() noexcept
    {
        clanTagSeqCount = 0; 
        clanTagPosition = 0;
        clanTagHoldLeft = 0;
        clanTagShrinking = false;
    }

    
    
    
    
    bool buildClanTagSequences(const char* text) noexcept
    {
        clanTagSeqCount = 0;
        clanTagPosition = 0;
        clanTagHoldLeft = 0;
        clanTagShrinking = false;
        std::size_t length = std::strlen(text);
        if (length >= sizeof(clanTagText))
            length = sizeof(clanTagText) - 1;
        std::memcpy(clanTagText, text, length);
        clanTagText[length] = '\0';
        for (std::size_t i = 0; i < length && clanTagSeqCount < kClanTagMaxSequences;) {
            std::size_t span = 1;
            while (i + span < length && (static_cast<unsigned char>(clanTagText[i + span]) & 0xC0) == 0x80)
                ++span;
            clanTagSeqOffset[clanTagSeqCount] = i;
            clanTagSeqLength[clanTagSeqCount] = span;
            ++clanTagSeqCount;
            i += span;
        }
        return clanTagSeqCount > 0;
    }

    [[nodiscard]] int clanTagChunk() const noexcept
    {
        
        
        const auto step = clanTagSeqCount / 4;
        return step > 0 ? step : 1;
    }

    void clanTagCopyVisible(char (&frame)[chat_tools::kBufferTextSize], int visible) const noexcept
    {
        std::size_t write = 0;
        for (int i = 0; i < visible && i < clanTagSeqCount; ++i) {
            if (write + clanTagSeqLength[i] >= sizeof(frame))
                break;
            std::memcpy(frame + write, clanTagText + clanTagSeqOffset[i], clanTagSeqLength[i]);
            write += clanTagSeqLength[i];
        }
        frame[write] = '\0';
    }

    
    [[nodiscard]] bool clanTagTypewriterFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (clanTagShrinking) {
            clanTagPosition -= clanTagChunk();
            if (clanTagPosition <= 0)
                return true; 
            clanTagCopyVisible(frame, clanTagPosition);
            return false;
        }
        if (clanTagHoldLeft > 0) {
            if (--clanTagHoldLeft == 0)
                clanTagShrinking = true;
            clanTagCopyVisible(frame, clanTagSeqCount);
            return false;
        }
        if (clanTagPosition >= clanTagSeqCount) {
            clanTagPosition = clanTagSeqCount;
            clanTagHoldLeft = kClanTagHoldUpdates;
            clanTagCopyVisible(frame, clanTagSeqCount);
            return false;
        }
        clanTagPosition += clanTagChunk();
        clanTagCopyVisible(frame, clanTagPosition);
        return false;
    }

    
    
    
    void clanTagGlitchFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        const char* const text = clanTagText;
        frame[0] = '\0';
        constexpr float kGlitchChance = 0.5f;
        std::size_t write = 0;
        for (int i = 0; i < clanTagSeqCount && write + 8 < sizeof(frame); ++i) {
            if (Random::floating(0.0f, 1.0f) >= kGlitchChance) {
                std::memcpy(frame + write, text + clanTagSeqOffset[i], clanTagSeqLength[i]);
                write += clanTagSeqLength[i];
            } else {
                const auto pick = static_cast<int>(Random::floating(0.0f, static_cast<float>(kClanTagGlitchSize)));
                frame[write++] = kClanTagGlitchCharset[pick];
            }
        }
        frame[write] = '\0';
    }

    
    
    void clanTagMarqueeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        const char* const text = clanTagText;
        frame[0] = '\0';
        std::size_t write = 0;
        for (int k = 0; k < clanTagSeqCount && write + 8 < sizeof(frame); ++k) {
            const int index = (clanTagPosition + k) % clanTagSeqCount;
            std::memcpy(frame + write, text + clanTagSeqOffset[index], clanTagSeqLength[index]);
            write += clanTagSeqLength[index];
        }
        constexpr const char seam[]{" |"};
        constexpr std::size_t seamLen = sizeof(seam) - 1;
        if (write + seamLen < sizeof(frame)) {
            std::memcpy(frame + write, seam, seamLen);
            write += seamLen;
        }
        frame[write] = '\0';
        clanTagPosition = (clanTagPosition + clanTagChunk()) % clanTagSeqCount;
    }

    
    void clanTagWaveFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr int kWavelength = 4;
        const int tick = ++clanTagFrameClock;
        const char* const text = clanTagText;
        std::size_t write = 0;
        for (int i = 0; i < clanTagSeqCount && write + 8 < sizeof(frame); ++i) {
            char c = text[clanTagSeqOffset[i]];
            if ((c & 0x80) == 0 && std::isalpha(static_cast<unsigned char>(c))) {
                const int height = ((i - tick) % kWavelength + kWavelength) % kWavelength;
                c = height < kWavelength / 2 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                                             : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            frame[write++] = c;
        }
        frame[write] = '\0';
    }

    
    void clanTagStrobeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        const int step = (++clanTagFrameClock) % 3;
        const char* const text = clanTagText;
        std::size_t write = 0;
        for (int i = 0; i < clanTagSeqCount && write + 4 < sizeof(frame); ++i) {
            const char c = text[clanTagSeqOffset[i]];
            if (step == 0)
                frame[write++] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            else if (step == 1)
                frame[write++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            else {
                frame[write++] = c;
                if (i + 1 < clanTagSeqCount && write + 4 < sizeof(frame))
                    frame[write++] = ' ';
            }
        }
        frame[write] = '\0';
    }

    
    
    void clanTagPulseFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr int kSegLen = 2; 
        const int cycle = clanTagSeqCount + kSegLen;
        const int head = clanTagPosition % cycle;
        const char* const text = clanTagText;
        std::size_t write = 0;
        for (int i = 0; i < clanTagSeqCount && write + 8 < sizeof(frame); ++i) {
            char c = text[clanTagSeqOffset[i]];
            if ((c & 0x80) == 0 && std::isalpha(static_cast<unsigned char>(c))) {
                const int distance = (i - head + cycle) % cycle;
                c = distance < kSegLen ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                                       : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            frame[write++] = c;
        }
        frame[write] = '\0';
        const auto step = clanTagChunk() > 1 ? clanTagChunk() : 2; 
        clanTagPosition += step;
    }

    
    
    void applyClanTagText(const char* frame) noexcept
    {
        if (!clanTagApplied)
            return;
        const auto stringPtr = hookContext.localPlayerController().clanTagStringPointer();
        if (!stringPtr.hasValue() || clanTagOriginalController != stringPtr.value())
            return; 
        auto* const target = stringPtr.value();
        char sanitized[chat_tools::kBufferTextSize];
        const auto wanted = chat_tools::sanitizeInto(frame, sanitized, sizeof(sanitized));
        const std::size_t length = wanted > clanTagOriginalLength ? clanTagOriginalLength : wanted;
        std::memcpy(target, sanitized, length);
        target[length] = '\0';
    }

    
    
    
    [[nodiscard]] bool ensureClanTagTarget() noexcept
    {
        const auto controller = hookContext.localPlayerController();
        const auto stringPtr = controller.clanTagStringPointer();
        if (!stringPtr.hasValue()) {
            restoreClanTag();
            return false;
        }

        auto* const target = stringPtr.value();
        if (clanTagApplied && clanTagOriginalController == target)
            return true; 

        if (clanTagOriginalController != target) {
            const std::size_t currentLength = std::strlen(target);
            if (currentLength == 0) {
                if (!clanTagLoggedEmpty) {
                    clanTagLoggedEmpty = true;
                    VerifyConsole::write(0.0f, "clantag", "no existing clan tag to overwrite (equip a Steam clan first)");
                }
                restoreClanTag();
                return false;
            }
            clanTagOriginalLength = currentLength < chat_tools::kBufferTextSize - 1 ? currentLength : chat_tools::kBufferTextSize - 1;
            std::memcpy(clanTagOriginal, target, clanTagOriginalLength);
            clanTagOriginal[clanTagOriginalLength] = '\0';
            clanTagOriginalController = target;
        }
        clanTagApplied = true;
        return true;
    }

    
    
    
    static constexpr int kClanTagMaxSequences = 96;
    static constexpr int kClanTagHoldUpdates = 8; 
    static constexpr int kClanTagSpeedMax = 64;   
    static constexpr char kClanTagGlitchCharset[] = "!<>-_\\/[]{}=+*^?#____";
    static constexpr int kClanTagGlitchSize = static_cast<int>(sizeof(kClanTagGlitchCharset)) - 1;
    inline static int clanTagTickCounter{0};
    inline static int clanTagPosition{0};
    inline static int clanTagHoldLeft{0};
    inline static bool clanTagShrinking{false};
    inline static int clanTagFrameClock{0};
    inline static int clanTagSeqCount{0};
    inline static char clanTagText[chat_tools::kBufferTextSize]{}; 
    inline static std::size_t clanTagSeqOffset[kClanTagMaxSequences]{};
    inline static std::size_t clanTagSeqLength[kClanTagMaxSequences]{};

private:
    
    inline static bool nameCvarFlagsPatched{false};
    
    
    inline static char lastSentName[chat_tools::kBufferTextSize2]{};

    
    
    
    bool ensureNameFlagsPatched() noexcept
    {
        if (nameCvarFlagsPatched)
            return true;
        nameCvarFlagsPatched = hookContext.template make<CvarSystem>().patchUserInfoFlag("name");
        if (!nameCvarFlagsPatched) {
            VerifyConsole::write(0.0f, "name", "could not patch the name cvar flags (offset unverifiable)");
            return false;
        }
        VerifyConsole::write(0.0f, "name", "name cvar flags patched (userinfo enabled)");
        return true;
    }

    
    
    
    void sendName(const char* text) noexcept
    {
        if (!ensureNameFlagsPatched())
            return;
        if (std::strcmp(text, lastSentName) == 0)
            return; 
        char command[184]{"setinfo name \""};
        const std::size_t room = sizeof(command) - 15 - 2;
        const std::size_t length = std::strlen(text) < room ? std::strlen(text) : room;
        std::memcpy(command + 14, text, length);
        command[14 + length] = '"';
        command[15 + length] = '\0';
        hookContext.template make<EngineCommandExecutor>().execute(command);
        std::snprintf(lastSentName, sizeof(lastSentName), "%s", text);
    }

    void applyFakeName() noexcept
    {
        char text[144];
        if (chat_tools::pendingNameText[0] != '\0') {
            
            
            chat_tools::sanitizeInto(chat_tools::pendingNameText, text, sizeof(text));
            chat_tools::pendingNameText[0] = '\0';
        } else if (!chat_tools::readSidecar(hookContext, chat_tools::kNameBuffer, text, sizeof(text))) {
            return;
        }
        if (text[0] == '\0')
            return;
        
        
        
        
        
        
        VerifyConsole::write(6.0f, "name", "applying fake name (live)");
        sendName(text);

        
        
        
        if (GET_CONFIG_VAR(chat_vars::NameForceReconnect))
            chat_tools::setSteamPersonaName(text);
    }

    void runSpam() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::SpamEnabled)) {
            chat_tools::spamTicksLeft = 0;
            chat_tools::spamLinesLeft = 0;
            chat_tools::spamCooldownLeft = 0;
            return;
        }
        if (chat_tools::spamCooldownLeft > 0) {
            --chat_tools::spamCooldownLeft;
            return;
        }
        if (chat_tools::spamTicksLeft > 0) {
            --chat_tools::spamTicksLeft;
            return;
        }
        if (chat_tools::spamLinesLeft == 0)
            chat_tools::spamLinesLeft = GET_CONFIG_VAR(chat_vars::SpamCount);

        char text[96];
        if (!readSidecar(hookContext, chat_tools::kSpamBuffer, text, sizeof(text)))
            return;
        char command[128]{"say "};
        std::memcpy(command + 4, text, std::strlen(text) + 1);
        hookContext.template make<EngineCommandExecutor>().execute(command);

        --chat_tools::spamLinesLeft;
        if (chat_tools::spamLinesLeft == 0)
            chat_tools::spamCooldownLeft = 192; 
        else
            chat_tools::spamTicksLeft = static_cast<int>(GET_CONFIG_VAR(chat_vars::SpamInterval)) * 6; 
    }

    void runWheel() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::WheelEnabled)) {
            chat_tools::wheelTicksLeft = 0;
            return;
        }
        if (chat_tools::wheelTicksLeft > 0) {
            --chat_tools::wheelTicksLeft;
            return;
        }
        const auto phraseIndex = static_cast<int>(GET_CONFIG_VAR(chat_vars::RadioPhrase));
        if (phraseIndex < 0 || phraseIndex >= chat_tools::kRadioPhraseCount)
            return;
        sendWheel(chat_tools::kRadioPhrases[phraseIndex].cwName);
        chat_tools::wheelTicksLeft = static_cast<int>(GET_CONFIG_VAR(chat_vars::WheelInterval)) * 6;
    }

    
    
    
    
    
    
    void runPingSpam() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::PingSpamEnabled)) {
            chat_tools::pingTicksLeft = 0;
            return;
        }
        if (chat_tools::pingTicksLeft > 0) {
            --chat_tools::pingTicksLeft;
            return;
        }
        hookContext.template make<EngineCommandExecutor>().execute("player_ping");
        chat_tools::pingTicksLeft = static_cast<int>(GET_CONFIG_VAR(chat_vars::PingInterval)) * 6;
    }

    void runNameCycle() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::NameCycleEnabled)) {
            chat_tools::cycleTicksLeft = 0;
            chat_tools::cycleIndex = 0;
            chat_tools::cycleCount = 0;
            return;
        }
        if (chat_tools::cycleTicksLeft > 0) {
            --chat_tools::cycleTicksLeft;
            return;
        }
        if (chat_tools::cycleCount == 0)
            chat_tools::cycleCount = chat_tools::sidecarListCount(hookContext, chat_tools::kNamesListFile);
        if (chat_tools::cycleCount == 0)
            return;
        if (chat_tools::cycleIndex >= chat_tools::cycleCount)
            chat_tools::cycleIndex = 0;

        char text[chat_tools::kBufferTextSize];
        if (!chat_tools::readSidecarListLine(hookContext, chat_tools::kNamesListFile, chat_tools::cycleIndex, text, sizeof(text))) {
            chat_tools::cycleCount = 0; 
            return;
        }
        ++chat_tools::cycleIndex;

        if (text[0] == '\0')
            return;
        
        
        
        sendName(text);
        chat_tools::cycleTicksLeft = static_cast<int>(GET_CONFIG_VAR(chat_vars::NameCycleInterval)) * 64; 
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    void runHudColorCycle() noexcept
    {
        if (!GET_CONFIG_VAR(chat_vars::HudColorCycle)) {
            chat_tools::colorTicksLeft = 0;
            return;
        }
        if (chat_tools::colorTicksLeft > 0) {
            --chat_tools::colorTicksLeft;
            return;
        }

        const auto changeTeammateColor = hookContext.patternSearchResults().template get<ChangeTeammateColorCycle>();
        if (!changeTeammateColor)
            return;
        changeTeammateColor();
        const int speed = GET_CONFIG_VAR(chat_vars::HudColorCycleSpeed) < 1 ? 1 : GET_CONFIG_VAR(chat_vars::HudColorCycleSpeed);
        chat_tools::colorTicksLeft = 64 / speed - 1;
        if (chat_tools::colorTicksLeft < 0)
            chat_tools::colorTicksLeft = 0;
    }

    HookContext& hookContext;
};
