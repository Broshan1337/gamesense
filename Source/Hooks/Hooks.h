#pragma once

#include "CSGOInputHook.h"
#include "GameEventManagerHook.h"
#include "PeepEventsHook.h"
#include "Source2ClientHook.h"
#include "ViewRenderHook.h"

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CSource2Client.h>
#include <CS2/Classes/CViewRender.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Vmt/VmtLengthCalculator.h>

struct Hooks {
    Hooks(PeepEventsHook peepEventsHook, cs2::CViewRender** viewRender, cs2::CSource2Client* source2Client, cs2::IGameEventManager2** gameEventManagerGlobal, cs2::CCSGOInput* csgoInput, const VmtLengthCalculator& clientVmtLengthCalculator) noexcept
        : clientVmtLengthCalculator{clientVmtLengthCalculator}
        , peepEventsHook{peepEventsHook}
        , viewRenderHook{viewRender, clientVmtLengthCalculator}
        , source2ClientHook{source2Client, clientVmtLengthCalculator}
        , gameEventManagerHook{gameEventManagerGlobal, clientVmtLengthCalculator}
        , csgoInputHook{csgoInput, clientVmtLengthCalculator}
    {
    }

    VmtLengthCalculator clientVmtLengthCalculator;
    VmtSwapper clientModeVmtSwapper;
    cs2::ClientModeCSNormal::GetViewmodelFov* originalGetViewmodelFov{nullptr};
    cs2::ClientModeCSNormal::OverrideView* originalOverrideView{nullptr};
    PeepEventsHook peepEventsHook;
    ViewRenderHook viewRenderHook;
    Source2ClientHook source2ClientHook;
    GameEventManagerHook gameEventManagerHook;
    CSGOInputHook csgoInputHook;
};
