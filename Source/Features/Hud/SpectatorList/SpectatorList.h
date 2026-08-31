#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Hud/SpectatorList/SpectatorListParams.h>
#include <Features/Hud/SpectatorList/SpectatorListState.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/Panorama/PanoramaLabel.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/StringBuilder.h>
#include <Utils/VerifyConsole.h>

// Who is watching my POV right now? Under the watermark, top-right, only visible while the list
// is non-empty.
//
// Detection: every player pawn carries an observer-services component (null while alive) whose
// m_hObserverTarget is the entity handle currently being spectated. A pawn whose target handle
// equals the LOCAL pawn's handle is watching us - the handle is index+serial, so no recycled
// pointer confusion. Both offsets are schema-resolved; the component is a POINTER field on the
// pawn (m_pObserverServices), the target handle an in-component CHandle.
template <typename HookContext>
class SpectatorList {
public:
    explicit SpectatorList(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        const double now = monotonicSeconds();

        auto&& boxPanel = uiEngine().getPanelFromHandle(state().boxPanelHandle);
        if (!boxPanel) {
            if (now - lastCreateAttempt < 1.0)
                return;
            lastCreateAttempt = now;
            createPanel();
            return;
        }

        if (now - lastUpdate < 0.3)
            return;
        lastUpdate = now;

        std::size_t spectatorCount = 0;
        collectSpectators(spectatorCount);

        auto&& updatedBox = uiEngine().getPanelFromHandle(state().boxPanelHandle);
        if (spectatorCount == 0) {
            if (updatedBox)
                updatedBox.setVisible(false);
            return;
        }

        if (!updatedBox)
            return;
        updatedBox.setVisible(true);
        for (std::size_t i = 0; i < lineCount(); ++i) {
            auto&& line = uiEngine().getPanelFromHandle(state().lineHandles[i]);
            if (!line)
                continue;
            if (i < spectatorCount) {
                line.clientPanel().template as<PanoramaLabel>().setText(lineTexts[i]);
                line.setVisible(true);
            } else {
                line.setVisible(false);
            }
        }
    }

    void onUnload() const noexcept
    {
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().boxPanelHandle);
    }

private:
    static constexpr std::size_t lineCount() noexcept
    {
        return sizeof(lineTexts) / sizeof(lineTexts[0]);
    }

    void collectSpectators(std::size_t& spectatorCount) const noexcept
    {
        spectatorCount = 0;

        const auto localHandle = localPawnHandleValue();
        if (localHandle == 0)
            return;

        // NOTE the declaring class: m_pObserverServices is declared on C_BasePlayerPawn (the
        // pawn's base), NOT C_CSPlayerPawn - the schema iterator only reads the requested
        // class's own fields, so asking the derived class silently returns nothing.
        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pObserverServices");
        const auto targetOffset = hookContext.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_hObserverTarget");
        const auto nameOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iszPlayerName");
        if (!servicesOffset.has_value() || !targetOffset.has_value() || !nameOffset.has_value()) {
            VerifyConsole::write(30.0f, "spec", "schema offsets unresolved - spectator detection inactive");
            return;
        }

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (spectatorCount >= lineCount())
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = baseEntity.template as<PlayerPawn>();
            if (!pawn || pawn.isControlledByLocalPlayer())
                return;

            auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(pawn.baseEntity());
            if (!pawnEntity)
                return;

            void* observerServices{};
            std::memcpy(&observerServices, reinterpret_cast<const std::byte*>(pawnEntity) + *servicesOffset, sizeof(observerServices));

            // Diagnostic: what do the other pawns' observer services actually hold? This is the
            // one place the detection can silently disagree with reality (handle encoding, bot
            // deathcam behavior), so measure instead of guess. Throttled hard - only while we
            // believe NOBODY is spectating, once per ~10s.
            if (debugLoggingEnabled()) {
                char debugLine[96];
                StringBuilder debugBuilder{debugLine};
                debugBuilder.put("local=", localHandle, ' ');
                if (!observerServices) {
                    debugBuilder.put("entity", identity.handle.index().value, ": services=null");
                } else {
                    std::uint32_t debugTarget{};
                    std::memcpy(&debugTarget, reinterpret_cast<const std::byte*>(observerServices) + *targetOffset, sizeof(debugTarget));
                    debugBuilder.put("entity", identity.handle.index().value, ": target=", debugTarget);
                }
                VerifyConsole::write(10.0f, "spec", "%s", debugBuilder.cstring());
            }

            if (!observerServices)
                return;   // alive - alive players do not spectate

            std::uint32_t targetHandleValue{};
            std::memcpy(&targetHandleValue, reinterpret_cast<const std::byte*>(observerServices) + *targetOffset, sizeof(targetHandleValue));
            if (targetHandleValue != localHandle)
                return;

            auto&& controller = pawn.playerController().baseEntity();
            const char* name = "?";
            if (auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(controller)) {
                const auto controllerName = reinterpret_cast<const char*>(reinterpret_cast<const std::byte*>(controllerEntity) + *nameOffset);
                if (looksLikeName(controllerName))
                    name = controllerName;
            }

            StringBuilder builder{lineTexts[spectatorCount]};
            builder.put(name);
            ++spectatorCount;
        });
    }

    // The debug dump runs only while the toggle file exists - create it with
    //   touch /tmp/osiris_spec_debug
    // and remove it when done.
    [[nodiscard]] static bool debugLoggingEnabled() noexcept
    {
        const int fd = LinuxPlatformApi::open("/tmp/osiris_spec_debug", 0 /* O_RDONLY */);
        if (fd >= 0) {
            LinuxPlatformApi::close(fd);
            return true;
        }
        return false;
    }

    [[nodiscard]] std::uint32_t localPawnHandleValue() const noexcept
    {
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return 0;
        return localPawn.baseEntity().handle().value;
    }

    // Player names are attacker-controlled bytes; same sanity idea as PlayerSlotLookup's check.
    [[nodiscard]] static bool looksLikeName(const char* name) noexcept
    {
        if (!name || name[0] == '\0')
            return false;
        for (int i = 0; i < 32 && name[i] != '\0'; ++i) {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c < 0x20 && c != '\t')
                return false;
        }
        return true;
    }

    void createPanel() const noexcept
    {
        using namespace spectator_list_params;

        auto&& panel = hookContext.panelFactory().createPanel(hookContext.hud().rootPanel()).uiPanel();
        if (!panel)
            return;

        panel.setFlowChildren(cs2::k_EFlowDown);
        panel.setBackgroundColor(kBoxColor);
        panel.setBorderRadius(kBoxBorderRadius);
        panel.setAlign(kAlignment);
        panel.setMargin(kBoxMargin);
        // Hidden until someone actually spectates; runtime show/hide is fine, only CREATION
        // hidden is the panorama trap (see the radio lessons).
        panel.setVisible(false);
        state().boxPanelHandle = panel.getHandle();

        for (std::size_t i = 0; i < lineCount(); ++i) {
            auto&& line = hookContext.panelFactory().createLabelPanel(panel).uiPanel();
            if (!line)
                continue;
            line.setFont(kFont);
            line.setColor(kTextColor);
            line.setMargin(kLineMargin);
            state().lineHandles[i] = line.getHandle();
        }
    }

    [[nodiscard]] auto& state() const noexcept
    {
        return hookContext.featuresStates().hudFeaturesStates.spectatorListState;
    }

    [[nodiscard]] decltype(auto) uiEngine() const noexcept
    {
        return hookContext.template make<PanoramaUiEngine>();
    }

    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
    }

    inline static char lineTexts[6][64]{};
    inline static double lastUpdate{-1.0e9};
    inline static double lastCreateAttempt{-1.0e9};

    HookContext& hookContext;
};
