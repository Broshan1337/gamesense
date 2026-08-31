#pragma once

#include <optional>

#include <CS2/Classes/CCvar.h>
#include <Config/Config.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/Entities/GameRules.h>
#include <GameClient/Entities/PlantedC4.h>
#include <GameClient/Entities/PlayerController.h>
#include <Features/Common/InWorldPanelsPerHookState.h>
#include <Features/Visuals/PlayerInfoInWorld/PlayerInfoPanelCachePerHookState.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/Econ/EconEntityOffsets.h>
#include <GameClient/Econ/EconItemAttributeOffsets.h>
#include <GameClient/Econ/EntitySubclassOffsets.h>
#include <GameClient/Econ/HudModelArmsOffset.h>
#include <GameClient/Econ/ModelStateOffsets.h>
#include <GameClient/SchemaSystem/ClientTypeScopePointer.h>
#include <GameClient/Entities/PlayerResource.h>
#include <GameClient/SchemaSystem/SchemaSystem.h>
#include <GameClient/FileSystem.h>
#include <GameClient/Hud/Hud.h>
#include <GameClient/Hud/HudContext.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <MemoryPatterns/PatternTypes/CvarPatternTypes.h>
#include <MemoryPatterns/PatternTypes/GameRulesPatternTypes.h>
#include <MemoryPatterns/PatternTypes/PlantedC4PatternTypes.h>
#include <GameClient/Panorama/PanelFactory.h>
#include <GameClient/GlobalVars.h>
#include <GameClient/Panorama/PanoramaTransformFactory.h>

struct BombStatusPanelState;
struct FeaturesStates;
struct GlowSceneObjectsState;
struct Hooks;
class EntityClassifier;

template <typename GlobalContext>
struct HookContext {
    HookContext() noexcept
        : fullGlobalContext{GlobalContext::instance().fullContext()}
    {
    }

    [[nodiscard]] static bool isGlobalContextComplete() noexcept
    {
        return GlobalContext::instance().isComplete();
    }

    static void initCompleteGlobalContextFromGameThread() noexcept
    {
        GlobalContext::instance().initCompleteContextFromGameThread();
    }

    static void destroyGlobalContext() noexcept
    {
        GlobalContext::destroyInstance();
    }

    [[nodiscard]] SoundWatcherState& soundWatcherState() const noexcept
    {
        return fullGlobalContext.soundWatcherState;
    }

    [[nodiscard]] BombStatusPanelState& bombStatusPanelState() const noexcept
    {
        return fullGlobalContext.bombStatusPanelState;
    }

    [[nodiscard]] InWorldPanelsState& inWorldPanelsState() const noexcept
    {
        return fullGlobalContext.inWorldPanelsState;
    }

    [[nodiscard]] InWorldPanelsPerHookState& inWorldPanelsPerHookState() const noexcept
    {
        return fullGlobalContext.inWorldPanelsPerHookState;
    }

    [[nodiscard]] PlayerInfoPanelCacheState& playerInfoPanelCacheState() const noexcept
    {
        return fullGlobalContext.playerInfoPanelCacheState;
    }

    [[nodiscard]] PlayerInfoPanelCachePerHookState& playerInfoPanelCachePerHookState() const noexcept
    {
        return fullGlobalContext.playerInfoPanelCachePerHookState;
    }

    void clearRenderHookState() const noexcept
    {
        inWorldPanelsPerHookState() = {};
        playerInfoPanelCachePerHookState() = {};
    }

    [[nodiscard]] FeaturesStates& featuresStates() const noexcept
    {
        return fullGlobalContext.featuresStates;
    }

    [[nodiscard]] GlowSceneObjectsState& glowSceneObjectsState() const noexcept
    {
        return fullGlobalContext.glowSceneObjectsState;
    }

    [[nodiscard]] EntityClassifier& entityClassifier() const noexcept
    {
        return fullGlobalContext.entityClassifier;
    }

    [[nodiscard]] Hooks& hooks() const noexcept
    {
        return fullGlobalContext.hooks;
    }

    [[nodiscard]] auto hud() noexcept
    {
        return Hud{HudContext{*this}};
    }

    [[nodiscard]] auto localPlayerController() noexcept
    {
        if (fullGlobalContext.patternSearchResults.template get<LocalPlayerControllerPointer>())
            return PlayerController{*this, *fullGlobalContext.patternSearchResults.template get<LocalPlayerControllerPointer>()};
        return PlayerController{*this, nullptr};
    }

    [[nodiscard]] auto globalVars() noexcept
    {
        if (fullGlobalContext.patternSearchResults.template get<GlobalVarsPointer>())
            return GlobalVars{*this, *fullGlobalContext.patternSearchResults.template get<GlobalVarsPointer>()};
        return GlobalVars{*this, nullptr};
    }

    [[nodiscard]] auto gameRules() noexcept
    {
        if (fullGlobalContext.patternSearchResults.template get<GameRulesPointer>())
            return GameRules{*this, *fullGlobalContext.patternSearchResults.template get<GameRulesPointer>()};
        return GameRules{*this, nullptr};
    }

    [[nodiscard]] auto plantedC4() noexcept
    {
        return std::optional{make<PlantedC4<HookContext>>(getPlantedC4())};
    }

    [[nodiscard]] auto cvarSystem() noexcept
    {
        return CvarSystem{*this};
    }

    [[nodiscard]] const auto& getConVarsBase() noexcept
    {
        if (!fullGlobalContext.conVars.has_value())
            fullGlobalContext.conVars.emplace(CvarSystem{*this});
        return *fullGlobalContext.conVars;
    }

    [[nodiscard]] auto schemaSystem() noexcept
    {
        return SchemaSystem{*this};
    }

    [[nodiscard]] const auto& econEntityOffsets() noexcept
    {
        return resolveOffsets(fullGlobalContext.econEntityOffsets);
    }

    [[nodiscard]] const auto& econItemAttributeOffsets() noexcept
    {
        return resolveOffsets(fullGlobalContext.econItemAttributeOffsets);
    }

    [[nodiscard]] const auto& entitySubclassOffsets() noexcept
    {
        return resolveOffsets(fullGlobalContext.entitySubclassOffsets);
    }

    [[nodiscard]] const auto& modelStateOffsets() noexcept
    {
        return resolveOffsets(fullGlobalContext.modelStateOffsets);
    }

    [[nodiscard]] const auto& hudModelArmsOffset() noexcept
    {
        return resolveOffsets(fullGlobalContext.hudModelArmsOffset);
    }

    [[nodiscard]] auto& skinChangerState() noexcept
    {
        return fullGlobalContext.skinChangerState;
    }

    [[nodiscard]] const auto& clientTypeScope() noexcept
    {
        if (!fullGlobalContext.clientTypeScope.has_value())
            fullGlobalContext.clientTypeScope.emplace();
        return *fullGlobalContext.clientTypeScope;
    }

    template <typename T, typename... Args>
    [[nodiscard]] auto make(Args&&... args) noexcept
    {
        return T{*this, std::forward<Args>(args)...};
    }

    template <template <typename...> typename T, typename... Args>
    [[nodiscard]] auto make(Args&&... args) noexcept
    {
        return T<HookContext>{*this, std::forward<Args>(args)...};
    }

    [[nodiscard]] auto panelFactory() noexcept
    {
        return PanelFactory{*this};
    }

    [[nodiscard]] auto panoramaTransformFactory() noexcept
    {
        return PanoramaTransformFactory{*this, fullGlobalContext.patternSearchResults.template get<TransformTranslate3dVMT>(), fullGlobalContext.patternSearchResults.template get<TransformScale3dVMT>()};
    }

    [[nodiscard]] const auto& panoramaSymbols() noexcept
    {
        auto& symbols = fullGlobalContext.panoramaSymbols;
        if (!symbols.has_value())
            symbols.emplace(*this);
        return *symbols;
    }

    [[nodiscard]] const auto& patternSearchResults() noexcept
    {
        return fullGlobalContext.patternSearchResults;
    }

    [[nodiscard]] auto& hudState() noexcept
    {
        return fullGlobalContext.hudState;
    }

    [[nodiscard]] auto& fileNameSymbolTableState() noexcept
    {
        return fullGlobalContext.fileNameSymbolTableState;
    }

    [[nodiscard]] auto& memAllocState() noexcept
    {
        return fullGlobalContext.memAllocState;
    }

    [[nodiscard]] auto& glowSceneObjectState() noexcept
    {
        return fullGlobalContext.glowSceneObjectState;
    }

    [[nodiscard]] auto& stylePropertySymbolsAndVMTs() noexcept
    {
        return fullGlobalContext.stylePropertySymbolsAndVMTs;
    }

    [[nodiscard]] auto config() noexcept
    {
        return Config{*this};
    }

    [[nodiscard]] auto& configState() noexcept
    {
        return fullGlobalContext.configState;
    }

    [[nodiscard]] const auto& osirisDirectoryPath() noexcept
    {
        return fullGlobalContext.osirisDirectoryPath;
    }

    [[nodiscard]] auto soundWatcher() noexcept
    {
        return SoundWatcher<HookContext>{fullGlobalContext.soundWatcherState, *this};
    }

    [[nodiscard]] auto uiPanel(cs2::CUIPanel* panel) noexcept
    {
        return PanoramaUiPanel<HookContext>{*this, panel};
    }

    [[nodiscard]] decltype(auto) activeLocalPlayerPawn() noexcept
    {
        return localPlayerController().pawn().template cast<PlayerPawn>();
    }

    [[nodiscard]] decltype(auto) localPlayerBulletInaccuracy() noexcept
    {
        return activeLocalPlayerPawn().getActiveWeapon().bulletInaccuracy();
    }

    [[nodiscard]] decltype(auto) playerResource()
    {
        if (fullGlobalContext.patternSearchResults.template get<PointerToPlayerResource>())
            return PlayerResource{*this, *fullGlobalContext.patternSearchResults.template get<PointerToPlayerResource>()};
        return PlayerResource{*this, nullptr};
    }

private:
    // Resolves a block of schema field offsets, RE-resolving while the answer is still incomplete
    // rather than caching whatever the first touch happened to produce.
    //
    // That distinction is the whole reason the skin changer no longer has to wait 15 seconds. These
    // blocks are resolved lazily on first use and kept for the lifetime of the process, so a single
    // touch made before the schema system could answer used to cache every offset as 0 - permanently,
    // for the rest of the session, not just until the schema came up. The old 15s gate existed to
    // guarantee that first touch happened late enough to be correct. Making a failed attempt
    // non-sticky removes the need for the guarantee: an early touch is now simply retried.
    //
    // Bounded at both ends so this cannot become a per-frame cost: it stops as soon as an attempt
    // comes back complete (the normal case, within a frame or two), and stops unconditionally after
    // kGiveUpMapTime, so a field genuinely removed by a game update settles for the best answer
    // available instead of walking the schema forever. An offset left at 0 makes its own feature
    // no-op, exactly as it did before.
    template <typename Offsets>
    [[nodiscard]] const Offsets& resolveOffsets(std::optional<Offsets>& cached) noexcept
    {
        if (!cached.has_value() || (!cached->isFullyResolved() && !pastOffsetResolveDeadline()))
            cached.emplace(schemaSystem());
        return *cached;
    }

    [[nodiscard]] bool pastOffsetResolveDeadline() noexcept
    {
        const auto mapTime = globalVars().curtime();
        return mapTime.hasValue() && mapTime.value() >= schema_readiness::kGiveUpMapTime;
    }

    [[nodiscard]] cs2::CPlantedC4* getPlantedC4() const noexcept
    {
        const auto* const plantedC4 = fullGlobalContext.patternSearchResults.template get<PlantedC4sPointer>();
        if (plantedC4)
            return *plantedC4;
        return nullptr;
    }

    GlobalContext::Complete& fullGlobalContext;
};
