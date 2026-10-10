#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include <CS2/Classes/CCSWeaponBaseVData.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/ScoreboardEquipment/ScoreboardEquipmentConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/BaseWeapon.h>
#include <GameClient/Entities/PlayerController.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/Hud/Hud.h>
#include <GameClient/KeyboardState.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>

// Scoreboard equipment reveal (ported from the reference Windows implementation): draws every
// player's carried weapons as icon rows next to their names ON THE GAME'S OWN SCOREBOARD via
// Panorama JS (RunScript into the HUD root). Native side walks player controllers, resolves
// weapon icon paths from the weapons' VData names ("weapon_ak47" -> "icons/equipment/ak47.svg"),
// adds armor/defuser from the pawn's item services, dedupes, sorts, and pushes per-player
// updates into the JS (SWeaponManager) installed once per session. Only sends when a player's
// set actually changed. All work happens on the game thread (render start) - RunScript is the
// engine's own JS entry, same thread the game runs panorama on.
template <typename HookContext>
class ScoreboardEquipment {
public:
    explicit ScoreboardEquipment(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(scoreboard_equipment_vars::Enabled)) {
            if (wasEnabled) {
                runScript(kClearScript);
                resetState();
            }
            return;
        }

        if (static_cast<cs2::C_BaseEntity*>(hookContext.localPlayerController().pawn()) == nullptr) {
            resetState();
            return;
        }

        if (++throttleCounter % kThrottle != 0)
            return;

        // Only walk + push while the scoreboard is up (TAB held): the icons live on the
        // scoreboard rows, and the entity walk (every pawn, every weapon, VData name read)
        // at 10Hz is what made deathmatch feel like it "slowly loads every player".
        if (!KeyboardState::isKeyDown(sdl3::scancode::kTab))
            return;

        if (!inited) {
            if (!runScript(kInitScript))
                return;
            inited = true;
        }
        wasEnabled = true;

        resolveOffsets();
        if (!offsetsReady())
            return;

        // All changed players batch into ONE RunScript per cycle (each receive() updates
        // one row) - one JS parse instead of one per player.
        batchLength = 0;
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::CCSPlayerController>())
                return;

            auto&& controller = hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(entityIdentity.entity));
            updatePlayer(controller, static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
        });

        if (batchLength > 0) {
            batch[batchLength] = '\0';
            runScript(batch);
        }
    }

private:
    static constexpr int kThrottle = 6;
    static constexpr int kMaxSlots = 32;
    static constexpr int kMaxWeapons = 16;
    static constexpr int kMaxPathChars = 48;
    static constexpr int kMaxScriptChars = 2048;
    // One cycle's concatenated receive() calls: up to kMaxSlots players per batch.
    static constexpr std::size_t kMaxBatchChars = 32 * 768;

    struct ResolvedOffsets {
        std::optional<std::int32_t> steamID;
        std::optional<std::int32_t> armorValue;
        std::optional<std::int32_t> itemServices;
        std::optional<std::int32_t> helmet;
        std::optional<std::int32_t> defuser;
        std::optional<std::int32_t> weaponType;

        [[nodiscard]] bool ready() const noexcept
        {
            return steamID.has_value() && armorValue.has_value() && itemServices.has_value()
                && helmet.has_value() && defuser.has_value() && weaponType.has_value();
        }
    };

    struct WeaponEntry {
        char path[kMaxPathChars]{};
        int type{};
        int sortKey{};
        bool grenade{};
    };

    struct PlayerCache {
        std::uint64_t xuid{};
        char script[kMaxScriptChars]{};
    };

    void resetState() const noexcept
    {
        inited = false;
        wasEnabled = false;
        for (auto& slot : cache)
            slot = PlayerCache{};
    }

    [[nodiscard]] static const char* stripWeaponPrefix(const char* name) noexcept
    {
        if (!name)
            return nullptr;
        if (std::strncmp(name, "weapon_", 7) == 0)
            return name + 7;
        return name;
    }

    // The reference's icon resolution: the VData name minus the "weapon_" prefix is the
    // equipment icon name (usp_silencer -> usp_silencer.svg, knife_karambit -> knife_karambit.svg).
    [[nodiscard]] static bool buildIconPath(char* out, std::size_t capacity, const char* vdataName) noexcept
    {
        const char* stripped = stripWeaponPrefix(vdataName);
        if (!stripped || !stripped[0])
            return false;
        const int written = std::snprintf(out, capacity, "icons/equipment/%s.svg", stripped);
        return written > 0 && static_cast<std::size_t>(written) < capacity;
    }

    [[nodiscard]] static bool isGrenadeName(const char* vdataName) noexcept
    {
        if (!vdataName)
            return false;
        return std::strstr(vdataName, "grenade") != nullptr
            || std::strstr(vdataName, "molotov") != nullptr
            || std::strstr(vdataName, "flashbang") != nullptr
            || std::strstr(vdataName, "decoy") != nullptr;
    }

    [[nodiscard]] static int sortKeyForType(int type, bool grenade, const char* path) noexcept
    {
        if (grenade)
            return 6;
        switch (type) {
        case 0: return 2; // knife
        case 1: return 1; // pistol
        case 2: case 3: case 4: case 5: case 6: return 0; // primary
        case 7: return 3; // c4
        case 8: return 4; // taser
        default: break;
        }
        if (std::strstr(path, "defuser"))
            return 7;
        if (std::strstr(path, "armor") || std::strstr(path, "kevlar"))
            return 5;
        return 8;
    }

    void updatePlayer(auto&& controller, cs2::C_BaseEntity* controllerEntity) const noexcept
    {
        const auto* controllerBytes = reinterpret_cast<const std::byte*>(controllerEntity);
        std::uint64_t xuid{};
        std::memcpy(&xuid, controllerBytes + *offsets().steamID, sizeof(xuid));
        if (xuid == 0)
            return;

        WeaponEntry weapons[kMaxWeapons]{};
        int weaponCount = 0;
        char activePath[kMaxPathChars]{};

        auto&& pawnEntity = controller.pawn();
        auto* const pawnPointer = static_cast<cs2::C_BaseEntity*>(pawnEntity.baseEntity());
        const auto pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(pawnPointer));
        const bool alive = pawnPointer && pawn.health().valueOr(0) > 0;

        if (alive && pawn.baseEntity().teamNumber() == controller.teamNumber()) {
            collectWeapons(pawn, weapons, weaponCount, activePath);
            collectArmorAndDefuser(pawn, pawnPointer, weapons, weaponCount);
            sortWeapons(weapons, weaponCount);
        }

        char script[kMaxScriptChars];
        buildUpdateScript(script, sizeof(script), xuid, weapons, weaponCount, activePath);

        PlayerCache* slot = findCacheSlot(xuid);
        if (!slot)
            return;
        if (slot->xuid == xuid && std::strcmp(slot->script, script) == 0)
            return;
        slot->xuid = xuid;
        std::strncpy(slot->script, script, sizeof(slot->script) - 1);
        slot->script[sizeof(slot->script) - 1] = '\0';

        // Append to the cycle batch instead of a RunScript per player.
        const std::size_t length = std::strlen(script);
        if (batchLength + length + 1 < sizeof(batch)) {
            std::memcpy(batch + batchLength, script, length);
            batchLength += length;
            batch[batchLength] = '\0';
        }
    }

    void collectWeapons(auto&& pawn, WeaponEntry* weapons, int& weaponCount, char* activePath) const noexcept
    {
        auto&& activeWeapon = pawn.getActiveWeapon();
        if (const auto name = activeWeapon.getName()) {
            buildIconPath(activePath, kMaxPathChars, name);
        }

        pawn.weapons().forEach([&](auto&& weaponEntity) {
            if (weaponCount >= kMaxWeapons)
                return;
            auto&& weapon = weaponEntity.template as<BaseWeapon>();

            const char* const vdataName = weapon.getName();
            if (!vdataName)
                return;
            const auto vdata = static_cast<cs2::CCSWeaponBaseVData*>(weapon.baseEntity().vData().valueOr(nullptr));
            if (!vdata)
                return;

            int type{};
            std::memcpy(&type, reinterpret_cast<const std::byte*>(vdata) + *offsets().weaponType, sizeof(type));
            const bool grenade = isGrenadeName(vdataName);
            if (type < 0 || type > 8 && !grenade)
                return;

            WeaponEntry entry{};
            if (!buildIconPath(entry.path, sizeof(entry.path), vdataName))
                return;
            entry.type = type;
            entry.grenade = grenade;
            entry.sortKey = sortKeyForType(type, grenade, entry.path);

            if (!grenade) {
                for (int i = 0; i < weaponCount; ++i) {
                    if (std::strcmp(weapons[i].path, entry.path) == 0)
                        return;
                }
            }
            weapons[weaponCount++] = entry;
        });
    }

    void collectArmorAndDefuser(auto&& pawn, cs2::C_BaseEntity* pawnPointer, WeaponEntry* weapons, int& weaponCount) const noexcept
    {
        const auto* pawnBytes = reinterpret_cast<const std::byte*>(pawnPointer);
        int armorValue{};
        std::memcpy(&armorValue, pawnBytes + *offsets().armorValue, sizeof(armorValue));
        const void* itemServices{};
        std::memcpy(&itemServices, pawnBytes + *offsets().itemServices, sizeof(itemServices));

        bool helmet = false;
        bool defuser = false;
        if (itemServices) {
            const auto* servicesBytes = static_cast<const std::byte*>(itemServices);
            std::memcpy(&helmet, servicesBytes + *offsets().helmet, sizeof(helmet));
            std::memcpy(&defuser, servicesBytes + *offsets().defuser, sizeof(defuser));
        }

        if (armorValue > 0 && weaponCount < kMaxWeapons) {
            WeaponEntry& entry = weapons[weaponCount++];
            std::strncpy(entry.path, helmet ? "icons/equipment/armor_helmet.vsvg" : "icons/equipment/kevlar.vsvg", sizeof(entry.path) - 1);
            entry.type = 10;
            entry.sortKey = 5;
        }
        if (defuser && weaponCount < kMaxWeapons) {
            WeaponEntry& entry = weapons[weaponCount++];
            std::strncpy(entry.path, "icons/equipment/defuser.svg", sizeof(entry.path) - 1);
            entry.type = 11;
            entry.sortKey = 7;
        }
    }

    static void sortWeapons(WeaponEntry* weapons, int weaponCount) noexcept
    {
        for (int i = 1; i < weaponCount; ++i) {
            const WeaponEntry key = weapons[i];
            int j = i - 1;
            while (j >= 0 && (weapons[j].sortKey > key.sortKey
                || (weapons[j].sortKey == key.sortKey && std::strcmp(weapons[j].path, key.path) > 0))) {
                weapons[j + 1] = weapons[j];
                --j;
            }
            weapons[j + 1] = key;
        }
    }

    static void buildUpdateScript(char* out, std::size_t capacity, std::uint64_t xuid, const WeaponEntry* weapons, int weaponCount, const char* activePath) noexcept
    {
        int offset = std::snprintf(out, capacity,
            "if(typeof SClient!=='undefined')SClient.receive({type:\"updateWeapons\",content:{xuid:\"%llu\",weapons:[", xuid);
        if (offset <= 0)
            return;
        for (int i = 0; i < weaponCount && static_cast<std::size_t>(offset) < capacity; ++i) {
            offset += std::snprintf(out + offset, capacity - offset,
                "%s{path:\"%s\",type:%d}", i > 0 ? "," : "", weapons[i].path, weapons[i].type);
            if (offset <= 0 || static_cast<std::size_t>(offset) >= capacity)
                return;
        }
        std::snprintf(out + offset, capacity - offset, "],active_path:\"%s\"}});", activePath);
    }

    // Runs one JS string on the HUD root panel (the context $.GetContextPanel() returns to
    // the scoreboard JS). The engine wrapper null-checks the engine + pattern itself.
    [[nodiscard]] bool runScript(const char* script) const noexcept
    {
        auto&& hudRoot = hookContext.hud().rootPanel();
        if (!hudRoot)
            return false;
        hookContext.template make<PanoramaUiEngine>().runScript(hudRoot, script);
        return true;
    }

    [[nodiscard]] PlayerCache* findCacheSlot(std::uint64_t xuid) const noexcept
    {
        for (auto& slot : cache) {
            if (slot.xuid == xuid)
                return &slot;
        }
        for (auto& slot : cache) {
            if (slot.xuid == 0) {
                slot.xuid = xuid;
                return &slot;
            }
        }
        return nullptr;
    }

    void resolveOffsets() const noexcept
    {
        if (offsets().ready())
            return;
        auto&& schema = hookContext.schemaSystem();
        offsets().steamID = schema.getFieldOffset("CCSPlayerController", "m_steamID");
        offsets().armorValue = schema.getFieldOffset("C_CSPlayerPawn", "m_ArmorValue");
        offsets().itemServices = schema.getFieldOffset("C_CSPlayerPawn", "m_pItemServices");
        offsets().helmet = schema.getFieldOffset("CCSPlayer_ItemServices", "m_bHasHelmet");
        offsets().defuser = schema.getFieldOffset("CCSPlayer_ItemServices", "m_bHasDefuser");
        offsets().weaponType = schema.getFieldOffset("CCSWeaponBaseVData", "m_WeaponType");
    }

    [[nodiscard]] bool offsetsReady() const noexcept
    {
        return offsets().ready();
    }

    [[nodiscard]] static ResolvedOffsets& offsets() noexcept
    {
        return offsetCache;
    }

    static constexpr const char* kClearScript{
        "if(typeof SWeaponManager!=='undefined')SWeaponManager.clear();"
    };

    // The reference's scoreboard JS, verbatim: installs SClient/SWeaponManager into the
    // scoreboard panel's JS context, widens the scoreboard + name columns, and creates the
    // per-player weapon icon rows (id-sb-name__nameicons container) with per-weapon sizing.
    static constexpr const char* kInitScript{
        "(function () {"
        "if (typeof SClient !== \"undefined\") SClient = undefined;"
        "SClient = (function () {"
        "    var handlers = {};"
        "    return {"
        "        register_handler: function (type, cb) { handlers[type] = cb; },"
        "        receive: function (msg) { if (msg && handlers[msg.type]) handlers[msg.type](msg); }"
        "    };"
        "})();"
        "SWeaponManager = (function () {"
        "    function getRow(xuid) {"
        "        var sb = $.GetContextPanel();"
        "        if (!sb) return null;"
        "        return sb.FindChildTraverse(\"player-\" + xuid) || sb.FindChildTraverse(\"id-\" + xuid);"
        "    }"
        "    return {"
        "        update: function (xuid, weapons, active_path) {"
        "            var sb = $.GetContextPanel();"
        "            if (sb) {"
        "                var sbMain = sb.FindChildTraverse(\"Scoreboard\") || (sb.id === \"Scoreboard\" ? sb : null);"
        "                if (!sbMain) {"
        "                    var cont = sb.FindChildTraverse(\"ScoreboardContainer\") || sb.FindChildTraverse(\"id-eom-scoreboard-container\");"
        "                    if (cont) sbMain = cont.FindChildTraverse(\"Scoreboard\") || cont;"
        "                }"
        "                if (sbMain) { sbMain.style.maxWidth = \"1800px\"; sbMain.style.width = \"1600px\"; }"
        "            }"
        "            var row = getRow(xuid);"
        "            if (!row) return;"
        "            var nameSection = row.FindChildTraverse(\"id-sb-name\");"
        "            if (nameSection) {"
        "                nameSection.style.maxWidth = \"600px\";"
        "                nameSection.style.minWidth = \"350px\";"
        "                nameSection.style.width = \"450px\";"
        "                nameSection.style.overflow = \"noclip\";"
        "            }"
        "            var nameIcons = row.FindChildTraverse(\"id-sb-name__nameicons\");"
        "            if (!nameIcons) return;"
        "            nameIcons.style.overflow = \"noclip\";"
        "            var cid = \"custom-weapons-container-\" + xuid;"
        "            var container = nameIcons.FindChildTraverse(cid);"
        "            if (!weapons || weapons.length === 0) {"
        "                if (container) container.style.visibility = \"collapse\";"
        "                return;"
        "            }"
        "            if (!container) {"
        "                container = $.CreatePanel(\"Panel\", nameIcons, cid);"
        "                container.AddClass(\"custom-weapons-container\");"
        "                container.style.flowChildren = \"right\";"
        "                container.style.height = \"100%\";"
        "                container.style.verticalAlign = \"center\";"
        "                container.style.paddingLeft = \"6px\";"
        "                container.style.overflow = \"noclip\";"
        "            }"
        "            container.style.visibility = \"visible\";"
        "            container.RemoveAndDeleteChildren();"
        "            for (var i = 0; i < weapons.length; ++i) {"
        "                var w = weapons[i];"
        "                if (!w || !w.path) continue;"
        "                var id = \"wep_\" + w.path.replace(/[^a-zA-Z0-9]/g, \"_\") + \"_\" + i;"
        "                var slot = $.CreatePanel(\"Panel\", container, id);"
        "                slot.style.height = \"100%\";"
        "                slot.style.width = \"fit-children\";"
        "                slot.style.verticalAlign = \"center\";"
        "                slot.style.margin = \"0px 1.5px\";"
        "                var img = $.CreatePanel(\"Image\", slot, \"img\");"
        "                var p = w.path;"
        "                var t = w.type;"
        "                var ww = \"14px\";"
        "                var hh = \"13px\";"
        "                if (p.indexOf(\"knife\") !== -1 || t === 0) { ww = \"32px\"; hh = \"12px\"; }"
        "                else if (p.indexOf(\"flashbang\") !== -1 || p.indexOf(\"smokegrenade\") !== -1 || p.indexOf(\"hegrenade\") !== -1 || p.indexOf(\"decoy\") !== -1) { ww = \"12px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"molotov\") !== -1 || p.indexOf(\"incgrenade\") !== -1) { ww = \"13px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"armor_helmet\") !== -1) { ww = \"22px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"kevlar\") !== -1) { ww = \"13px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"defuser\") !== -1 || t === 11) { ww = \"13px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"taser\") !== -1 || t === 8) { ww = \"16px\"; hh = \"12px\"; }"
        "                else if (p.indexOf(\"c4\") !== -1 || t === 7) { ww = \"18px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"usp_silencer\") !== -1) { ww = \"30px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"deagle\") !== -1) { ww = \"24px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"elite\") !== -1 || p.indexOf(\"revolver\") !== -1) { ww = \"22px\"; hh = \"13px\"; }"
        "                else if (t === 1) { ww = \"20px\"; hh = \"13px\"; }"
        "                else if (p.indexOf(\"mac10\") !== -1 || p.indexOf(\"mp9\") !== -1 || p.indexOf(\"mp7\") !== -1) { ww = \"22px\"; hh = \"14px\"; }"
        "                else if (p.indexOf(\"awp\") !== -1 || p.indexOf(\"ssg08\") !== -1 || p.indexOf(\"scar20\") !== -1 || p.indexOf(\"g3sg1\") !== -1) { ww = \"40px\"; hh = \"14px\"; }"
        "                else { ww = \"36px\"; hh = \"14px\"; }"
        "                img.style.width = ww;"
        "                img.style.height = hh;"
        "                img.style.verticalAlign = \"center\";"
        "                img.scaling = \"stretch-aspect-preserve\";"
        "                if (p.indexOf(\"file://\") !== 0) {"
        "                    if (p.indexOf(\"icons/equipment\") === -1) p = \"icons/equipment/\" + p;"
        "                    if (p.indexOf(\".svg\") === -1 && p.indexOf(\".vsvg\") === -1) p += \".svg\";"
        "                    p = \"file://{images}/\" + p;"
        "                }"
        "                img.SetImage(p);"
        "                slot.style.opacity = (w.path === active_path) ? \"1.0\" : \"0.45\";"
        "            }"
        "        },"
        "        clear: function () {"
        "            var sb = $.GetContextPanel();"
        "            if (!sb) return;"
        "            var cs = sb.FindChildrenWithClassTraverse(\"custom-weapons-container\");"
        "            for (var i = 0; i < cs.length; ++i) cs[i].DeleteAsync(0);"
        "        }"
        "    };"
        "})();"
        "SClient.register_handler(\"updateWeapons\", function (msg) {"
        "    if (msg && msg.content) {"
        "        SWeaponManager.update(msg.content.xuid, msg.content.weapons, msg.content.active_path);"
        "    }"
        "});"
        "})();"
    };

    inline static bool inited{false};
    inline static bool wasEnabled{false};
    inline static int throttleCounter{0};
    inline static ResolvedOffsets offsetCache{};
    inline static PlayerCache cache[kMaxSlots]{};
    inline static char batch[kMaxBatchChars]{};
    inline static std::size_t batchLength{0};

    HookContext& hookContext;
};
