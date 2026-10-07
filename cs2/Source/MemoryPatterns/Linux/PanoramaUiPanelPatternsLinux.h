#pragma once
#include <MemoryPatterns/PatternTypes/UiPanelPatternTypes.h>
#include <MemorySearch/CodePattern.h>
struct PanoramaUiPanelPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<SetParentFunctionOffset, CodePattern{"48 8B 90 ? ? ? ? 48 85 F6 74 16"}.add(3).read()>()
            .template addPattern<GetAttributeStringFunctionOffset, CodePattern{"FF 90 ? ? ? ? 80 BB 90 ? ? ? ? 48 89 C2"}.add(2).read()>()
            // 2026-09-23 1.41.8.2: site recompiled (r15d -> r14d source, rdi restore follows);
            // slot moved 0x4D0 -> 0x110 (schema-side vtable reshuffle). The offset bytes are
            // load-bearing; verified exactly-once.
            .template addPattern<SetAttributeStringFunctionOffset, CodePattern{"FF 90 ? ? ? ? 44 8B B5 ? ? ? ? 48 89 DF"}.add(2).read()>();
    }
    [[nodiscard]] static consteval auto addPanoramaPatterns(auto panoramaPatterns) noexcept
    {
        // Verified against the installed 2026-10-06 libpanorama.so and CUIPanel vtable.
        // The old child-list anchors read another object's +0x2C8/+0x2D0 storage;
        // CUIPanel's own checked accessor reads count +0x28 and array +0x30.
        // The visibility wrapper sets the style boolean, rather than calling the
        // unrelated client pointer getter previously matched as SetVisible.
        return panoramaPatterns
            .template addPattern<SetPanelVisibleFunctionPointer, CodePattern{"40 0F B6 F6 48 83 C7 ? E9 ? ? ? ? CC CC CC 55 48 89 E5 41 57 41 56 41 55 41 54"}>()
            // CUIPanel child accessor: check count, then index the array.
            .template addPattern<ChildPanelsCountOffset, CodePattern{"31 C0 85 F6 78 ? 3B 77 ? 7D ? 48 8B 47 ? 48 63 F6 48 8B 04 F0 C3"}.add(8).read8()>()
            .template addPattern<ChildPanelsArrayOffset, CodePattern{"31 C0 85 F6 78 ? 3B 77 ? 7D ? 48 8B 47 ? 48 63 F6 48 8B 04 F0 C3"}.add(14).read8()>()
            .template addPattern<PanelClassesVectorOffset, CodePattern{"97 ? ? ? ? 85 D2 7E ? 48 8B 87"}.add(1).read()>()
            .template addPattern<PanelStyleOffset, CodePattern{"48 8D 47 ? C3 CC CC CC CC CC CC CC CC CC CC CC 80 8F ? ? ? ? 04 C3"}.add(3).read8()>()
            .template addPattern<ParentWindowOffset, CodePattern{"48 8B 47 ? C3 CC CC CC CC CC CC CC CC CC CC CC 0F B6 87 ? ? ? ? C0 E8 06"}.add(3).read8()>()
            .template addPattern<OffsetToPanelId, CodePattern{"48 8B 47 ? 48 8D 15 ? ? ? ? 48 85 C0 48 0F 44 C2 C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 48 8B 57 ? 31 C0 48 85 D2 74 ? 80 3A 00 0F 95 C0 C3"}.add(3).read8()>()
            .template addPattern<OffsetToPanelFlags, CodePattern{"3E 41 F6 87 ? ? ? ?"}.add(4).read()>()
            .template addPattern<OffsetToPanelHandle, CodePattern{"48 8B 86 ? ? ? ? C3"}.add(3).read()>();
    }
};
