#pragma once

#include "GlobalContext/HookQuiesce.h"
#include "GlobalContext/GlobalContext.h"
#include "Hooks/PeepEventsHook.h"
#include "Hooks/Graphics/VulkanHook.h"
#include "Platform/SelfUnload.h"
#include "Utils/ReturnAddress.h"
#include <Utils/RetAddrSpoofer.h>
#include "Utils/StatusReport.h"
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
#include <Features/Misc/DiscordRpc.h>
#include <Features/Hud/HudThemeColor.h>
#include <Features/SkinChanger/SkinChanger.h>
#include <Features/Game/Blockbot.h>
#include <Features/Game/Bunnyhop.h>
#include <Features/Game/Movement.h>
#include <Features/Game/TestStrafer.h>
#include <Features/Game/CooldownRevealer.h>
#include <Features/Game/FakeLevel.h>
#include <Features/Game/FvaEmulator.h>
#include <Features/Game/IsValveDsSpoof.h>
#include <Features/Game/FakePrime.h>
#include <Features/Game/MatchAutoAccept.h>
#include <Features/Game/HitLog.h>
#include <Features/Game/Killsay.h>
#include <Features/Game/PanicKey.h>
#include <Features/Game/TeamDamageTracker.h>
#include <Features/Game/VoteRevealer.h>
#include <Features/Sound/HitSound.h>
#include <Features/Sound/SpawnProtectionSound.h>
#include <Features/Sound/WelcomeSound.h>
#include <Features/Hud/Watermark/Watermark.h>
#include <Features/Hud/StatusPanel/StatusPanel.h>
#include <Features/Hud/CombatStats/CombatStats.h>
#include <Features/Hud/SpectatorList/SpectatorList.h>
#include <Features/Visuals/Hitmarker/Hitmarker.h>
#include <Features/Visuals/PlayerList/PlayerList.h>
#include <Features/Visuals/Removals/Removals.h>
#include <Features/Visuals/ThirdPerson/ForceThirdPerson.h>
#include <Features/Visuals/WorldColors/WorldColors.h>
#include <Features/Lua/LuaManager.h>
#include <Hooks/SceneRenderHooks.h>

[[NOINLINE]] void finishInit(auto& hookContext)
{
    hookContext.entityClassifier().init(hookContext);
    hookContext.config().init();
    hookContext.config().scheduleLoad();

    // Lua scripting framework: creates ~/OsirisCS2/scripts and prepares the manager. Scripts
    // are loaded on demand from the menu tab, not automatically here.
    lua::init();
    lua::menuOpenQuery = []() noexcept { return GUI::isMenuOpen(); };

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

    // ImGui menu: build the context (allocations bridged to CS2's IMemAlloc) and attempt the
    // Vulkan presentation hook. The hook legitimately fails while libvulkan is not mapped yet
    // (early startup) - ViewRenderHook_onRenderStart retries it every frame until it sticks.
    // NOTE: the PeepEvents hook deliberately STAYS enabled from here on (it used to disable
    // itself after serving as the init trigger) - it is the menu's input feed now.
    if (GUI::init())
        (void)VulkanHook::tryInstall(); // false = libvulkan not mapped yet; retried per frame below

    hookContext.hooks().viewRenderHook.install();
    hookContext.hooks().source2ClientHook.install();
    hookContext.hooks().gameEventManagerHook.install();
    hookContext.hooks().csgoInputHook.install();
    // Both ClientMode slots (OverrideView for third person / view punch removal, GetViewmodelFov)
    // ride one VmtSwapper; the hook bodies self-gate on their config vars.
    hookContext.template make<ClientModeHooks>().hookClientMode();
    // Scene-render pass-through hooks (particle recolor, light recolor) + the patch anchors for
    // the Removals toggles. Fails closed per anchor with a StatusReport entry.
    (void)scene_render_hooks::install(); // per-anchor results already landed in StatusReport
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
    if (initInProgress)
        HookContext<GlobalContext>::initCompleteGlobalContextFromGameThread();

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
    hookContext.template make<StatusPanel>().onUnload();
    hookContext.template make<CombatStats>().onUnload();
    hookContext.template make<Blockbot>().onUnload();
    hookContext.template make<Bunnyhop>().onUnload();
    hookContext.template make<Movement>().onUnload();
    hookContext.template make<Triggerbot>().onUnload();
    hookContext.template make<RadioManager>().onUnload();
    hookContext.template make<DiscordRpc>().onUnload();
    hookContext.template make<FakePrime>().onUnload();
    hookContext.template make<FakeLevel>().onUnload();
    hookContext.template make<MatchAutoAccept>().onUnload();
    hookContext.template make<CooldownRevealer>().onUnload();
    // Feature-owned code patches (legs render skip, post-hud layer skip) and the scene-render
    // pass-through hooks must be restored while the context is still alive.
    hookContext.template make<Removals>().onUnload();
    scene_render_hooks::uninstall();
    hookContext.hooks().viewRenderHook.uninstall();
    hookContext.hooks().source2ClientHook.uninstall();
    hookContext.hooks().gameEventManagerHook.uninstall();
    hookContext.hooks().csgoInputHook.uninstall();
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
}

// Numeric event fields for the Lua callbacks' `event` table (see lua::dispatchEvent). Strings
// are deliberately absent: intForKey/floatForKey/entityForKey are the only vtable slots
// verified by decompilation in this tree (GameEventFields.h); GetString (slots 11/12 pair) is
// not, and calling an unverified slot with a char* cast is a garbage-pointer read.
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
            out[count].intValue = value;
            ++count;
        }
    };
    const auto addFloat = [&](const char* key, float value) {
        if (count < kMaxArgs) {
            out[count].key = key;
            out[count].isNumber = true;
            out[count].numberValue = value;
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
    } else if (std::strcmp(eventName, "player_death") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
        addInt("attacker", static_cast<int>(game_events::entityForKey(event, "attacker")));
        addInt("assister", static_cast<int>(game_events::entityForKey(event, "assister")));
        addInt("headshot", game_events::intForKey(event, "headshot"));
        addInt("dominated", game_events::intForKey(event, "dominated"));
    } else if (std::strcmp(eventName, "weapon_fire") == 0) {
        addInt("userid", static_cast<int>(game_events::entityForKey(event, "userid")));
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
    hookContext.template make<VoteRevealer>().onFireEventClientSide(event);
    hookContext.template make<SpawnProtectionSound>().onFireEventClientSide(event);
    hookContext.template make<CombatStats>().onFireEventClientSide(event);
    hookContext.template make<Killsay>().onFireEventClientSide(event);
    // Lua scripts: dispatch under the event's own name (player_hurt etc.), with the common
    // numeric fields passed along as the callbacks' `event` table. Everything is read through
    // the event vtable BEFORE the original can recycle the event object.
    const char* const eventName = game_events::name(event);
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
    // The bunnyhop runs FIRST: its frame-perfect landing pair (release + press at the predicted
    // touchdown fraction) has to be on the command's subtick timeline before the quantized strafer
    // reads that same timeline - the strafer then schedules its yaw deltas AFTER the jump press
    // instead of colliding with it.
    hookContext.template make<Bunnyhop>().onCreateMove(cmd);
    // The movement suite (edgejump/edgestop/slowwalk/fastladder/jumpbug) stages behind the
    // bunnyhop: same CreateMove decision point, its writes go out through BuildUserCmd /
    // WriteMoveCrc below. See Movement.h.
    hookContext.template make<Movement>().onCreateMove(cmd);
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
    // FVA-style view-angle chains: runs LAST so the history entries interpolate towards the FINAL
    // angles every feature above settled on. See Features/Game/FvaEmulator.h.
    hookContext.template make<FvaEmulator>().onCreateMove(cmd);

    // Lua scripts: one "createmove" callback batch per input tick, after all native features.
    lua::dispatchTick();
}

// Hook on CCSGOInput slot 6 - the function that builds the command from the queued input samples.
//
// This one runs BEFORE the original, which is the opposite of the CreateMove hook above and is the
// entire point: slot 6 is what turns input into movement, so steering means handing it different
// input, not correcting its output afterwards. Everything downstream - the movement members, the
// protobuf fields, the per-subtick analog deltas - is then produced by the game itself.
std::uint64_t CSGOInputHook_onBuildUserCmd(cs2::CCSGOInput* thisptr, int slot, int frameNumber) noexcept
{
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
    const bool shuttingDown = HookQuiesce::isShuttingDown();
    if (shuttingDown && !HookContext<GlobalContext>::isGlobalContextComplete())
        return 0;

    HookQuiesce::InFlight flight;
    HookContext<GlobalContext> hookContext;
    if (!shuttingDown && !hookContext.template make<PanicKey>().isActive()) {
    hookContext.template make<Blockbot>().onWriteMoveCrc(cmd);
    hookContext.template make<Bunnyhop>().onWriteMoveCrc(cmd);
    // The movement suite writes its buttons/view angles/subtick brackets behind the bunnyhop's
    // (both share the slot-7 write position; the staged flags never overlap on one tick).
    hookContext.template make<Movement>().onWriteMoveCrc(cmd);
    // The quantized strafer writes its yaw-delta subtick steps HERE, not at CreateMove: slot 6
    // rebuilds the subtick timeline from the input queue after CreateMove on this build, so
    // steps appended there never reach the wire. It runs after the bunnyhop so its simulation
    // starts after the landing taps on the same timeline.
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
    }
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
    if (const auto mapTime = hookContext.globalVars().curtime(); mapTime.hasValue() && mapTime.value() >= schema_readiness::kMinMapTime)
        hookContext.template make<SkinChanger>().run();

    hookContext.make<InWorldPanels>().updateState();
    SoundWatcher<decltype(hookContext)> soundWatcher{hookContext.soundWatcherState(), hookContext};
    soundWatcher.update();
    SoundFeatures{hookContext.soundWatcherState(), hookContext.hooks().viewRenderHook, hookContext}.runOnViewMatrixUpdate();
    hookContext.template make<Hitmarker>().run();
    hookContext.template make<PlayerList>().run();
    hookContext.template make<WorldColors>().run();
    // Viewmodel position: forces the viewmodel_offset_x/y/z cvars while enabled.
    hookContext.template make<ViewmodelMod>().run();
    hookContext.template make<Removals>().run();

    hookContext.template make<SpawnProtectionSound>().run();
    hookContext.template make<CooldownRevealer>().run();
    hookContext.template make<FakePrime>().run();
    hookContext.template make<FakeLevel>().run();
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
    hookContext.template make<StatusPanel>().run();
    hookContext.template make<CombatStats>().run();
    hookContext.template make<HudThemeColor>().run();
    hookContext.template make<SpectatorList>().run();

    // Init health report: features that validated runtime prerequisites during init recorded
    // [status] entries; dump them once on the first rendered frame (engine console is ready by
    // now - WelcomeSound's rationale, same pattern).
    StatusReport::dumpOnce([](const char* message, bool ok) {
        VerifyConsole::write(0.0f, "status", "%s %s", ok ? "OK" : "FAIL-CLOSED", message);
    });

    UnloadFlag unloadFlag;
    // The ImGui menu's Unload button sets a present-thread flag; the teardown itself stays on
    // this thread like every other unload path.
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
