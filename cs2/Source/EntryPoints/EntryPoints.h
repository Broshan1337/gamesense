#pragma once

#include <UI/ImGui/MenuInput.h>

#include "GlobalContext/HookQuiesce.h"
#include "GlobalContext/GlobalContext.h"
#include "Hooks/PeepEventsHook.h"
#include "Hooks/Graphics/VulkanHook.h"
#include "hooks/vac_hook.h"
#include "Platform/SelfUnload.h"
#include "Utils/ReturnAddress.h"
#include <Utils/CrashLogger.h>
#include <Utils/ConsoleBanner.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/RetAddrSpoofer.h>
#include <Utils/SessionBind.h>
#include <Security/Honeypots.h>
#include "Utils/StatusReport.h"
#include <cstdlib>
#include "Utils/VerifyConsole.h"

#include <SDL3/SDL_events.h>

#include <UI/ImGui/GUI.h>

#include <CS2/Econ/PaintKitIndex.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/EntitySystem/CEntityIndex.h>
#include <Features/Combat/Aimbot/Aimbot.h>
#include <Features/Combat/DuckPeek.h>
#include <Features/Visuals/Esp/Esp.h>
#include <Features/Visuals/BacktrackVisual/BacktrackVisual.h>
#include <Features/Visuals/Crosshair/CrosshairEsp.h>
#include <Features/Visuals/Radar/RadarEsp.h>
#include <Features/Visuals/BombInfo/BombInfoEsp.h>
#include <Features/Visuals/GrenadePrediction/GrenadePredictionEsp.h>
#include <Features/Visuals/SpectatorSync/SpectatorSync.h>
#include <Features/Visuals/EventLog/EventLog.h>
#include <Features/Combat/AntiAim/AntiAim.h>
#include <Features/Combat/DoubleTap/DoubleTap.h>
#include <Features/Combat/Resolver/Resolver.h>
#include <Features/Combat/Aimbot/AimbotFovCircle.h>
#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/SpreadCircleVis/SpreadCircleVis.h>
#include <Features/Combat/LegitAimbot/LegitAimbot.h>
#include <Features/Visuals/AnimationMods/AnimationMods.h>
#include <Features/Combat/Rcs/Rcs.h>
#include <Features/Combat/Triggerbot/Triggerbot.h>
#include <GameClient/Lagcomp.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/UserCmd.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Features/Radio/RadioManager.h>
#include <Features/Radio/SoundBoard.h>
#include <Features/Misc/DiscordRpc.h>
#include <Features/Misc/Freecam/Freecam.h>
#include <Features/Hud/HudThemeColor.h>
#include <Features/SkinChanger/SkinChanger.h>
#include <Features/Game/AgentChanger.h>
#include <Features/Game/AutoPeek.h>
#include <Features/Game/Blockbot.h>
#include <Features/Game/Bunnyhop.h>
#include <Features/Game/ChatTools.h>
#include <Features/Game/LastTickDefuse.h>
#include <Features/Game/Movement.h>
#include <Features/Game/NameAnimator.h>
#include <Features/Game/NetLag.h>
#include <Features/Game/UserInfoFlood.h>
#include <Features/Game/ServerLagger.h>
#include <Features/Game/SuperToss.h>
#include <Features/Game/GrenadeAim.h>
#include <Features/Game/NameAnimator.h>
#include <Features/Game/NetLag.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <Features/Game/TestStrafer.h>
#include <Features/Game/CooldownRevealer.h>
#include <Features/Game/FakeLevel.h>
#include <Features/Game/FakePremier.h>
#include <Features/Game/FakeCommends.h>
#include <Features/Game/FvaEmulator.h>
#include <Features/Game/IsValveDsSpoof.h>
#include <Features/Game/FakePrime.h>
#include <Features/Game/RevealRadar.h>
#include <Features/Game/SpectateEnemies.h>
#include <Features/Game/MatchAutoAccept.h>
#include <Features/Game/HitLog.h>
#include <Features/Game/Killsay.h>
#include <Features/Game/PanicKey.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Game/PlayerAnalyzer/PlayerAnalyzer.h>
#include <GameClient/VoiceTapHook.h>
#include <Features/Game/VoteRevealer.h>
#include <Features/Sound/HitSound.h>
#include <Features/Sound/SpawnProtectionSound.h>
#include <Features/Sound/WelcomeSound.h>
#include <Features/Hud/CombatStats/CombatStats.h>
#include <Features/Hud/SpectatorList/SpectatorList.h>
#include <Features/Visuals/Hitmarker/Hitmarker.h>
#include <Features/Visuals/PlayerList/PlayerList.h>
#include <Features/Visuals/ScoreboardEquipment/ScoreboardEquipment.h>
#include <Features/Visuals/OffScreenArrows/OffScreenArrows.h>
#include <Features/Visuals/BulletTracers/BulletTracers.h>
#include <Features/Visuals/BulletImpacts/BulletImpacts.h>
#include <Features/Visuals/KillEffects/KillEffects.h>
#include <Features/Visuals/Removals/Removals.h>
#include <Features/Visuals/ThirdPerson/ForceThirdPerson.h>
#include <Features/Visuals/WorldColors/WorldColors.h>
#include <Features/Lua/LuaManager.h>
#include <Features/Lua/LuaConfigBridge.h>
#include <Features/Game/GrenadeThrows.h>
#include <Hooks/SceneRenderHooks.h>
#include <Hooks/TeamSelectEventGuard.h>
#include <Hooks/ChamsHook.h>
#include <Features/Visuals/Chams/Chams.h>
#include <Features/Visuals/Chams/ChamsMaterials.h>
#include <UI/ImGui/GuiLog.h>

[[NOINLINE]] void finishInit(auto& hookContext)
{
    hookContext.entityClassifier().init(hookContext);
    hookContext.config().init();
    hookContext.config().scheduleLoad();

    
    
    lua::init();
    lua::menuOpenQuery = []() noexcept { return GUI::isMenuOpen(); };
    
    
    
    lua::imguiContextQuery = []() noexcept { return ImGui::GetCurrentContext() != nullptr; };

    
    
    
    lua::localPlayerIndexQuery = []() noexcept -> int {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return 0;
        HookContext<GlobalContext> context;
        return context.localPlayerController().baseEntity().handle().index().value;
    };
    lua::entityFromIndexQuery = [](int entityIndex) noexcept -> void* {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return nullptr;
        HookContext<GlobalContext> context;
        return context.make<EntitySystem>().getEntityFromIndex(cs2::CEntityIndex{entityIndex});
    };
    
    
    lua::luaTextureRequest = &VulkanHook::lua_texture::request;
    lua::luaTextureQuery = &VulkanHook::lua_texture::query;
    lua::luaTextureRelease = &VulkanHook::lua_texture::release;
    
    
    lua::entityOriginQuery = [](int entityIndex, float* out) noexcept -> bool {
        if (!out || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        const auto origin = context.make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(context.make<EntitySystem>().getEntityFromIndex(cs2::CEntityIndex{entityIndex}))).absOrigin();
        if (!origin.hasValue())
            return false;
        out[0] = origin.value().x;
        out[1] = origin.value().y;
        out[2] = origin.value().z;
        return true;
    };
    
    
    
    lua::entityListQuery = [](const char* className, int* outIndices, int max) noexcept -> int {
        if (!className || !outIndices || max <= 0 || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return 0;
        HookContext<GlobalContext> context;
        auto&& entitySystem = context.make<EntitySystem>();
        const auto entityClass = entitySystem.findEntityClass(className);
        if (!entityClass)
            return -1;
        int count = 0;
        entitySystem.forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (count >= max)
                return;
            if (entityIdentity.entityClass != entityClass)
                return;
            const int index = entityIdentity.handle.index().value;
            if (index > 0)
                outIndices[count++] = index;
        });
        return count;
    };
    lua::schemaFieldOffsetQuery = [](const char* className, const char* fieldName) noexcept -> int {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return -1;
        HookContext<GlobalContext> context;
        const auto offset = context.schemaSystem().getFieldOffset(className, fieldName);
        return offset.has_value() ? *offset : -1;
    };
    lua::playerListQuery = [](lua::PlayerListEntry* out, int max) noexcept -> int {
        if (!out || max <= 0 || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return 0;
        HookContext<GlobalContext> context;
        int count = 0;
        context.make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (count >= max)
                return;
            auto&& baseEntity = context.make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
            if (!baseEntity.classify().is<cs2::C_CSPlayerPawn>())
                return;
            const int controllerIndex = baseEntity.as<PlayerPawn>().playerController().baseEntity().handle().index().value;
            if (controllerIndex <= 0)
                return;
            out[count].controllerIndex = controllerIndex;
            out[count].pawnIndex = entityIdentity.handle.index().value;
            ++count;
        });
        return count;
    };
    
    
    
    
    lua::spectatorListQuery = [](int targetPawnIndex, int* out, int max) noexcept -> int {
        if (!out || max <= 0 || targetPawnIndex <= 0
            || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return 0;
        HookContext<GlobalContext> context;
        const auto servicesOffset = context.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pObserverServices");
        const auto modeOffset = context.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_iObserverMode");
        const auto targetOffset = context.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_hObserverTarget");
        if (!servicesOffset.has_value() || !modeOffset.has_value() || !targetOffset.has_value())
            return 0;
        int count = 0;
        context.make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (count >= max)
                return;
            auto&& baseEntity = context.make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
            if (!baseEntity.classify().is<cs2::C_CSPlayerPawn>())
                return;
            const auto pawn = reinterpret_cast<std::uintptr_t>(entityIdentity.entity);
            const auto services = *reinterpret_cast<void**>(pawn + *servicesOffset);
            if (!services)
                return;
            const auto servicesPtr = reinterpret_cast<std::uintptr_t>(services);
            if (*reinterpret_cast<const std::uint8_t*>(servicesPtr + *modeOffset) == 0)
                return; 
            const std::uint32_t handle = *reinterpret_cast<const std::uint32_t*>(servicesPtr + *targetOffset);
            if ((handle & 0x7FFF) != static_cast<std::uint32_t>(targetPawnIndex))
                return;
            const int controllerIndex = baseEntity.as<PlayerPawn>().playerController().baseEntity().handle().index().value;
            if (controllerIndex <= 0)
                return;
            out[count++] = controllerIndex;
        });
        return count;
    };
    
    
    lua::engineCommandQuery = [](const char* command) noexcept {
        if (!command || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return;
        HookContext<GlobalContext> context;
        context.template make<EngineCommandExecutor>().execute(command);
    };
    
    
    lua::cvarIntQuery = [](const char* name, int* out) noexcept -> bool {
        if (!name || !out || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        const auto value = context.template make<CvarSystem>().readIntConVar(name);
        if (!value.has_value())
            return false;
        *out = *value;
        return true;
    };
    lua::cvarFloatQuery = [](const char* name, float* out) noexcept -> bool {
        if (!name || !out || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        const auto value = context.template make<CvarSystem>().readFloatConVar(name);
        if (!value.has_value())
            return false;
        *out = *value;
        return true;
    };
    lua::cvarFloatSetQuery = [](const char* name, float value) noexcept -> bool {
        if (!name || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        return context.template make<CvarSystem>().forceFloatConVar(name, value);
    };
    lua::cvarBoolSetQuery = [](const char* name, bool value) noexcept -> bool {
        if (!name || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        return context.template make<CvarSystem>().forceBoolConVar(name, value);
    };
    
    
    
    lua::worldToScreenQuery = [](float x, float y, float z, float* ndcX, float* ndcY) noexcept -> bool {
        if (!ndcX || !ndcY || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        const auto clip = context.template make<WorldToClipSpaceConverter>().toClipSpace(cs2::Vector{x, y, z});
        if (!clip.onScreen())
            return false;
        const float inverseW = 1.0f / clip.w;
        *ndcX = clip.x * inverseW;
        *ndcY = clip.y * inverseW;
        return true;
    };
    
    
    
    lua::entityClassNameQuery = [](int entityIndex, char* outName, int nameCap) noexcept -> bool {
        if (!outName || nameCap <= 0 || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        const char* name = context.make<EntitySystem>().entityClassNameForIndex(entityIndex);
        if (!name || name[0] == '\0')
            return false;
        std::snprintf(outName, static_cast<std::size_t>(nameCap), "%s", name);
        return true;
    };
    
    
    
    lua::configEntryCountQuery = []() noexcept -> int {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return 0;
        HookContext<GlobalContext> context;
        return lua_config_bridge::entryCount(context);
    };
    lua::configEntryAtQuery = [](int index, char* outPath, int pathCap, int* outKind) noexcept -> bool {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        return lua_config_bridge::entryAt(context, index, outPath, pathCap, outKind);
    };
    lua::configGetQuery = [](const char* path, lua::ConfigValue* out) noexcept -> bool {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        return lua_config_bridge::configGet(context, path, out);
    };
    lua::configSetQuery = [](const char* path, const lua::ConfigValue* value) noexcept -> bool {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return false;
        HookContext<GlobalContext> context;
        return lua_config_bridge::configSet(context, path, value);
    };
    lua::configSaveQuery = []() noexcept {
        if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return;
        HookContext<GlobalContext> context;
        context.config().saveActive();
    };

    
    
    
    
    
    honey::keepAlive(); 
    if (GUI::init())
        (void)VulkanHook::tryInstall(); 

    
    
    
    
    
    
    
    const bool vtableHooksDisabled = std::getenv("NS_DISABLE_VTABLE_HOOKS") != nullptr;
    const bool gemDisabled = vtableHooksDisabled || std::getenv("NS_DISABLE_GEM_HOOK") != nullptr;
    const bool fsnDisabled = vtableHooksDisabled || std::getenv("NS_DISABLE_FSN_HOOK") != nullptr;
    const bool inputDisabled = vtableHooksDisabled || std::getenv("NS_DISABLE_INPUT_HOOK") != nullptr;
    if (gemDisabled)
        StatusReport::record("GEM hook fail-closed (bisect switch)", true);
    if (fsnDisabled)
        StatusReport::record("FSN hook fail-closed (bisect switch)", true);
    if (inputDisabled)
        StatusReport::record("CSGOInput hook fail-closed (bisect switch)", true);
    if (!fsnDisabled)
        hookContext.hooks().source2ClientHook.install();
    if (!gemDisabled)
        hookContext.hooks().gameEventManagerHook.install();
    if (!inputDisabled)
        hookContext.hooks().csgoInputHook.install();
    hookContext.hooks().viewRenderHook.install();
    
    
    (void)voice_tap::install();
    
    
    hookContext.template make<ClientModeHooks>().hookClientMode();
    
    
    (void)scene_render_hooks::install();

    if (team_select_guard::install(reinterpret_cast<std::uintptr_t>(hookContext.patternSearchResults().template get<TeamSelectEventGuardSite>())))
        StatusReport::record("TeamSelect event null-guard installed (game spawn-event crash workaround)", true);
    else
        StatusReport::record("TeamSelect event null-guard NOT installed (crash workaround inactive)", false);

    (void)chams_hook::install();


    static_cast<void>(netlag_hook::install());
}

int SDLHook_PeepEvents(void* events, int numevents, int action, unsigned minType, unsigned maxType) noexcept
{
    
    
    
    if (HookQuiesce::isShuttingDown())
        return 0;

    HookQuiesce::InFlight flight;
    const auto initInProgress = !HookContext<GlobalContext>::isGlobalContextComplete();
    if (initInProgress) {
        
        
        
        (void)session_bind::verifyOnce([] { GUI::requestUnload(); });
        HookContext<GlobalContext>::initCompleteGlobalContextFromGameThread();
    }

    HookContext<GlobalContext> hookContext;

    if (initInProgress)
        finishInit(hookContext);

    return menu_input::drain(events, numevents, action, minType, maxType,
        hookContext.hooks().peepEventsHook.original,
        [](SDL_Event* batch, int count) { return GUI::polledEvents(batch, count); });

}

[[NOINLINE]] void unload(auto& hookContext) noexcept
{
    
    
    lua::unloadAll();

    hookContext.template make<BombTimer>().onUnload();
    hookContext.template make<DefusingAlert>().onUnload();
    hookContext.template make<PostRoundTimer>().onUnload();
    hookContext.template make<OutlineGlow>().onUnload();
    hookContext.template make<BombStatusPanel>().onUnload();
    hookContext.template make<InWorldPanels>().onUnload();
    hookContext.template make<Blockbot>().onUnload();
    hookContext.template make<Bunnyhop>().onUnload();
    hookContext.template make<AnimationMods>().onUnload();
    hookContext.template make<Movement>().onUnload();
    hookContext.template make<SuperToss>().onUnload();
    hookContext.template make<LastTickDefuse>().onUnload();
    hookContext.template make<Triggerbot>().onUnload();
    hookContext.template make<RadioManager>().onUnload();
    hookContext.template make<soundboard::SoundBoard>().onUnload();
    hookContext.template make<DiscordRpc>().onUnload();
    hookContext.template make<FakePrime>().onUnload();
    hookContext.template make<FakeLevel>().onUnload();
    hookContext.template make<FakePremier>().onUnload();
    hookContext.template make<FakeCommends>().onUnload();
    hookContext.template make<MatchAutoAccept>().onUnload();
    hookContext.template make<CooldownRevealer>().onUnload();
    hookContext.template make<ChatTools>().restoreClanTag();
    
    {
        char path[192];
        if (ns_paths::joinFormat(path, sizeof(path), "ns_module_integrity", "_%d",
                LinuxPlatformApi::processId()))
            ::unlink(path);
    }
    hookContext.template make<userinfo_flood::UserInfoFlood>().onUnload();
    hookContext.template make<server_lagger::ServerLagger>().onUnload();
    
    
    hookContext.template make<Removals>().onUnload();
    hookContext.template make<ViewmodelMod>().onUnload();
    hookContext.template make<Freecam>().onUnload();
    scene_render_hooks::uninstall();
    team_select_guard::uninstall();
    chams_hook::uninstall();
    netlag_hook::unload();
    hookContext.hooks().viewRenderHook.uninstall();
    hookContext.hooks().source2ClientHook.uninstall();
    hookContext.hooks().gameEventManagerHook.uninstall();
    hookContext.hooks().csgoInputHook.uninstall();
    voice_tap::uninstall();
    hookContext.template make<ClientModeHooks>().restoreClientModeHooks();
    hookContext.template make<NoScopeInaccuracyVis>().onUnload();
    hookContext.template make<Aimbot>().onUnload();
    hookContext.template make<DuckPeek<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<Esp<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<BacktrackVisual<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<CrosshairEsp<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<RadarEsp<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<BombInfoEsp<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<GrenadePredictionEsp<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<BulletTracers>().onUnload();
    hookContext.template make<BulletImpacts>().onUnload();
    hookContext.template make<SpectatorSync<HookContext<GlobalContext>>>().onUnload();
    event_log::onUnload();
    hookContext.template make<AntiAim<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<DoubleTapGate<HookContext<GlobalContext>>>().onUnload();
    hookContext.template make<AimbotFovCircle>().onUnload();
    hookContext.template make<SpreadCircleVis>().onUnload();
    hookContext.template make<BombPlantAlert>().onUnload();

    hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&hookContext](const auto& entityIdentity) {
        auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
        const auto entityTypeInfo = baseEntity.classify();
        if (entityTypeInfo.template is<cs2::C_CSPlayerPawn>())
            hookContext.template make<ModelGlow>().onUnload()(PlayerModelGlow{hookContext}, baseEntity.template as<PlayerPawn>());
        else if (entityTypeInfo.template is<cs2::C_C4>())
            hookContext.template make<ModelGlow>().onUnload()(DroppedBombModelGlow{hookContext}, baseEntity.template as<BaseWeapon>());
        else if (entityTypeInfo.template is<cs2::CBaseAnimGraph>())
            hookContext.template make<ModelGlow>().onUnload()(DefuseKitModelGlow{hookContext}, baseEntity);
        else if (entityTypeInfo.template is<cs2::CPlantedC4>())
            hookContext.template make<ModelGlow>().onUnload()(TickingBombModelGlow{hookContext}, baseEntity.template as<PlantedC4>());
        else if (entityTypeInfo.isGrenadeProjectile())
            hookContext.template make<ModelGlow>().onUnload()(GrenadeProjectileModelGlow{hookContext}, baseEntity);
        else if (entityTypeInfo.isWeapon())
            hookContext.template make<ModelGlow>().onUnload()(WeaponModelGlow{hookContext}, baseEntity.template as<BaseWeapon>());
    });
}



















void Source2ClientHook_onFrameStageNotify(cs2::CSource2Client* thisptr, int frameStage) noexcept
{
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    hookContext.hooks().source2ClientHook.getOriginalOnFrameStageNotify()(thisptr, frameStage);
    if (shuttingDown)
        return;

    
    
    
    
    
    if (frameStage == 6 && (GET_CONFIG_VAR(aimbot_vars::Backtrack) || GET_CONFIG_VAR(aimbot_vars::Extrapolate) || GET_CONFIG_VAR(triggerbot_vars::Backtrack) || GET_CONFIG_VAR(legit_aimbot_vars::Backtrack) || GET_CONFIG_VAR(backtrack_visual_vars::Enabled)))
        hookContext.template make<Lagcomp>().run();

    
    
    
    
    
    
    

    
    
    
    
    if (frameStage == 6)
        hookContext.template make<ChatTools>().run();

    
    
    
    
    
    if (frameStage == 6)
        hookContext.template make<RadioManager>().runFrameStageNotify();
}







static int buildLuaEventArgs(lua::EventArg* out, const char* eventName, cs2::IGameEvent* event) noexcept
{
    constexpr int kMaxArgs = 16;
    int count = 0;
    if (!out || !eventName || !event)
        return 0;
    const auto addInt = [&](const char* key, int value) {
        if (count < kMaxArgs) {
            out[count].key = key;
            out[count].isNumber = false;
            out[count].isString = false;
            out[count].intValue = value;
            ++count;
        }
    };
    const auto addFloat = [&](const char* key, float value) {
        if (count < kMaxArgs) {
            out[count].key = key;
            out[count].isNumber = true;
            out[count].isString = false;
            out[count].numberValue = value;
            ++count;
        }
    };
    const auto addString = [&](const char* key, const char* value) {
        if (count < kMaxArgs && value && value[0] != '\0') {
            out[count].key = key;
            out[count].isNumber = false;
            out[count].isString = true;
            std::size_t i = 0;
            for (; value[i] != '\0' && i + 1 < sizeof(out[count].strValue); ++i)
                out[count].strValue[i] = value[i];
            out[count].strValue[i] = '\0';
            ++count;
        }
    };

    if (std::strcmp(eventName, "player_hurt") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addInt("attacker", static_cast<int>(game_events::entityForKey(event, "attacker")));
        addInt("dmg_health", game_events::intForKey(event, "dmg_health"));
        addInt("health", game_events::intForKey(event, "health"));
        addInt("armor", game_events::intForKey(event, "armor"));
        addInt("dmg_armor", game_events::intForKey(event, "dmg_armor"));
        addInt("hitgroup", game_events::intForKey(event, "hitgroup"));
        addString("weapon", game_events::stringForKey(event, "weapon"));
    } else if (std::strcmp(eventName, "player_death") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addInt("attacker", static_cast<int>(game_events::entityForKey(event, "attacker")));
        addInt("assister", static_cast<int>(game_events::entityForKey(event, "assister")));
        addInt("headshot", game_events::intForKey(event, "headshot"));
        addInt("dominated", game_events::intForKey(event, "dominated"));
        addString("weapon", game_events::stringForKey(event, "weapon"));
    } else if (std::strcmp(eventName, "weapon_fire") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addString("weapon", game_events::stringForKey(event, "weapon"));
    } else if (std::strcmp(eventName, "item_purchase") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addInt("team", game_events::intForKey(event, "team"));
        addString("weapon", game_events::stringForKey(event, "weapon"));
    } else if (std::strcmp(eventName, "bullet_impact") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addFloat("x", game_events::floatForKey(event, "x"));
        addFloat("y", game_events::floatForKey(event, "y"));
        addFloat("z", game_events::floatForKey(event, "z"));
    } else if (std::strcmp(eventName, "hegrenade_detonate") == 0
        || std::strcmp(eventName, "flashbang_detonate") == 0
        || std::strcmp(eventName, "smokegrenade_detonate") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addFloat("x", game_events::floatForKey(event, "x"));
        addFloat("y", game_events::floatForKey(event, "y"));
        addFloat("z", game_events::floatForKey(event, "z"));
    }
    return count;
}







bool GameEventManagerHook_onFireEventClientSide(cs2::IGameEventManager2* thisptr, cs2::IGameEvent* event) noexcept
{
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return true;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;

    if (shuttingDown)
        return hookContext.hooks().gameEventManagerHook.getOriginalFireEventClientSide()(thisptr, event);

    
    
    
    hookContext.template make<HitSound>().onFireEventClientSide(event);
    hookContext.template make<Hitmarker>().onFireEventClientSide(event);
    hookContext.template make<BulletTracers>().onFireEventClientSide(event);
    hookContext.template make<BulletImpacts>().onFireEventClientSide(event);
    hookContext.template make<KillEffects>().onFireEventClientSide(event);
    hookContext.template make<WorldColors>().onFireEventClientSide(event);
    hookContext.template make<HitLog>().onFireEventClientSide(event);
    hookContext.template make<TeamDamageTracker>().onFireEventClientSide(event);
    hookContext.template make<PlayerAnalyzer>().onFireEventClientSide(event);
    hookContext.template make<VoteRevealer>().onFireEventClientSide(event);
    hookContext.template make<SpawnProtectionSound>().onFireEventClientSide(event);
    hookContext.template make<CombatStats>().onFireEventClientSide(event);
    hookContext.template make<Killsay>().onFireEventClientSide(event);
    resolver::onFireEventClientSide(hookContext, event);
    event_log::onFireEventClientSide(hookContext, event);
    
    
    
    
    hookContext.template make<soundboard::SoundBoard>().onGameEvent(event);
    hookContext.template make<ChatTools>().onFireEventClientSide(event);
    
    
    
    const char* const eventName = game_events::name(event);
    
    
    
    
    if (game_events::is(event, "game_newmap")) {
        if (const char* mapName = game_events::stringForKey(event, "mapname"); mapName && mapName[0] != '\0') {
            lua::setCurrentMapName(mapName);
            grenade_throws::setCurrentMap(mapName);
        }
    }
    lua::EventArg luaArgs[16];
    const int luaArgCount = buildLuaEventArgs(luaArgs, eventName, event);
    lua::dispatchEvent(eventName, luaArgs, luaArgCount);

    return hookContext.hooks().gameEventManagerHook.getOriginalFireEventClientSide()(thisptr, event);
}








void CSGOInputHook_onCreateMove(cs2::CCSGOInput* thisptr, int slot, cs2::CUserCmd* cmd) noexcept
{
    CrashLogger::trace(0x370);
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    // engine input object feeds Movement::engineViewAngles(); captured from thisptr every frame.
    hookContext.template make<Movement>().captureInput(thisptr);
    hookContext.hooks().csgoInputHook.getOriginalCreateMove()(thisptr, slot, cmd);
    if (shuttingDown)
        return;

    
    
    
    auto&& panicKey = hookContext.template make<PanicKey>();
    panicKey.run();
    
    
    hookContext.template make<NetLag>().run();
    
    hookContext.template make<NameAnimator>().run();
    if (panicKey.isActive())
        return;

    
    
    
    
    
    // Preserve native subtick inputs; individual features edit only their own fields.

    
    
    hookContext.template make<IsValveDsSpoof>().run();

    hookContext.template make<Blockbot>().onCreateMove(cmd);
    
    
    hookContext.template make<SuperToss>().onCreateMove(cmd);
    hookContext.template make<GrenadeAim>().onCreateMove(cmd);
    
    
    
    hookContext.template make<LastTickDefuse>().onCreateMove(cmd);
    
    
    
    hookContext.template make<Movement>().onCreateMove(cmd);
    
    
    
    
    hookContext.template make<Bunnyhop>().onCreateMove(cmd);
    
    
    hookContext.template make<TestStrafer>().onCreateMove(cmd);

    // grenade ESP produces on the GAME thread: the trajectory reads view
    // angles + velocity + traces, which tear mid-tick on the render thread.
    hookContext.template make<GrenadePredictionEsp<HookContext<GlobalContext>>>().run();



    hookContext.template make<Aimbot>().onCreateMove(cmd);
    const bool rageShotStaged = hookContext.template make<Aimbot>().shotStaged();
    hookContext.template make<DuckPeek<HookContext<GlobalContext>>>().onCreateMove(rageShotStaged);
    hookContext.template make<AntiAim<HookContext<GlobalContext>>>().onCreateMove(cmd, rageShotStaged);
    hookContext.template make<LegitAimbot>().onCreateMove(cmd);
    
    hookContext.template make<Rcs>().onCreateMove(cmd);
    
    
    
    hookContext.template make<AutoPeek>().onCreateMove(cmd);
    
    
    hookContext.template make<FvaEmulator>().onCreateMove(cmd);

    
    
    

    
    hookContext.template make<AgentChanger>().run();

    // chams materials: create the vmat set ONCE on the main thread
    // (see chams_hook::onGeneratePrimitives).
    if ((GET_CONFIG_VAR(chams_vars::Enabled)
            || GET_CONFIG_VAR(chams_vars::ViewmodelEnabled)
            || GET_CONFIG_VAR(chams_vars::OwnWeaponsEnabled))
        && chams_materials::needsCreation()
        && !chams_materials::ensureMaterialsCreated())
        VerifyConsole::write(0.0f, "chams-mat", "creation failed: %s (color-only fallback)",
            chams_materials::lastError());

    
    
    hookContext.template make<RevealRadar>().run();

    
    
    
    
    hookContext.template make<SpectateEnemies>().run();

    
    
    
    hookContext.template make<ChatTools>().run();

    
    
    
    
    hookContext.template make<userinfo_flood::UserInfoFlood>().run();

    
    
    
    
    hookContext.template make<server_lagger::ServerLagger>().run();

    
    
    
    hookContext.template make<soundboard::SoundBoard>().run();

    
    
    
    CrashLogger::trace(0x371);
    lua::dispatchTick(cmd);
}







std::uint64_t CSGOInputHook_onBuildUserCmd(cs2::CCSGOInput* thisptr, int slot, int frameNumber) noexcept
{
    CrashLogger::trace(0x372);
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return 0;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    if (!shuttingDown && !hookContext.template make<PanicKey>().isActive()) {
        hookContext.template make<Blockbot>().onBuildUserCmd(thisptr, slot);
    }
    CrashLogger::trace(0x373);
    return hookContext.hooks().csgoInputHook.getOriginalBuildUserCmd()(thisptr, slot, frameNumber);
}







std::uint64_t CSGOInputHook_onWriteMoveCrc(cs2::CCSGOInput* thisptr, cs2::CUserCmd* cmd) noexcept
{
    CrashLogger::trace(0x374);
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return 0;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    hookContext.template make<Movement>().captureInput(thisptr);
    if (!shuttingDown && !hookContext.template make<PanicKey>().isActive()) {
    hookContext.template make<Blockbot>().onWriteMoveCrc(cmd);
    
    
    hookContext.template make<SuperToss>().onWriteMoveCrc(cmd);
    hookContext.template make<GrenadeAim>().onWriteMoveCrc(cmd);
    hookContext.template make<LastTickDefuse>().onWriteMoveCrc(cmd);
    
    
    hookContext.template make<Movement>().onWriteMoveCrc(cmd);
    
    
    
    hookContext.template make<TestStrafer>().onWriteMoveCrc(cmd);



        hookContext.template make<FvaEmulator>().onWriteMoveCrcEarly(cmd);
        
        
        
        
        hookContext.template make<DuckPeek<HookContext<GlobalContext>>>().onWriteMoveCrc(cmd);
        hookContext.template make<Triggerbot>().onWriteMoveCrc(cmd);
        
        
        
        hookContext.template make<Aimbot>().onWriteMoveCrc(cmd);
        // doubletap gate: after the aimbot/triggerbot writers (sees every fire intent).
        hookContext.template make<DoubleTapGate<HookContext<GlobalContext>>>().onWriteMoveCrc(cmd);
        // anti-aim writes base viewangles here: writes in CreateMove travel
        // with a stale checksum and the server drops them. real angles are
        // restored right after the original call below.
        hookContext.template make<AntiAim<HookContext<GlobalContext>>>().onWriteMoveCrc(cmd);
        hookContext.template make<LegitAimbot>().onWriteMoveCrc(cmd);
        
        
        
        
        
        
        hookContext.template make<FvaEmulator>().onWriteMoveCrcLate(cmd);
        // Commit movement against the final command view basis.
        hookContext.template make<Bunnyhop>().onWriteMoveCrc(cmd);
        // Freecam: zero the command's movement so the body stays put while
        // the camera flies (runs after Bunnyhop so its staged jumps don't
        // move the player either).
        hookContext.template make<Freecam>().onWriteMoveCrc(cmd);

        
        
        
        if (GET_CONFIG_VAR(movement_vars::Desubtick))
            SubtickMoves<HookContext<GlobalContext>>::stripAnalog(UserCmd{cmd}.baseMessage());
    }
    CrashLogger::trace(0x375);

    // save/restore the command's real angles around the original call: it
    // serializes + CRCs the (possibly faked) angles; the local view must not
    // follow them.
    const UserCmd saveRestoreCmd{cmd};
    const auto savedAngles = saveRestoreCmd.readRawViewAngles();
    const auto result = hookContext.hooks().csgoInputHook.getOriginalWriteMoveCrc()(thisptr, cmd);
    if (savedAngles.hasValue())
        saveRestoreCmd.restoreRawViewAngles(savedAngles.value());
    return result;
}

void ViewRenderHook_onRenderStart(cs2::CViewRender* thisptr) noexcept
{
    
    
    if (HookQuiesce::isShuttingDown() && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;

    HookContext<GlobalContext> hookContext;
    hookContext.clearRenderHookState();
    hookContext.hooks().viewRenderHook.getOriginalOnRenderStart()(thisptr);


    
    
    
    (void)VulkanHook::tryInstall(); 

    
    
    hookContext.template make<WelcomeSound>().run();

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (!hookContext.entityClassifier().initialized())
        hookContext.entityClassifier().init(hookContext);

    if (const auto mapTime = hookContext.globalVars().curtime(); mapTime.hasValue() && mapTime.value() >= schema_readiness::kMinMapTime)
        hookContext.template make<SkinChanger>().run();

    hookContext.make<InWorldPanels>().updateState();
    SoundWatcher<decltype(hookContext)> soundWatcher{hookContext.soundWatcherState(), hookContext};
    soundWatcher.update();
    SoundFeatures{hookContext.soundWatcherState(), hookContext.hooks().viewRenderHook, hookContext}.runOnViewMatrixUpdate();
    hookContext.template make<Hitmarker>().run();
    hookContext.template make<OffScreenArrows>().run();

    hookContext.template make<SpectatorSync<HookContext<GlobalContext>>>().update();
    hookContext.template make<CrosshairEsp<HookContext<GlobalContext>>>().run();
    hookContext.template make<Esp<HookContext<GlobalContext>>>().run();
    hookContext.template make<BacktrackVisual<HookContext<GlobalContext>>>().run();
    hookContext.template make<RadarEsp<HookContext<GlobalContext>>>().run();
    hookContext.template make<BombInfoEsp<HookContext<GlobalContext>>>().run();
    hookContext.template make<AntiAim<HookContext<GlobalContext>>>().onRenderUpdate();
    hookContext.template make<KillEffects>().run();
    hookContext.template make<BulletTracers>().run();
    hookContext.template make<BulletImpacts>().run();
    hookContext.template make<PlayerList>().run();
    hookContext.template make<ScoreboardEquipment>().run();
    
    
    hookContext.template make<PlayerAnalyzer>().run();
    
    voice_tap::drainProbe(hookContext);
    hookContext.template make<WorldColors>().run();
    
    hookContext.template make<ViewmodelMod>().run();
    hookContext.template make<Removals>().run();

    hookContext.template make<SpawnProtectionSound>().run();
    hookContext.template make<CooldownRevealer>().run();
    hookContext.template make<FakePrime>().run();
    hookContext.template make<FakeLevel>().run();
    hookContext.template make<FakePremier>().run();
    hookContext.template make<FakeCommends>().run();
    hookContext.template make<MatchAutoAccept>().run();
    hookContext.make<NoScopeInaccuracyVis>().update();
    hookContext.make<AimbotFovCircle>().update();
    hookContext.make<SpreadCircleVis>().update();
    hookContext.template make<AnimationMods>().run();
    hookContext.make<RenderingHookEntityLoop>().run();
    hookContext.make<GlowSceneObjects>().removeUnreferencedObjects();
    hookContext.make<DefusingAlert>().run();
    hookContext.make<KillfeedPreserver>().run();
    hookContext.make<BombStatusPanelManager>().run();
    hookContext.make<InWorldPanels>().hideUnusedPanels();
    hookContext.template make<CombatStats>().run();
    hookContext.template make<HudThemeColor>().run();
    hookContext.template make<SpectatorList>().run();

    
    
    
    // console banner: NS logo once, ahead of the status lines; gui.log stays anomaly-only.
    console_banner::printOnce();

    StatusReport::dumpOnce([](const char* message, bool ok) {
        
        
        gui_log::write("[status] %s %s", ok ? "OK" : "FAIL-CLOSED", message);
        VerifyConsole::write(0.0f, "status", "%s %s", ok ? "OK" : "FAIL-CLOSED", message);
    });

    UnloadFlag unloadFlag;
    
    
    
    
    
    {
        char unloadRequestPath[192];
        if (ns_paths::join(unloadRequestPath, sizeof(unloadRequestPath), "ns_unload_request")
            && ::access(unloadRequestPath, F_OK) == 0) {
            ::unlink(unloadRequestPath);
            GUI::requestUnload();
        }
    }
    
    
    
    
    {
        
        
        session_bind::presentTick([] { GUI::requestUnload(); });
    }
    if (GUI::consumeUnloadRequest())
        unloadFlag.set();
    hookContext.config().update();
    hookContext.config().performFileOperation();

    if (unloadFlag) {
        SelfUnload::log("unload: flag set - tearing down features and hooks");
        
        
        
        // Restore temporary config overrides before ui_config rejects shutdown writes.
        neverlose::restoreFeatureBinds();
        HookQuiesce::beginShutdown();
        
        
        
        
        hookContext.hooks().peepEventsHook.disable();
        
        
        
        VulkanHook::restorePointers();
        
        
        
        
        
        
        fva::hooks::uninstall_vac_hook();
        unload(hookContext);
        
        
        HookQuiesce::drainInFlightCallbacks();

        
        
        hookContext.template make<AttackCommand>().reset();
        hookContext.template make<Triggerbot>().disarmStatics();
        hookContext.template make<Lagcomp>().clearSlots();

        
        
        
        
        VulkanHook::destroyResources();
        GUI::destroy();

        HookContext<GlobalContext>::destroyGlobalContext();
        SelfUnload::log("unload: teardown complete, scheduling self-unmap");

        
        
        
        
        
        
        
        
        SelfUnload::unmapSelf();
    }
}

LINUX_ONLY([[gnu::aligned(8)]]) std::uint64_t PlayerPawn_sceneObjectUpdater(cs2::C_CSPlayerPawn* playerPawn, void* unknown, bool unknownBool) noexcept
{
    if (HookQuiesce::isShuttingDown()) {
        
        
        return 0;
    }
    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    const auto originalReturnValue = RetAddrSpoofer::spoof(hookContext.featuresStates().visualFeaturesStates.modelGlowState.originalPlayerPawnSceneObjectUpdater)(playerPawn, unknown, unknownBool);

    auto&& playerPawn_ = hookContext.make<PlayerPawn>(playerPawn);
    hookContext.make<ModelGlow>().updateInSceneObjectUpdater()(PlayerModelGlow{hookContext}, playerPawn_, EntityTypeInfo{});

    return originalReturnValue;
}

LINUX_ONLY([[gnu::aligned(8)]]) std::uint64_t Weapon_sceneObjectUpdater(cs2::C_CSWeaponBase* weapon, void* unknown, bool unknownBool) noexcept
{
    if (HookQuiesce::isShuttingDown())
        return 0;
    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    const auto originalReturnValue = RetAddrSpoofer::spoof(hookContext.featuresStates().visualFeaturesStates.modelGlowState.originalWeaponSceneObjectUpdater)(weapon, unknown, unknownBool);
    if (auto&& c4 = hookContext.make<BaseWeapon>(weapon).template cast<C4>())
        hookContext.make<ModelGlow>().updateInSceneObjectUpdater()(DroppedBombModelGlow{hookContext}, c4.baseWeapon(), EntityTypeInfo{});
    else
        hookContext.make<ModelGlow>().updateInSceneObjectUpdater()(WeaponModelGlow{hookContext}, hookContext.make<BaseWeapon>(weapon), hookContext.make<BaseWeapon>(weapon).baseEntity().classify());
    return originalReturnValue;
}

float ClientModeHook_getViewmodelFov(cs2::ClientModeCSNormal* clientMode) noexcept
{
    if (HookQuiesce::isShuttingDown())
        return 90.0f;
    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    const auto originalFov = RetAddrSpoofer::spoof(hookContext.hooks().originalGetViewmodelFov)(clientMode);
    if (auto&& viewmodelMod = hookContext.template make<ViewmodelMod>(); viewmodelMod.shouldModifyViewmodelFov())
        return viewmodelMod.viewmodelFov();
    return originalFov;
}





void ClientModeHook_onOverrideView(cs2::ClientModeCSNormal* thisptr, ViewSetup* viewSetup) noexcept
{
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;
    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    RetAddrSpoofer::spoof(hookContext.hooks().originalOverrideView)(thisptr, viewSetup);
    if (shuttingDown)
        return;

    hookContext.template make<ForceThirdPerson>().overrideView(viewSetup);
    hookContext.template make<Removals>().overrideView(viewSetup);
    // Freecam last: it fully owns the camera position while engaged.
    hookContext.template make<Freecam>().overrideView(viewSetup);
}




void scene_render_hooks::onParticlesDrawArray(void* particleObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    
    
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    HookQuiesce::InFlight flight;
    if (shuttingDown) {
        using ParticlesDrawArrayFn = void(*)(void*, void*, void*, unsigned, void*, void*, void*);
        RetAddrSpoofer::spoof(reinterpret_cast<ParticlesDrawArrayFn>(hooks.particlesDrawArray.original()))(particleObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
        return;
    }
    HookContext<GlobalContext> hookContext;
    hookContext.template make<WorldColors>().recolorParticles(primitives, static_cast<int>(primitiveCount));
    using ParticlesDrawArrayFn = void(*)(void*, void*, void*, unsigned, void*, void*, void*);
        RetAddrSpoofer::spoof(reinterpret_cast<ParticlesDrawArrayFn>(hooks.particlesDrawArray.original()))(particleObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
}




void scene_render_hooks::onProcessLights(void* lightBinner, void* sceneLightObject, void* unknown) noexcept
{
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    HookQuiesce::InFlight flight;
    if (shuttingDown) {
        using ProcessLightsFn = void(*)(void*, void*, void*);
        RetAddrSpoofer::spoof(reinterpret_cast<ProcessLightsFn>(hooks.lightBinnerProcessLights.original()))(lightBinner, sceneLightObject, unknown);
        return;
    }
    HookContext<GlobalContext> hookContext;
    hookContext.template make<WorldColors>().recolorLight(sceneLightObject);
    using ProcessLightsFn = void(*)(void*, void*, void*);
    RetAddrSpoofer::spoof(reinterpret_cast<ProcessLightsFn>(hooks.lightBinnerProcessLights.original()))(lightBinner, sceneLightObject, unknown);
}





void scene_render_hooks::onSkyBoxDrawArray(void* skyBoxObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    using SkyBoxDrawArrayFn = void(*)(void*, void*, void*, unsigned, void*, void*, void*);
    const auto original = reinterpret_cast<SkyBoxDrawArrayFn>(hooks.skyBoxDrawArray.original());
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    HookQuiesce::InFlight flight;

    if (shuttingDown) {
        RetAddrSpoofer::spoof(original)(skyBoxObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
        return;
    }

    HookContext<GlobalContext> hookContext;
    WorldColors<HookContext<GlobalContext>>::SavedSkyTint savedSkyTints[16]{};
    const auto savedCount = hookContext.template make<WorldColors>().recolorSky(savedSkyTints, 16, primitives, static_cast<int>(primitiveCount));
    
    
    RetAddrSpoofer::spoof(original)(skyBoxObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
    WorldColors<HookContext<GlobalContext>>::restoreSky(savedSkyTints, savedCount);
}

namespace
{





void worldDrawArrayBody(VTableSlotPatch& patch, void* sceneObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    using SceneDrawArrayFn = void(*)(void*, void*, void*, unsigned, void*, void*, void*);
    const auto original = reinterpret_cast<SceneDrawArrayFn>(patch.original());
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    HookQuiesce::InFlight flight;
    if (shuttingDown) {
        RetAddrSpoofer::spoof(original)(sceneObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
        return;
    }
    HookContext<GlobalContext> hookContext;
    hookContext.template make<WorldColors>().recolorWorld(primitives, static_cast<int>(primitiveCount));
    RetAddrSpoofer::spoof(original)(sceneObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
}

}

void scene_render_hooks::onSceneObjectDrawArray(void* sceneObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    worldDrawArrayBody(hooks.sceneObjectDrawArray, sceneObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
}

void scene_render_hooks::onAggregateSceneObjectDrawArray(void* sceneObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    worldDrawArrayBody(hooks.aggregateSceneObjectDrawArray, sceneObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
}


inline std::atomic<int> chamsCallsUntilSnapshot{0};






void chams_hook::onSceneObjectDrawArray(void* desc, void* renderContext, void* primitives, unsigned primitiveCount,
    void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    using SceneDrawArrayFn = void(*)(void*, void*, void*, unsigned, void*, void*, void*);
    const auto original = reinterpret_cast<SceneDrawArrayFn>(chams_hook::originalDrawArrayFor(desc));

    const bool shuttingDown = HookQuiesce::isShuttingDown();
    HookQuiesce::InFlight flight;
    if (shuttingDown || !original) {
        if (original)
            RetAddrSpoofer::spoof(original)(desc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
        return;
    }

    HookContext<GlobalContext> hookContext;
    const bool chamsEnabled = GET_CONFIG_VAR(chams_vars::Enabled);
    const bool worldModEnabled = false; // world recolor owns its own DrawArray hooks
    const bool hideSupported = Chams<HookContext<GlobalContext>>::hideModelsSupported();
    const bool hideEnemies = hideSupported && GET_CONFIG_VAR(chams_vars::HideEnemies);
    const bool hideLocal = hideSupported && GET_CONFIG_VAR(chams_vars::HideLocalPlayer);
    if (!chamsEnabled && !worldModEnabled && !hideEnemies && !hideLocal) {
        RetAddrSpoofer::spoof(original)(desc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
        return;
    }

    auto&& chams = hookContext.template make<Chams>();
    if (primitives && primitiveCount > 0) {
        const void* sceneObject = chams.sceneObjectOfFirstPrimitive(primitives);
        if (sceneObject) {
            const bool overlayTarget = chams.wantsOverlayPass(sceneObject);
            const bool targetIsEnemy = chams.overlayPassIsEnemy(sceneObject);

            // hide models: skipping the original call means the mesh never draws.
            if ((hideEnemies && overlayTarget && targetIsEnemy)
                || (hideLocal && chams.ownsLocalPawn(sceneObject)))
                return;

            if (chamsEnabled && overlayTarget && chams_materials::materialsReady()) {
                const auto materialType = static_cast<chams_materials::Type>(
                    static_cast<std::uint8_t>(GET_CONFIG_VAR(chams_vars::MaterialType)));
                auto* const visibleMaterial = chams_materials::materialFor(materialType, false);
                auto* const occludedMaterial = chams_materials::materialFor(materialType, true);
                const bool drawOccluded = GET_CONFIG_VAR(chams_vars::DrawOccluded);
                const auto visibleColor = Chams<HookContext<GlobalContext>>::primitiveColor(
                    targetIsEnemy ? GET_CONFIG_VAR(chams_vars::EnemyColor) : GET_CONFIG_VAR(chams_vars::TeamColor));
                const auto occludedColor = Chams<HookContext<GlobalContext>>::primitiveColor(
                    targetIsEnemy ? GET_CONFIG_VAR(chams_vars::EnemyOccludedColor) : GET_CONFIG_VAR(chams_vars::TeamOccludedColor));

                // material write happens at draw time: the game re-binds prim+0x20
                // to the entity's own material between GeneratePrimitives and here.
                if (visibleMaterial && (!drawOccluded || occludedMaterial)) {
                    if (drawOccluded) {
                        Chams<HookContext<GlobalContext>>::applyMaterialOverrideRange(primitives, primitiveCount,
                            occludedMaterial, occludedColor);
                        RetAddrSpoofer::spoof(original)(desc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
                        Chams<HookContext<GlobalContext>>::applyMaterialOverrideRange(primitives, primitiveCount,
                            visibleMaterial, visibleColor);
                        RetAddrSpoofer::spoof(original)(desc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
                    } else {
                        Chams<HookContext<GlobalContext>>::applyMaterialOverrideRange(primitives, primitiveCount,
                            visibleMaterial, visibleColor);
                        RetAddrSpoofer::spoof(original)(desc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
                    }
                    return;
                }
            }
        }
    }

    RetAddrSpoofer::spoof(original)(desc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
}

void chams_hook::onGeneratePrimitives(void* desc, void* sceneObject, void* sceneView, void* primitives) noexcept
{
    using GeneratePrimitivesFn = void (*)(void*, void*, void*, void*);
    const auto original = reinterpret_cast<GeneratePrimitivesFn>(chams_hook::originalFor(desc));

    const bool shuttingDown = HookQuiesce::isShuttingDown();
    HookQuiesce::InFlight flight;
    if (shuttingDown || !original) {
        if (original)
            RetAddrSpoofer::spoof(original)(desc, sceneObject, sceneView, primitives);
        return;
    }

    HookContext<GlobalContext> hookContext;

    
    const bool chamsEnabled = GET_CONFIG_VAR(chams_vars::Enabled);
    const bool viewmodelChamsEnabled = GET_CONFIG_VAR(chams_vars::ViewmodelEnabled);
    const bool ownWeaponsEnabled = GET_CONFIG_VAR(chams_vars::OwnWeaponsEnabled);
    const bool worldModEnabled = GET_CONFIG_VAR(WorldColorsWorldEnabled);
    const bool hideSupported = Chams<HookContext<GlobalContext>>::hideModelsSupported();
    const bool hideEnemies = hideSupported && GET_CONFIG_VAR(chams_vars::HideEnemies);
    const bool hideLocal = hideSupported && GET_CONFIG_VAR(chams_vars::HideLocalPlayer);
    if (!chamsEnabled && !viewmodelChamsEnabled && !ownWeaponsEnabled && !worldModEnabled && !hideEnemies && !hideLocal) {
        RetAddrSpoofer::spoof(original)(desc, sceneObject, sceneView, primitives);
        return;
    }

    auto&& chams = hookContext.template make<Chams>();

    
    // GeneratePrimitives can run on several render threads; the refresh is
    // single-writer + double-buffered inside Chams, this counter just paces it.
    if (chamsCallsUntilSnapshot.fetch_sub(1, std::memory_order_relaxed) <= 1) {
        chams.refreshLivePawnPointers();
        chamsCallsUntilSnapshot.store(Chams<HookContext<GlobalContext>>::kSnapshotRefreshInterval, std::memory_order_relaxed);
    }

    const auto previousCount = Chams<HookContext<GlobalContext>>::primitiveCount(primitives);
    RetAddrSpoofer::spoof(original)(desc, sceneObject, sceneView, primitives);
    // own viewmodel chams (hands + weapon), single pass, no occluded variant.
    // fail-closed: no materials = nothing draws.
    if (viewmodelChamsEnabled && chams.isPartOfViewmodel(sceneObject)) {
        if (chams_materials::materialsReady()) {
            const auto materialType = static_cast<chams_materials::Type>(
                static_cast<std::uint8_t>(GET_CONFIG_VAR(chams_vars::ViewmodelMaterialType)));
            if (auto* const viewmodelMaterial = chams_materials::materialFor(materialType, false)) {
                const auto end = Chams<HookContext<GlobalContext>>::primitiveCount(primitives);
                Chams<HookContext<GlobalContext>>::applyMaterialOverride(primitives, previousCount, end,
                    viewmodelMaterial, Chams<HookContext<GlobalContext>>::primitiveColor(GET_CONFIG_VAR(chams_vars::ViewmodelColor)));
                return;
            }
        }
        return;
    }

    // own weapons chams: local player's weapon models attached to the character
    // (third person / shadows). distinguished from the viewmodel by the
    // scene-object flag, from the body by the desc class (skeletal bodies
    // cannot take per-prim material overrides).
    if (ownWeaponsEnabled
        && !chams.isPartOfViewmodel(sceneObject)
        && chams.ownsLocalPawn(sceneObject)) {
        if (chams_materials::materialsReady()) {
            const auto materialType = static_cast<chams_materials::Type>(
                static_cast<std::uint8_t>(GET_CONFIG_VAR(chams_vars::OwnWeaponsMaterialType)));
            if (auto* const ownWeaponMaterial = chams_materials::materialFor(materialType, false)) {
                const auto end = Chams<HookContext<GlobalContext>>::primitiveCount(primitives);
                Chams<HookContext<GlobalContext>>::applyMaterialOverride(primitives, previousCount, end,
                    ownWeaponMaterial, Chams<HookContext<GlobalContext>>::primitiveColor(GET_CONFIG_VAR(chams_vars::OwnWeaponsColor)));
                return;
            }
        }
        return;
    }

    // enemy/team chams + hiding happen at draw time (onSceneObjectDrawArray):
    // the game re-binds primitive materials between generation and draw, so
    // writes here never reach the body draws. this hook keeps the viewmodel +
    // own-weapon passes and the world recolor.
    if (worldModEnabled && primitives && !chams.ownsLocalPawn(sceneObject))
        Chams<HookContext<GlobalContext>>::applyOverlay(primitives, previousCount,
            Chams<HookContext<GlobalContext>>::primitiveColor(GET_CONFIG_VAR(WorldColorsWorldColor)));
}


