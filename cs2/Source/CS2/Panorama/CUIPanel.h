#pragma once

#include <cstdint>

#include <CS2/Classes/CUtlString.h>
#include <CS2/Classes/CUtlVector.h>

#include "CPanel2D.h"
#include "CPanoramaSymbol.h"

namespace cs2
{

enum EPanelFlag {
    k_EPanelFlag_IsVisible = 0x01,
    k_EPanelFlag_HasOwnLayoutFile = 0x40
};

struct CUIPanel {
    const void* vmt;
    CPanel2D* clientPanel;

    // 2026-09-26 5GB update: the children storage split into two separate fields -
    // the count (uint32) at +0x2C8 and the array (CUIPanel**) at +0x2D0, live-verified
    // (the root 'CSGOHud' panel's count = its child count; the array = the children
    // CUIPanels). The old embedded CUtlVector<CUIPanel*> shape is gone.
    struct Children {
        std::uint32_t count;
        CUIPanel** memory;
    };

    using setParent = void (*)(CUIPanel* thisptr, CUIPanel* parent);
    using setVisible = void (*)(CUIPanel* thisptr, bool visible);
    using getAttributeString = const char* (*)(CUIPanel* thisptr, CPanoramaSymbol attributeName, const char* defaultValue);
    using setAttributeString = void (*)(CUIPanel* thisptr, CPanoramaSymbol attributeName, const char* value);

    using classesVector = CUtlVector<CPanoramaSymbol>;
    using m_pchID = CUtlString;
    using PanelFlags = std::uint8_t;
};

} // namespace cs2
