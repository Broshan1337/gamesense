#pragma once

#include "GlobalContext/HookQuiesce.h"
#include "GlobalContext/GlobalContext.h"
#include "Hooks/PeepEventsHook.h"
#include "Hooks/Graphics/VulkanHook.h"
#include "hooks/vac_hook.h"
#include "Platform/SelfUnload.h"
#include "Utils/ReturnAddress.h"
#include <Utils/CrashLogger.h>
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
#include <Features/Combat/Aimbot/AimbotFovCircle.h>
#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/SpreadCircleVis/SpreadCircleVis.h>
#include <Features/Combat/LegitAimbot/LegitAimbot.h>
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
#include <Features/Hud/Watermark/Watermark.h>
#include <Features/Hud/CombatStats/CombatStats.h>
#include <Features/Hud/SpectatorList/SpectatorList.h>
#include <Features/Visuals/Hitmarker/Hitmarker.h>
#include <Features/Visuals/PlayerList/PlayerList.h>
#include <Features/Visuals/Removals/Removals.h>
#include <Features/Visuals/ThirdPerson/ForceThirdPerson.h>
#include <Features/Visuals/WorldColors/WorldColors.h>
#include <Features/Lua/LuaManager.h>
#include <Features/Lua/LuaConfigBridge.h>
#include <Hooks/SceneRenderHooks.h>
#include <Hooks/ChamsHook.h>
#include <Features/Visuals/Chams/Chams.h>

[[NOINLINE]] void finishInit(auto& hookContext)
{
    hookContext.entityClassifier().init(hookContext);
    hookContext.config().init();
    hookContext.config().scheduleLoad();

    // Lua scripting framework: creates ~/OsirisCS2/scripts and prepares the manager. Scripts
    // are loaded on demand from the menu tab, not automatically here.
    lua::init();
    lua::menuOpenQuery = []() noexcept { return GUI::isMenuOpen(); };
    // imgui.* dispatch gate: script windows render only inside a live ImGui frame (the menu
    // render calls dispatchMenuWindows on the present thread). Null in unit tests -> the menu
    // dispatch no-ops even with "menu" callbacks registered.
    lua::imguiContextQuery = []() noexcept { return ImGui::GetCurrentContext() != nullptr; };

    // Entity/schema bridges for the Lua entity.* API. Each query builds its own HookContext per
    // call behind the same guards as ui_config::withContext, so the Lua core never links the
    // pattern/schema machinery and the unit tests run with all four pointers null.
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
    // renderer.load_image bridge into the Vulkan texture pool (present-thread only, like every
    // other texture the menu stages).
    lua::luaTextureRequest = &VulkanHook::lua_texture::request;
    lua::luaTextureQuery = &VulkanHook::lua_texture::query;
    lua::luaTextureRelease = &VulkanHook::lua_texture::release;
    // entity.get_origin: the game's own GetAbsOrigin through the game scene node (the schema's
    // m_vecAbsOrigin lives on CGameSceneNode behind a pointer, unreachable from lua get_prop).
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
    // entity.get_all: resolve the class ONCE through the game's own class map (exact string
    // match - schema names, e.g. "C_PlantedC4"), then walk the networkable identities. -1
    // answers "unknown class" so the binding can surface a script error instead of an empty list.
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
    // entity.get_spectators: controllers whose pawn is ACTIVELY spectating (observer mode != 0)
    // the given pawn. The pawn -> m_pObserverServices -> m_hObserverTarget chain lives behind a
    // schema-resolved POINTER field (untouchable from lua get_prop), so the walk happens here.
    // Offsets resolve per call from the runtime schema - no RVAs to go stale on an update.
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
                return; // observer services exist but the pawn is not spectating anything
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
    // Console-command bridge for Lua client.exec - the same EngineCommandExecutor every feature
    // uses (queued into the engine's command buffer, drained next frame; LOCAL client console).
    lua::engineCommandQuery = [](const char* command) noexcept {
        if (!command || !HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
            return;
        HookContext<GlobalContext> context;
        context.template make<EngineCommandExecutor>().execute(command);
    };
    // API v2 bridges: runtime convar reads/writes and the world-to-screen projection. Same
    // guard pattern as above - null-safe "unavailable" results while the context is missing.
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
    // World -> normalized device coordinates through the frame's worldToProjection matrix
    // (same WorldToClipSpaceConverter the in-world panel features draw with). The NDC -> pixel
    // conversion happens in the binding, where the ImGui display size lives.
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
    // entity.get_class: most-derived schema class name through the game's own entity-class
    // map (reverse lookup in EntitySystem - no new RE). Copied out before returning; null =
    // stale index / context unavailable.
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
    // Native config bridges for Lua config.* - each call builds its own HookContext and walks
    // the same ConfigSchema the .cfg save/load uses (see LuaConfigBridge.h). Same guard
    // pattern as every bridge above.
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

    // ImGui menu: build the context (allocations bridged to CS2's IMemAlloc) and attempt the
    // Vulkan presentation hook. The hook legitimately fails while libvulkan is not mapped yet
    // (early startup) - ViewRenderHook_onRenderStart retries it every frame until it sticks.
    // NOTE: the PeepEvents hook deliberately STAYS enabled from here on (it used to disable
    // itself after serving as the init trigger) - it is the menu's input feed now.
    honey::keepAlive(); // honey decodes: keeps the decoy bodies/strings in the binary (returns immediately)
    if (GUI::init())
        (void)VulkanHook::tryInstall(); // false = libvulkan not mapped yet; retried per frame below

    // 2026-09-26 crash isolation: per-hook env switches (Steam launch options, e.g.
    // "NS_DISABLE_GEM_HOOK=1 %command%"). NS_DISABLE_VTABLE_HOOKS=1 = all three at once.
    // ViewRender stays (it carries the config pipeline + the ESP panel path). The
    // 09-26/27 crash families: (a) the original tier0-free signature - returned when the
    // CSGOInput hook came back on, (b) the engine2 #GP on the NaN-poisoned global - fixed
    // with the offset-pattern audit. The GEM hook is the prime remaining suspect: its
    // hooked object's identity and the clone-length math are unverified on this build.
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
    // CHEAT O METER voice receive tap: VMT hook on the CNetworkMessages ReadFromBuffer slot -
    // pass-through unless the analyzer's VoiceProbe toggle stages packets for the [vtap] probe.
    (void)voice_tap::install();
    // Both ClientMode slots (OverrideView for third person / view punch removal, GetViewmodelFov)
    // ride one VmtSwapper; the hook bodies self-gate on their config vars.
    hookContext.template make<ClientModeHooks>().hookClientMode();
    // Scene-render pass-through hooks (particle recolor, light recolor) + the patch anchors for
    // the Removals toggles. Fails closed per anchor with a StatusReport entry.
    (void)scene_render_hooks::install(); // per-anchor results already landed in StatusReport
    // Enemy chams: GeneratePrimitives vtable patches (pass-through until chams_vars::Enabled).
    (void)chams_hook::install();
    // Datagram-level net-lag experiment (own servers only): GOT hook over the sendto/sendmsg
    // imports of libsteamnetworkingsockets.so. Pass-through until net_lag_vars::Enabled.
    static_cast<void>(netlag_hook::install());
}

int SDLHook_PeepEvents(void* events, int numevents, int action, unsigned minType, unsigned maxType) noexcept
{
    // Shutdown guard FIRST: past this point the context can be mid-destroy, and a re-init from
    // here would resurrect it behind the teardown thread's back. This hook only exists between
    // early init and finishInit()'s disable() - returning "no events processed" is harmless.
    if (HookQuiesce::isShuttingDown())
        return 0;

    HookQuiesce::InFlight flight;
    const auto initInProgress = !HookContext<GlobalContext>::isGlobalContextComplete();
    if (initInProgress) {
        // SESSION BIND FIRST (hardening 2026-09-24): the pattern vault's runtime lock must be
        // armed from the injection trailer BEFORE the pools unseal for their one and only
        // scan. Fail-closed: an unverified module releases itself and the pools never decrypt.
        session_bind::verifyOnce([] { GUI::requestUnload(); });
        HookContext<GlobalContext>::initCompleteGlobalContextFromGameThread();
    }

    HookContext<GlobalContext> hookContext;

    if (initInProgress)
        finishInit(hookContext);

    const int processed = hookContext.hooks().peepEventsHook.original(events, numevents, action, minType, maxType);

    // Menu input: only GET polls carry events the game will act on. Feed the fetched batch to
    // ImGui (which queues it for the present thread) and swallow the whole batch while the menu
    // is open, so the game never sees keyboard/mouse input that went into the menu. The menu
    // toggle itself (INSERT / ALT+I) is handled inside polledEvents before the swallow decision.
    if (action == SDL_GETEVENT && processed > 0
        && GUI::polledEvents(static_cast<const SDL_Event*>(events), processed))
        return 0;

    return processed;
}

[[NOINLINE]] void unload(auto& hookContext) noexcept
{
    // Lua states first: they own timers, child processes (http) and Lua heaps that must all be
    // gone before any of the feature/hook teardown below runs.
    lua::unloadAll();

    hookContext.template make<BombTimer>().onUnload();
    hookContext.template make<DefusingAlert>().onUnload();
    hookContext.template make<PostRoundTimer>().onUnload();
    hookContext.template make<OutlineGlow>().onUnload();
    hookContext.template make<BombStatusPanel>().onUnload();
    hookContext.template make<InWorldPanels>().onUnload();
    hookContext.template make<Watermark>().onUnload();
    hookContext.template make<Blockbot>().onUnload();
    hookContext.template make<Bunnyhop>().onUnload();
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
    // Remove this session's integrity-baseline report (loader watchdog liveness signal).
    {
        char path[192];
        if (ns_paths::joinFormat(path, sizeof(path), "ns_module_integrity", "_%d",
                LinuxPlatformApi::processId()))
            ::unlink(path);
    }
    hookContext.template make<userinfo_flood::UserInfoFlood>().onUnload();
    hookContext.template make<server_lagger::ServerLagger>().onUnload();
    // Feature-owned code patches (legs render skip, post-hud layer skip) and the scene-render
    // pass-through hooks must be restored while the context is still alive.
    hookContext.template make<Removals>().onUnload();
    scene_render_hooks::uninstall();
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

// Real per-frame-stage hook (CSource2Client::OnFrameStageNotify, vtable slot 36 - see
// Hooks/Source2ClientHook.h and CS2/Classes/CSource2Client.h for the full RE trail).
// Infrastructure only - no feature logic hangs off it right now.
//
// It USED to also call SkinChanger::run() at frameStage==6 (FramePostDataUpdate), added purely
// to test whether the knife holding-animation bug was a frame-timing issue - the theory being
// that running the subclass swap only at render-start was too late for the animation graph.
// That theory is now conclusively disproven: the real cause was that updateSubclass() skips
// ResolveSubclassData behind an entity flag, so m_pSubclassVData never became the target knife's
// (see BaseWeapon::resolveSubclassData() and project notes). Frame timing was never involved.
//
// The call was removed rather than left in place because it was not free: SkinChanger::run()
// was running TWICE per frame, once here and once in ViewRenderHook_onRenderStart below, doing
// the whole per-weapon apply pass both times. That is pure duplicated work, and it doubled the
// rate at which this feature queues asynchronous composite-material jobs - which is the exact
// subsystem behind this project's recurring entity-teardown crash. ViewRenderHook is kept as the
// single call site because it is the long-standing one, present in every historical test
// including the one that confirmed the fix.
void Source2ClientHook_onFrameStageNotify(cs2::CSource2Client* thisptr, int frameStage) noexcept
{
    // CHAINDIAG: FSN heartbeat (throttled) - alive means the client hook chain works.
    {
        static std::uint32_t fsnCalls = 0;
        static std::int64_t lastLogNs = 0;
        ++fsnCalls;
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const std::int64_t nowNs = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
        if (lastLogNs == 0 || nowNs - lastLogNs > 10'000'000'000LL) {
            lastLogNs = nowNs;
            gui_log::write("[chaindiag] fsn alive, calls=%u lastStage=%d", fsnCalls, frameStage);
        }
    }
    // Quiesce pattern shared by all hook entry points: during teardown we still deliver the
    // original (a missed frame stage glitches the client) while skipping ALL feature logic.
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    hookContext.hooks().source2ClientHook.getOriginalOnFrameStageNotify()(thisptr, frameStage);
    if (shuttingDown)
        return;

    // Lag-comp bone-record capture runs HERE (frame stage 6, FramePostDataUpdate - velocity-cs2's
    // exact timing), NOT inside CreateMove: setup() swaps globalvars tick/simtime and forces
    // skeleton regeneration through the game's own mask setters, which must not happen while the
    // client's CreateMove/prediction flow (spread math etc.) is mid-flight. Running it from
    // CreateMove is the prime suspect of the 2026-08-23 23:56 SEGV.
    if (frameStage == 6 && (GET_CONFIG_VAR(aimbot_vars::Backtrack) || GET_CONFIG_VAR(aimbot_vars::Extrapolate)))
        hookContext.template make<Lagcomp>().run();

    // Inventory changer (config-skin sync + pending adds): CREATEMOVE ONLY (session 8's rule).
    // The FrameStageNotify menu tick was tried in session 11 and REVERTED 2026-09-06: injecting
    // into the main menu crashed (libclient+0x1f941d7, [null+0x10], the session view-id walk)
    // because our item/view mutation on the game thread still races the game's OWN view
    // enumeration that runs on another thread while the menu is up. CreateMove only ticks
    // in-game, which is where this feature matters anyway - do not re-add a menu tick without
    // solving that enumeration race first.

    // Chat tools also tick here so their cadences (HUD color cycle in the lobby) run in the
    // main menu. Breadcrumb-proven clean during the 2026-09-06 inject crashes (0x310->0x311
    // completed on every run); it touches no GC/session state. One caller of the shared parity
    // gate drives the cadence per frame - see ChatTools.h.
    if (frameStage == 6)
        hookContext.template make<ChatTools>().run();

    // Radio console work (auto voice-key bind/unbind, voice_modenable): the ONLY place this
    // feature touches the engine command buffer. Queued on the present thread by the broadcast
    // updates, drained HERE (game thread, ticks in menus too) because the buffer is not
    // thread-safe - a present-thread execute crashed a live session (2026-09-11). Drain is
    // last-wins idempotent (bind/unbind/voice_modenable), so a missed frame only delays.
    if (frameStage == 6)
        hookContext.template make<RadioManager>().runFrameStageNotify();
}

// Numeric + string event fields for the Lua callbacks' `event` table (see lua::dispatchEvent).
// Strings go through the verified GetString slot (GameEventFields.h slot 11); the value is
// copied into the EventArg at build time because the engine may recycle the event object
// after the original FireEventClientSide runs.
// "userid"/"attacker" come out of entityForKey as 0-BASED PLAYER SLOTS (65535 = nobody) - see
// localPlayerIsAttacker in GameEventFields.h for the full encoding trail.
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

// Real hook on IGameEventManager2::FireEventClientSide (CGameEventManager vtable slot 9 - see
// Hooks/GameEventManagerHook.h and CS2/Classes/IGameEventManager2.h for the full RE trail).
// Infrastructure only for now - no per-event feature logic wired in yet (hitmarkers, hitsounds,
// killfeed icon overrides, etc. are all meant to hang off this same hook later, each reading
// event->GetName() and dispatching on it, matching the reference's own OnFireEventClientSide
// dispatcher pattern).
bool GameEventManagerHook_onFireEventClientSide(cs2::IGameEventManager2* thisptr, cs2::IGameEvent* event) noexcept
{
    // CHAINDIAG: game-event heartbeat (hitmarker/hitsound/WorldColors all gate off this).
    {
        static std::uint32_t gemEvents = 0;
        static std::int64_t lastLogNs = 0;
        ++gemEvents;
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const std::int64_t nowNs = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
        if (lastLogNs == 0 || nowNs - lastLogNs > 10'000'000'000LL) {
            lastLogNs = nowNs;
            gui_log::write("[chaindiag] gem alive, events=%u", gemEvents);
        }
    }
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return true;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;

    if (shuttingDown)
        return hookContext.hooks().gameEventManagerHook.getOriginalFireEventClientSide()(thisptr, event);

    // Run our own handling BEFORE the original: the original dispatches the event to every
    // registered listener, and some of them are free to invalidate or recycle the event object
    // afterwards. Reading it first avoids depending on it surviving that.
    hookContext.template make<HitSound>().onFireEventClientSide(event);
    hookContext.template make<Hitmarker>().onFireEventClientSide(event);
    hookContext.template make<WorldColors>().onFireEventClientSide(event);
    hookContext.template make<HitLog>().onFireEventClientSide(event);
    hookContext.template make<TeamDamageTracker>().onFireEventClientSide(event);
    hookContext.template make<PlayerAnalyzer>().onFireEventClientSide(event);
    hookContext.template make<VoteRevealer>().onFireEventClientSide(event);
    hookContext.template make<SpawnProtectionSound>().onFireEventClientSide(event);
    hookContext.template make<CombatStats>().onFireEventClientSide(event);
    hookContext.template make<Killsay>().onFireEventClientSide(event);
    // Airhorn trigger decisions run on the game thread (event data), playback on the present
    // thread (updateAirhorn) - the pending-trigger atomic bridges the two.
    // Sound-board event triggers (First Blood / Headshot / Round Win) replaced the old
    // airhorn trigger logic that lived in RadioManager.onGameEvent.
    hookContext.template make<soundboard::SoundBoard>().onGameEvent(event);
    hookContext.template make<ChatTools>().onFireEventClientSide(event);
    // Lua scripts: dispatch under the event's own name (player_hurt etc.), with the common
    // numeric fields passed along as the callbacks' `event` table. Everything is read through
    // the event vtable BEFORE the original can recycle the event object.
    const char* const eventName = game_events::name(event);
    // Map-name capture for client.get_map_name: game_newmap carries the map as a string field
    // (read through the freshly verified GetString slot, see game_events::stringForKey). Kept
    // until the next game_newmap; the Lua side guards on having a local pawn, so a stale name
    // in the main menu is inert.
    if (game_events::is(event, "game_newmap")) {
        if (const char* mapName = game_events::stringForKey(event, "mapname"); mapName && mapName[0] != '\0')
            lua::setCurrentMapName(mapName);
    }
    lua::EventArg luaArgs[16];
    const int luaArgCount = buildLuaEventArgs(luaArgs, eventName, event);
    lua::dispatchEvent(eventName, luaArgs, luaArgCount);

    return hookContext.hooks().gameEventManagerHook.getOriginalFireEventClientSide()(thisptr, event);
}

// Real hook on CCSGOInput::CreateMove (vtable slot 26 - see CS2/Classes/CCSGOInput.h for the RE
// trail). This is the movement-input path, and the first hook in this project that is not on the
// render or event path.
//
// Ordering matters here in a way it did not for the event hook: the original is what FILLS IN the
// user command from the player's actual input, so anything that wants to steer movement has to run
// AFTER it and edit the finished command. Running first would just have the original overwrite it.
void CSGOInputHook_onCreateMove(cs2::CCSGOInput* thisptr, int slot, cs2::CUserCmd* cmd) noexcept
{
    // CHAINDIAG: is the input chain alive in-match? (2026-10-04: every CreateMove-gated
    // feature - triggerbot/bhop/strafer - dead with zero errors; this tells alive from dead.)
    {
        static std::uint32_t createMoveCalls = 0;
        static std::int64_t lastLogNs = 0;
        ++createMoveCalls;
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const std::int64_t nowNs = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
        if (lastLogNs == 0 || nowNs - lastLogNs > 10'000'000'000LL) {
            lastLogNs = nowNs;
            gui_log::write("[chaindiag] createmove alive, calls=%u slot=%d", createMoveCalls, slot);
        }
    }
    // 0x370 entry / 0x371 normal tail (2026-09-26 22:15 crash: the game destroyed an
    // input-system object whose per-slot command ring held a dead-stack pointer; these
    // breadcrumbs prove which input path ran last, see hud_root_walk 0x376/0x377 for the
    // ESP panel path)
    CrashLogger::trace(0x370);
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    hookContext.hooks().csgoInputHook.getOriginalCreateMove()(thisptr, slot, cmd);
    if (shuttingDown)
        return;

    // The panic toggle is polled FIRST so engaging it can also disarm the features with
    // cross-command state, and so a panicked state can always be released again (the poll itself
    // never gets gated).
    auto&& panicKey = hookContext.template make<PanicKey>();
    panicKey.run();
    // Net-lag poll runs BEFORE the panic gate so its hold-to-choke bind and config stay live
    // even while combat features are panicked (it is a network feature, not a combat one).
    hookContext.template make<NetLag>().run();
    // Name animator: cosmetic live renames (setinfo path) - like visuals, it keeps running.
    hookContext.template make<NameAnimator>().run();
    if (panicKey.isActive())
        return;

    // velocity-cs2's desubtick, at the reference's exact position: the command's subtick steps
    // (the game's quantized mouse events, one per mouse sample) are wiped before any feature
    // writes its own, so the shared 32-slot timeline belongs to the features. This MUST run here
    // and not inside a feature: the bunnyhop below runs first and its landing taps are already
    // on the timeline by the time anything else executes - a clear placed any later erases them.
    hookContext.template make<SubtickMoves>().clear(UserCmd{cmd}.baseMessage());

    // mytest's isvalveds_check: idempotent per-tick local spoof of the "Valve official server"
    // flag, dump-anchored (see cs2::C_CSGameRules::kIsValveDsOffset).
    hookContext.template make<IsValveDsSpoof>().run();

    hookContext.template make<Blockbot>().onCreateMove(cmd);
    // SuperToss: while a grenade throw is mid-flight, capture the corrected throw angles
    // (friend-source port). Its write goes out at WriteMoveCrc below. See SuperToss.h.
    hookContext.template make<SuperToss>().onCreateMove(cmd);
    // Last-tick defuse/plant: while the bind is held, press USE under a bomb about to blow and
    // ATTACK+USE for the end-of-round plant. Its button writes go out at WriteMoveCrc. See
    // LastTickDefuse.h.
    hookContext.template make<LastTickDefuse>().onCreateMove(cmd);
    // The movement suite (edgejump/edgestop/slowwalk/fastladder/jumpbug) runs BEFORE the bunnyhop,
    // matching the friend-source order: the jumpbug's per-tick coordination flag (jumpBugActive)
    // must be fresh when the bunnyhop decides whether its landing predictor engages. See Movement.h.
    hookContext.template make<Movement>().onCreateMove(cmd);
    // The bunnyhop: its frame-perfect landing pair (release + press at the predicted touchdown
    // fraction) has to be on the command's subtick timeline before the quantized strafer
    // reads that same timeline - the strafer then schedules its yaw deltas AFTER the jump press
    // instead of colliding with it. Stands down while the jumpbug bracket is armed.
    hookContext.template make<Bunnyhop>().onCreateMove(cmd);
    // The quantized strafer simulates air acceleration across up to 16 sub-frames of the remaining
    // tick and emits one yaw_delta step per sub-frame. See TestStrafer.h.
    hookContext.template make<TestStrafer>().onCreateMove(cmd);
    // Silent aimbot: CreateMove runs after the original, so the command's input_history is populated
    // here (it is empty at WriteMoveCrc). The aimbot redirects the firing shot's history entry angles
    // onto the target without touching the rendered view. See Aimbot.h.
    hookContext.template make<Aimbot>().onCreateMove(cmd);
    // Legit aim assist: moves the REAL view angles smoothly toward the target while its aim key is held
    // (the Legit-tab counterpart to the silent aimbot above). See LegitAimbot.h.
    hookContext.template make<LegitAimbot>().onCreateMove(cmd);
    // Standalone recoil control: during a spray, pulls the real view against the recoil kick. See Rcs.h.
    hookContext.template make<Rcs>().onCreateMove(cmd);
    // skeet's quick peek: anchors the standstill position and counter-drives the player back onto
    // it (movement buttons + a subtick analog step) while they are peeked out. Runs after combat
    // like the reference's pipeline. See AutoPeek.h.
    hookContext.template make<AutoPeek>().onCreateMove(cmd);
    // FVA-style view-angle chains: runs LAST so the history entries interpolate towards the FINAL
    // angles every feature above settled on. See Features/Game/FvaEmulator.h.
    hookContext.template make<FvaEmulator>().onCreateMove(cmd);

    // (The InventoryChanger was REMOVED 2026-09-06: its GC session-view mutation kept racing
    // the game's own enumeration - menu + map-load crashes. The skin changer needs none of it;
    // the agent model swap (AgentChanger, below) stays - it touches no GC state.)

    // Local agent model changer - same thread rule (SetModel swaps the live pawn's model).
    hookContext.template make<AgentChanger>().run();

    // skeet's reveal radar: set the client-side spotted flag on every alive enemy pawn (game
    // thread, same rule as the changer above). See RevealRadar.h.
    hookContext.template make<RevealRadar>().run();

    // Spectate enemies while dead: watches the local observer target each tick (game thread,
    // CreateMove) and, when the game's own spec_next/spec_prev writes a teammate into it, advances
    // to the next/prev alive enemy instead - the mp_forcecamera candidate filter never sees the
    // write. See SpectateEnemies.h.
    hookContext.template make<SpectateEnemies>().run();

    // Chat tools (fake name / chat spam / radio spam / HUD color cycle) - networked console
    // commands, game thread. Also ticks from FrameStageNotify 6 so the cadences run in the
    // main menu; the shared parity gate keeps the rate exact with both callers live.
    hookContext.template make<ChatTools>().run();

    // Multi-field userinfo flood (setinfo churn over the selected userinfo cvars - every packet
    // makes the server validate all carried fields, and the userinfo-table row rewrite + delta
    // broadcast it drives is the CS2 stringtable churn). Game thread, CreateMove only - the
    // engine command buffer is not thread-safe, and userinfo only matters in a live match.
    hookContext.template make<userinfo_flood::UserInfoFlood>().run();

    // Server lagger: voice-datagram flood through the game's own net channel (the friend-source
    // profile port - message factory builds genuine CCLCMsg_VoiceData, engine frames/encrypts,
    // the server relays to every player). Game thread, CreateMove only; per-tick guarded, and
    // it stands down when no net session exists.
    hookContext.template make<server_lagger::ServerLagger>().run();

    // Sound board: wav clips -> opus (the game's own libsoundsystem) -> paced CCLCMsg_VoiceData
    // through the net channel (the lagger's pipeline at voice cadence). Game thread, CreateMove;
    // stands down silently when not connected.
    hookContext.template make<soundboard::SoundBoard>().run();

    // Lua scripts: one "createmove" callback batch per input tick, after all native features.
    // The tick's own command goes along so the cmd.* bindings can read/steer it (game-thread
    // lifetime only - the pointer is dead the moment this returns).
    CrashLogger::trace(0x371);
    lua::dispatchTick(cmd);
}

// Hook on CCSGOInput slot 6 - the function that builds the command from the queued input samples.
//
// This one runs BEFORE the original, which is the opposite of the CreateMove hook above and is the
// entire point: slot 6 is what turns input into movement, so steering means handing it different
// input, not correcting its output afterwards. Everything downstream - the movement members, the
// protobuf fields, the per-subtick analog deltas - is then produced by the game itself.
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
        hookContext.template make<Bunnyhop>().onBuildUserCmd(thisptr, slot);
        hookContext.template make<Movement>().onBuildUserCmd(thisptr, slot);
    }
    CrashLogger::trace(0x373);
    return hookContext.hooks().csgoInputHook.getOriginalBuildUserCmd()(thisptr, slot, frameNumber);
}

// Hook on CCSGOInput slot 7, which copies the command's three button words into `buttons_pb` and
// then checksums them into `move_crc`.
//
// Runs BEFORE the original so a pressed button is picked up by both. This is where buttons have to
// be set: they live on the command, and slot 7 is the only point outside CreateMove that hands us
// one - slot 6 obtains its command by calling sub_15DAC30 rather than from any global.
std::uint64_t CSGOInputHook_onWriteMoveCrc(cs2::CCSGOInput* thisptr, cs2::CUserCmd* cmd) noexcept
{
    CrashLogger::trace(0x374);
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return 0;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    if (!shuttingDown && !hookContext.template make<PanicKey>().isActive()) {
    hookContext.template make<Blockbot>().onWriteMoveCrc(cmd);
    // SuperToss + last-tick defuse/plant write phase: corrected throw angles (with the movement
    // fix re-projection) and the USE/ATTACK+USE button presses staged at CreateMove.
    hookContext.template make<SuperToss>().onWriteMoveCrc(cmd);
    hookContext.template make<LastTickDefuse>().onWriteMoveCrc(cmd);
    hookContext.template make<Bunnyhop>().onWriteMoveCrc(cmd);
    // The movement suite writes its buttons/view angles/subtick brackets behind the bunnyhop's
    // (both share the slot-7 write position; the staged flags never overlap on one tick).
    hookContext.template make<Movement>().onWriteMoveCrc(cmd);
    // The quantized strafer's write phase: consumes the CreateMove capture and emits the
    // view-anchored yaw_delta sweep. Runs after Bunnyhop/Movement so maxWhen() accounts for
    // their landing taps / jumpbug bracket before placing steps.
    hookContext.template make<TestStrafer>().onWriteMoveCrc(cmd);
        // FVA silent-shots experiment position: with FvaSilentShots ON the chains publish HERE
        // so the shot writer below sees them and takes the per-entry redirect (shot claimed at
        // the aim angle through input_history). OFF = no-op here, publish happens at the tail.
        hookContext.template make<FvaEmulator>().onWriteMoveCrcEarly(cmd);
        // Same hook, same command object as the bunnyhop above. The triggerbot splices an IN_ATTACK attack
        // into the command itself - banks + buttons_pb plus a subtick press/release pair - so each firing
        // tick carries a complete press and release. This is the command that actually lands (the ring
        // command did not).
        hookContext.template make<Triggerbot>().onWriteMoveCrc(cmd);
        // Rage force-shot counterpart: the aimbot staged its fire decision during CreateMove; the splice
        // happens HERE because slot 6 rebuilds buttons_pb/subtick_moves between the two hooks. See
        // Aimbot::onWriteMoveCrc / AttackCommand.h.
        hookContext.template make<Aimbot>().onWriteMoveCrc(cmd);
        // FVA chain publisher - DEFAULT position: chains were staged at CreateMove; THIS is where
        // they reach the wire (slot 6 rebuilds input_history after CreateMove). Deliberately LAST
        // in this hook: every feature above must see the field exactly as slot 6 left it
        // (SubtickShotWriter self-selects its shot path from whether history is present - publishing
        // before it flipped that decision and changed shot behavior mid-spray). In the
        // FvaSilentShots experiment this call is a no-op and the early position above published.
        hookContext.template make<FvaEmulator>().onWriteMoveCrcLate(cmd);

        // skeet's desubtick end-stage ("final subtick"): last writer position - strip the analog
        // movement components from every subtick step so the SERVER receives no subtick movement.
        // Button/angle steps (shots, jumps, strafer steering) survive. See SubtickMoves::stripAnalog.
        if (GET_CONFIG_VAR(movement_vars::Desubtick))
            SubtickMoves<HookContext<GlobalContext>>::stripAnalog(UserCmd{cmd}.baseMessage());
    }
    CrashLogger::trace(0x375);
    return hookContext.hooks().csgoInputHook.getOriginalWriteMoveCrc()(thisptr, cmd);
}

void ViewRenderHook_onRenderStart(cs2::CViewRender* thisptr) noexcept
{
    // This thread owns the teardown (see the unloadFlag block below), so it does not participate
    // in the InFlight counting - it DRAINS it instead.
    if (HookQuiesce::isShuttingDown() && !HookContext<GlobalContext>::isGlobalContextComplete())
        return;

    HookContext<GlobalContext> hookContext;
    hookContext.clearRenderHookState();
    hookContext.hooks().viewRenderHook.getOriginalOnRenderStart()(thisptr);


    // The presentation hook is installed lazily: libvulkan (and the renderer's pointer cache)
    // may not exist yet during early init, so every render start retries until it sticks. Once
    // installed this is an atomic load and returns immediately.
    (void)VulkanHook::tryInstall(); // cheap no-op once installed; retries until the renderer cache exists

    // First rendered frame after injection, and only that one - see WelcomeSound.h for why it waits
    // for a frame instead of announcing itself from finishInit().
    hookContext.template make<WelcomeSound>().run();

    // Real Skin Changer feature: applies the user's persisted per-weapon skin selections to
    // every weapon the local player owns (see SkinChanger.h for the inventory-changer
    // semantics and SkinChangerState.h for why this doesn't call into the (real,
    // composite-material-rebuilding) regenerate function every single frame). Gated on the same
    // map-time threshold as the client-scope resolver, so the two cannot drift apart.
    //
    // This replaces an earlier crude always-on test hook that unconditionally wrote one
    // hardcoded skin onto whatever the active weapon happened to be. setSkin() itself has
    // gone through two real fixes since: first a per-entity attribute-buffer pool (to stop a
    // shared buffer being stomped on across weapon changes), then a full switch to the game's
    // own internal SetAttributeValueByName mechanism once a symbol-confirmed crash showed the
    // raw-write approach was skipping real bookkeeping the proper path performs - see
    // BaseWeapon::setSkin() and project notes for the full trail.
    //
    // The gate below was 15 seconds until the offset caches were made to re-resolve instead of
    // keeping a failed first attempt forever (HookContext::resolveOffsets). That stickiness was the
    // real reason for the wait: touching the offsets too early cached them all as 0 for the whole
    // session, so the gate had to guarantee the first touch was late enough to be correct. With
    // that fixed the gate only has to mean "the client is actually running".
    // CLASSIFIER RETRY (the 2026-10-03 silent-death fix): EntityClassifier::init runs once
    // at module init - in the MAIN MENU, where the entity-system global is still null - so
    // every class lookup failed and ESP/glow/knife-skins stayed dead even after joining a
    // map. Retry at the top of the feature layer until it fills (the check = one array
    // read; the re-init = a full idempotent rebuild).
    if (!hookContext.entityClassifier().initialized())
        hookContext.entityClassifier().init(hookContext);

    if (const auto mapTime = hookContext.globalVars().curtime(); mapTime.hasValue() && mapTime.value() >= schema_readiness::kMinMapTime)
        hookContext.template make<SkinChanger>().run();

    hookContext.make<InWorldPanels>().updateState();
    SoundWatcher<decltype(hookContext)> soundWatcher{hookContext.soundWatcherState(), hookContext};
    soundWatcher.update();
    SoundFeatures{hookContext.soundWatcherState(), hookContext.hooks().viewRenderHook, hookContext}.runOnViewMatrixUpdate();
    hookContext.template make<Hitmarker>().run();
    hookContext.template make<PlayerList>().run();
    // CHEAT O METER sampling: samples the selected players' networked eye angles + publishes
    // the HUD snapshot. Runs BEFORE the pawn panel loop so the ESP tags read fresh scores.
    hookContext.template make<PlayerAnalyzer>().run();
    // voice tap probe drain (game thread - the hook only stages on the network thread)
    voice_tap::drainProbe(hookContext);
    hookContext.template make<WorldColors>().run();
    // Viewmodel position: forces the viewmodel_offset_x/y/z cvars while enabled.
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
    hookContext.make<RenderingHookEntityLoop>().run();
    hookContext.make<GlowSceneObjects>().removeUnreferencedObjects();
    hookContext.make<DefusingAlert>().run();
    hookContext.make<KillfeedPreserver>().run();
    hookContext.make<BombStatusPanelManager>().run();
    hookContext.make<InWorldPanels>().hideUnusedPanels();
    hookContext.template make<Watermark>().run();
    hookContext.template make<CombatStats>().run();
    hookContext.template make<HudThemeColor>().run();
    hookContext.template make<SpectatorList>().run();

    // Init health report: features that validated runtime prerequisites during init recorded
    // [status] entries; dump them once on the first rendered frame (engine console is ready by
    // now - WelcomeSound's rationale, same pattern).
    StatusReport::dumpOnce([](const char* message, bool ok) {
        // Mirror into the gui log: the engine console is ephemeral, but the gui log survives
        // the session on the host - the only way to read the init health report after a crash.
        gui_log::write("[status] %s %s", ok ? "OK" : "FAIL-CLOSED", message);
        VerifyConsole::write(0.0f, "status", "%s %s", ok ? "OK" : "FAIL-CLOSED", message);
    });

    UnloadFlag unloadFlag;
    // The ImGui menu's Unload button sets a present-thread flag; the teardown itself stays on
    // this thread like every other unload path. The LOADER's unload button cannot use dlopen/
    // dlclose anymore (the VAC hardening unlinked our link_map node, so RTLD_NOLOAD finds
    // nothing) - it instead writes <exchangeRoot>/ns_unload_request (NsPaths.h), which is
    // consumed here: same present-thread flag, same teardown path.
    {
        char unloadRequestPath[192];
        if (ns_paths::join(unloadRequestPath, sizeof(unloadRequestPath), "ns_unload_request")
            && ::access(unloadRequestPath, F_OK) == 0) {
            ::unlink(unloadRequestPath);
            GUI::requestUnload();
        }
    }
    // SESSION BIND: one-time injection-trailer verify + loader liveness (Utils/SessionBind.h).
    // Fail-closed: a module injected by anything but our loader releases itself here; an
    // orphaned module (loader died) releases after the ~5 min grace. Same teardown path as
    // the unload request below - proven graceful.
    {
        // The one-time verify may already have run on the SDL init thread (it must precede
        // the pattern scan); the tick then only drives the loader liveness poll.
        session_bind::presentTick([] { GUI::requestUnload(); });
    }
    if (GUI::consumeUnloadRequest())
        unloadFlag.set();
    hookContext.config().update();
    hookContext.config().performFileOperation();

    if (unloadFlag) {
        SelfUnload::log("unload: flag set - tearing down features and hooks");
        // Phase 1: latch the flag so every other hook thread short-circuits its feature logic
        // (all entry points check HookQuiesce first). This thread may keep using the context -
        // it is still complete and it is the one running the teardown.
        HookQuiesce::beginShutdown();
        // The PeepEvents hook stays installed for the whole session now (it feeds the menu), so
        // it MUST be restored here: past the self-unmap below the game's next SDL_PeepEvents
        // call would jump into unmapped memory. A callback already inside the hook is covered
        // by the drain further down.
        hookContext.hooks().peepEventsHook.disable();
        // The Vulkan present path runs on its own thread and only checks the quiesce flag
        // inside our render - restore the swapped cache pointers immediately so no NEW present
        // routes into us, then the drain below guarantees the in-flight one is done.
        VulkanHook::restorePointers();
        // Steam-client GOT hooks (fopen/fgets/open/read/close inside steamclient.so point into
        // this very library) must be restored before the self-unmap: past the dlclose those GOT
        // entries would be dangling pointers into unmapped memory and the next steamclient
        // worker thread calling fopen would SIGSEGV (the 2026-09-10 post-unload crash).
        // Threads already inside a vac hook return within microseconds; the unmap worker's
        // 500ms delay covers them with an enormous margin.
        fva::hooks::uninstall_vac_hook();
        unload(hookContext);
        // Phase 2: wait until every callback that entered before the flag was set has left our
        // code. Only then is destructing the context safe.
        HookQuiesce::drainInFlightCallbacks();

        // Statics that would survive a re-injection otherwise: phantom attack-pulse edges,
        // stale triggerbot timers, bone-history slots keyed to entities of a previous world.
        hookContext.template make<AttackCommand>().reset();
        hookContext.template make<Triggerbot>().disarmStatics();
        hookContext.template make<Lagcomp>().clearSlots();

        // ImGui + Vulkan-hook teardown: renderer shutdown (ImGui_ImplVulkan owns objects from
        // our pool/renderpass) and our Vulkan resources first, then the ImGui context. All of
        // it after the drain (the present thread must be out of GUI::render) and while the
        // game's device is still alive.
        VulkanHook::destroyResources();
        GUI::destroy();

        HookContext<GlobalContext>::destroyGlobalContext();
        SelfUnload::log("unload: teardown complete, scheduling self-unmap");

        // Last, and only after every hook is restored and all our state is gone: unmap the library
        // itself. Without this the process keeps us loaded forever, and injecting again into the
        // same game silently does nothing because dlopen will not re-run our constructor for an
        // already-loaded library. See Platform/Linux/LinuxSelfUnload.h for why the actual unmap has
        // to happen on another thread, and why it must be the very last thing that ever runs.
        //
        // This returns immediately - the unmap is deliberately deferred until well after we have
        // returned from this hook and back into the game.
        SelfUnload::unmapSelf();
    }
}

LINUX_ONLY([[gnu::aligned(8)]]) std::uint64_t PlayerPawn_sceneObjectUpdater(cs2::C_CSPlayerPawn* playerPawn, void* unknown, bool unknownBool) noexcept
{
    if (HookQuiesce::isShuttingDown()) {
        // Original is stored in the (possibly gone) context - during teardown the per-entity
        // uninstall loop in unload() restores these handles first, so this path stays theoretical.
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

// Hook on ClientModeCSNormal::OverrideView (vtable slot WIN64_LINUX(15, 16), verified offline -
// see CS2/Classes/ClientModeCSNormal.h). Runs AFTER the original, which fills the view setup
// from the per-player camera: third person repositions the camera, view punch removal restores
// the raw input angles. Both no-op unless their config vars are enabled.
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
}

// Hook on CParticleObjectDesc::DrawArray (libparticles.so, vtable slot 1 - 7 args, primitive
// count in ECX, stride-0x70 primitives; verified offline). Recolors molotov/incendiary
// primitives BEFORE the original consumes the array (WorldColors pass-through semantics).
void scene_render_hooks::onParticlesDrawArray(void* particleObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept
{
    // Teardown path forwards through the stored original directly - it stays valid until the
    // patches are restored (scene_render_hooks::uninstall), which happens before the drain.
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

// Hook on CLightBinnerGPU::ProcessLights (libscenesystem.so, vtable slot 3 - the slot holds a
// jmp thunk, which we keep as the call target for the original). Recolors each light before the
// binner sees it.
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

// Hook on CSkyBoxObjectDesc::DrawArray (libscenesystem.so, vtable slot 1 - verified offline; the
// SkyTint attribute the renderer consumes is copied from object+0xD8/0xDC/0xE0 right inside this
// function). Overwrites the tint floats before the original runs and restores them afterwards,
// so nothing persists.
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
    // The original copies the (patched) tint floats into the render attribute entry - it must
    // run between the write and the restore, or nothing is drawn at all.
    RetAddrSpoofer::spoof(original)(skyBoxObjectDesc, renderContext, primitives, primitiveCount, sceneView, sceneLayer, perFrameStats);
    WorldColors<HookContext<GlobalContext>>::restoreSky(savedSkyTints, savedCount);
}

namespace
{

// Shared body for the world-geometry DrawArray hooks (CBaseSceneObjectDesc /
// CAggregateSceneObjectDesc slot 1 - the two descriptor types the bulk of map geometry draws
// through, verified by live instance counting). Pass-through: recolor the primitive colors,
// then run the original over the modified array.
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

// Live-pawn snapshot pacing for the chams hook body (see Chams.h).
inline int chamsCallsUntilSnapshot{0};
// Phase-3 material diagnostic counters (cumulative).

// Hook on SceneObjectDesc::GeneratePrimitives (5 descriptor vtables, byte offset +0x20 - see
// ChamsHook.h). Pass-through until chams_vars::Enabled; for enemy scene objects it re-runs the
// original a second time (after clearing the object's generated-layers marker) and tints the
// APPENDED primitive range - the duplicate pass draws the model again in the chams color.
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

    // Fast pass-through: skip the rest of the machinery while both features are disabled.
    const bool chamsEnabled = GET_CONFIG_VAR(chams_vars::Enabled);
    const bool worldModEnabled = GET_CONFIG_VAR(WorldColorsWorldEnabled);
    if (!chamsEnabled && !worldModEnabled) {
        RetAddrSpoofer::spoof(original)(desc, sceneObject, sceneView, primitives);
        return;
    }

    auto&& chams = hookContext.template make<Chams>();

    // The live-pawn snapshot refreshes on a call counter (see Chams.h for the race-freedom note).
    if (chamsCallsUntilSnapshot <= 0) {
        chams.refreshLivePawnPointers();
        chamsCallsUntilSnapshot = Chams<HookContext<GlobalContext>>::kSnapshotRefreshInterval;
    }
    --chamsCallsUntilSnapshot;

    RetAddrSpoofer::spoof(original)(desc, sceneObject, sceneView, primitives);

    if (chamsEnabled && chams.wantsOverlayPass(sceneObject)) {
        // Flat chams: tint the enemy's OWN primitives in place, at generation time. The primitive
        // buffers are pooled and recycled across objects, so any generate->draw correlation by
        // buffer address tints whatever object recycles the address (gloves/guns/random props) and
        // races the pool (flicker). Generation-time is the only point where the buffer is
        // guaranteed to belong to the classified object. Every regeneration passes through this
        // hook, so the tint is refreshed automatically.
        if (primitives)
            Chams<HookContext<GlobalContext>>::applyOverlay(primitives, 0,
                Chams<HookContext<GlobalContext>>::primitiveColor(GET_CONFIG_VAR(chams_vars::EnemyColor)));
        return;
    }

    // World modulation (2026-09-12 redesign): tint every NON-pawn scene object's own primitives
    // at generation time with the world color - the same generate-time mechanism as chams flat.
    // This replaces the per-frame prim write in the DrawArray hooks: that approach raced the
    // game's own light setup (flickering viewmodel) and the fog forcing pushed the renderer into
    // rendertarget passes the pool couldn't serve. Objects owned by the LOCAL player pawn (first-
    // person arms/weapons, the local body) keep their natural shading; dropped weapons and other
    // props get the world tint like the rest of the map.
    if (worldModEnabled && primitives && !chams.ownsLocalPawn(sceneObject))
        Chams<HookContext<GlobalContext>>::applyOverlay(primitives, 0,
            Chams<HookContext<GlobalContext>>::primitiveColor(GET_CONFIG_VAR(WorldColorsWorldColor)));
}


