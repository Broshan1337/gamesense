#pragma once

#include <bit>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CImagePanel.h>
#include <MemoryPatterns/PatternTypes/PanoramaImagePanelPatternTypes.h>
#include <UI/ImGui/GuiLog.h>

struct SvgImageParams {
    const char* imageUrl;
    int textureHeight{-1};
    std::optional<cs2::Color> fillColor{};
};

template <typename HookContext>
class ImagePanel {
public:
    using RawType = cs2::CImagePanel;

    ImagePanel(HookContext& hookContext, cs2::CImagePanel* panel) noexcept
        : hookContext{hookContext}
        , panel{panel}
    {
    }

    [[nodiscard]] decltype(auto) uiPanel() const noexcept
    {
        return hookContext.uiPanel(panel ? panel->uiPanel : nullptr);
    }

    [[nodiscard]] cs2::ImageProperties* getImageProperties() const noexcept
    {
        return hookContext.patternSearchResults().template get<ImagePropertiesOffset>().of(panel).get();
    }

    [[nodiscard]] std::string_view getImagePath() const noexcept
    {
        if (auto&& imagePath = hookContext.patternSearchResults().template get<OffsetToImagePath>().of(panel).get(); imagePath && imagePath->m_pString)
            return imagePath->m_pString;
        return {};
    }

    void setImageSvg(const char* imageUrl, int textureHeight = -1) const noexcept
    {
        setImageSvg(SvgImageParams{.imageUrl = imageUrl, .textureHeight = textureHeight});
    }

    void setImageSvg(const SvgImageParams& params) const noexcept
    {
        const auto properties{getImageProperties()};
        if (!properties)
            return;

        properties->scale = uiScaleFactor();
        properties->textureHeight = params.textureHeight;

        if (params.fillColor.has_value()) {
            properties->svgAttributes[static_cast<std::size_t>(cs2::SvgAttributeType::FillColor)] = std::bit_cast<cs2::SvgAttribute>(*params.fillColor);
            properties->presentSvgAttributes |= 1 << static_cast<std::size_t>(cs2::SvgAttributeType::FillColor);
        }

        if (hookContext.patternSearchResults().template get<SetImageFunctionPointer>())
            hookContext.patternSearchResults().template get<SetImageFunctionPointer>()(panel, params.imageUrl, nullptr, properties);
    }

private:
    // 2026-10-05: the UiScaleFactorOffset pattern matches 32 sites on the 2026-10-04+ build
    // (the update duplicated this copy-shape across panel types) and the uniqueness check
    // zeroes it - the fallback below used to read through offset 0 (the panel's vptr as a
    // float!) and assert-abort debug builds in-match. Degraded to a logged fallback until
    // the pattern is re-forged with a unique anchor; a wrong-but-bounded scale only skews
    // panel rendering, an abort kills the whole session.
    [[nodiscard]] float uiScaleFactor() const
    {
        const auto scale = uiPanel().getUiScaleFactor().valueOr(1.0f);
        if (scale < 0.1f || scale > 10.0f) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                gui_log::write("[panorama] invalid UI scale factor %f - falling back to 1.0 (UiScaleFactorOffset pattern likely stale)", static_cast<double>(scale));
            }
            return 1.0f;
        }
        return scale;
    }

    HookContext& hookContext;
    cs2::CImagePanel* panel;
};
