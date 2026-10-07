#pragma once






























#include <pthread.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include <GameClient/KeyboardState.h>
#include <imgui.h>
#include <UI/ImGui/Neverlose/Neverlose.h> 
#include <Utils/NsPaths.h> 
#include <Utils/NsStr.h> 

struct lua_State;

namespace lua
{

inline constexpr int kMaxScripts = 16;
inline constexpr int kMaxHttpSlots = 8;
inline constexpr std::size_t kMaxScriptName = 128; 
inline constexpr std::size_t kMaxError = 384;
inline constexpr std::size_t kMaxScriptBytes = 256 * 1024;
inline constexpr std::size_t kMaxHttpBytes = 256 * 1024;
inline constexpr int kMaxGuiItems = 96;            
inline constexpr std::size_t kMaxGuiLabel = 48;
inline constexpr int kMaxGuiOptions = 64;          
inline constexpr int kMaxGuiText = 64;             
inline constexpr int kMaxEventString = 64;         
inline constexpr int kMaxPendingCalls = 32;        
inline constexpr int kMaxScriptTextures = 8;       
inline constexpr int kMaxImguiWidgets = 48;        
inline constexpr std::size_t kMaxImguiText = 128;  
inline constexpr std::size_t kMaxImguiTitle = 64;  


inline constexpr int kInstructionBudget = 50'000'000;

struct HttpSlot {
    bool active = false;
    bool processDone = false;
    pid_t pid = 0;
    int scriptIndex = -1;
    int callbackRef = -1; 
    char outPath[192] = {};
};





enum class ScriptPage : int
{
    Subtab = -1,
    Rage = 0,
    Legit,
    Movement,
    PlayerInfo,
    Glow,
    Viewmodel,
    Effects,
    Hud,
    Sound,
    Inventory,
    Radio,
    Scripts,
    Misc
};
inline constexpr int kScriptPageCount = 13; 
inline constexpr const char* kScriptPageNames[kScriptPageCount] = {
    "Rage", "Legit", "Movement", "Player Info", "Glow",
    "Viewmodel", "Effects", "Hud", "Sound", "Inventory", "Radio", "Scripts", "Misc"
};






struct GuiItem {
    enum class Type : unsigned char { Checkbox, Slider, Dropdown, Divider, Color, Keybind, FloatSlider, Text };
    Type type = Type::Checkbox;
    char label[kMaxGuiLabel] = {};
    bool boolValue = false; 
    int intValue = 0;       
    int minValue = 0;
    int maxValue = 100;
    float floatValue = 0.0f; 
    float floatMin = 0.0f;
    float floatMax = 1.0f;
    std::uint32_t colorValue = 0xFFFFFFFF; 
    char textValue[kMaxGuiText] = {};      
    int page = static_cast<int>(ScriptPage::Subtab); 
    
    int optionCount = 0;
    char optionStorage[kMaxGuiOptions][kMaxGuiLabel] = {};
    const char* optionPtrs[kMaxGuiOptions] = {};
};





struct ImguiWidgetState {
    char text[kMaxImguiText] = {};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    
    
    
    char label[kMaxGuiLabel] = {};
    char kind = 0;
};



struct PendingCall {
    double when = 0.0;
    int functionRef = -1;
    int argsRef = -1; 
};





struct EventArg {
    const char* key = nullptr;
    bool isNumber = false; 
    int intValue = 0;
    float numberValue = 0.0f;
    bool isString = false;
    char strValue[kMaxEventString] = {};
};


struct PlayerListEntry {
    int controllerIndex = 0; 
    int pawnIndex = 0;       
};

struct Script {
    char name[kMaxScriptName] = {};
    lua_State* L = nullptr;
    bool errored = false;
    char lastError[kMaxError] = {};
    bool hasPaint = false; 
    bool hasTick = false;
    bool hasMenu = false;  
    bool hasUnload = false; 
    GuiItem guiItems[kMaxGuiItems];
    int guiItemCount = 0;
    
    
    char tabLabel[kMaxGuiLabel] = {};
    
    ImguiWidgetState imguiWidgets[kMaxImguiWidgets];
    int imguiWidgetCount = 0;
    PendingCall pendingCalls[kMaxPendingCalls];
    int pendingCallCount = 0;
    int textureSlots[kMaxScriptTextures] = {}; 
};

struct FileEntry {
    char name[kMaxScriptName];
    long size;
};



extern Script scripts[kMaxScripts];
extern HttpSlot httpSlots[kMaxHttpSlots];
extern char scriptsDirPath[512];

extern std::atomic<int> stackBaseRepairCountForTesting;

extern bool (*stackBaseRepairLogQuery)() noexcept;


extern pthread_mutex_t mutex;


extern ImDrawList* paintDrawList;

extern pid_t (*spawnHostShellQuery)(const char* script) noexcept;


extern bool (*menuOpenQuery)() noexcept;



extern bool (*imguiContextQuery)() noexcept;












extern int (*localPlayerIndexQuery)() noexcept;



extern void (*luaTextureRequest)(int index, const void* pixelsRgba, int width, int height) noexcept;
extern void* (*luaTextureQuery)(int index) noexcept;
extern void (*luaTextureRelease)(int index) noexcept;

extern void* (*entityFromIndexQuery)(int entityIndex) noexcept;



extern int (*entityListQuery)(const char* className, int* outIndices, int max) noexcept;



extern bool (*entityOriginQuery)(int entityIndex, float* out) noexcept;

extern int (*schemaFieldOffsetQuery)(const char* className, const char* fieldName) noexcept;


extern bool (*entityClassNameQuery)(int entityIndex, char* outName, int nameCap) noexcept;

extern int (*playerListQuery)(PlayerListEntry* out, int max) noexcept;




extern int (*spectatorListQuery)(int targetPawnIndex, int* outControllerIndices, int max) noexcept;


extern void (*engineCommandQuery)(const char* command) noexcept;



extern bool (*cvarIntQuery)(const char* name, int* out) noexcept;
extern bool (*cvarFloatQuery)(const char* name, float* out) noexcept;

extern bool (*cvarFloatSetQuery)(const char* name, float value) noexcept;
extern bool (*cvarBoolSetQuery)(const char* name, bool value) noexcept;


extern bool (*worldToScreenQuery)(float x, float y, float z, float* ndcX, float* ndcY) noexcept;


extern std::atomic<int> dispatchThreadKind;









enum class ConfigValueKind : int { Bool = 'b', Uint = 'u', Float = 'f', Color = 'c' };
struct ConfigValue {
    ConfigValueKind kind = ConfigValueKind::Uint;
    bool boolValue = false;
    unsigned long long uintValue = 0;
    double floatValue = 0.0;
    unsigned char color[4] = {}; 
};
inline constexpr int kMaxConfigPath = 128;

extern int (*configEntryCountQuery)() noexcept;


extern bool (*configEntryAtQuery)(int index, char* outPath, int pathCap, int* outKind) noexcept;

extern bool (*configGetQuery)(const char* path, ConfigValue* out) noexcept;


extern bool (*configSetQuery)(const char* path, const ConfigValue* value) noexcept;

extern void (*configSaveQuery)() noexcept;



void init() noexcept;
bool load(const char* name) noexcept;      
void unloadScript(int index) noexcept;
void unloadAll() noexcept;
int listFiles(FileEntry* out, int max) noexcept;
bool readScript(const char* name, char* buffer, std::size_t bufferSize, long* outSize = nullptr) noexcept;
bool writeScript(const char* name, const char* content, std::size_t length) noexcept;
bool createScript(const char* name) noexcept;
bool deleteScript(const char* name) noexcept;
int loadedIndex(const char* name) noexcept;
bool validScriptName(const char* name) noexcept;






void formatSteamId64(std::uint64_t sid, char* out, std::size_t outSize) noexcept;
bool parseSteamId64(const char* text, std::uint64_t* out) noexcept;






void setCurrentMapName(const char* name) noexcept;



struct PatternByte {
    bool wildcard;
    unsigned char value;
};
inline constexpr int kMaxPatternBytes = 64;



int parseIdaPattern(const char* pattern, PatternByte* out, int maxBytes) noexcept;


const unsigned char* scanMemoryPattern(const unsigned char* data, std::size_t size,
    const PatternByte* bytes, int byteCount) noexcept;






void dispatchEvent(const char* eventName, const EventArg* args = nullptr, int argCount = 0) noexcept;

void dispatchPaint(ImDrawList* drawList) noexcept;




void dispatchMenuWindows() noexcept;


int scriptPageFromName(const char* name) noexcept;



void dispatchTick(void* userCmd = nullptr) noexcept;

} 
