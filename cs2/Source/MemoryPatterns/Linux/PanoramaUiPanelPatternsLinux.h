#pragma once

#include <MemoryPatterns/PatternTypes/UiPanelPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PanoramaUiPanelPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<SetParentFunctionOffset, CodePattern{"48 8B 90 ? ? ? ? 48 85 F6 74 16"}.add(3).read()>()
            // 2026-09-23 1.41.8.2: the old shape matched a SECOND site (an item-panel call with a
            // different vtable slot) - extended with this site's unique continuation `49 89 C7`
            // (the result lands in r15). Slot stays 0x298.
            .template addPattern<SetVisibleFunctionOffset, CodePattern{"48 8B 07 FF 90 ? ? ? ? 4C 89 F6 48 89 DF 49 89 C7"}.add(5).read()>()
            .template addPattern<GetAttributeStringFunctionOffset, CodePattern{"FF 90 ? ? ? ? 80 BB 90 ? ? ? ? 48 89 C2"}.add(2).read()>()
            // 2026-09-23 1.41.8.2: site recompiled (r15d -> r14d source, rdi restore follows);
            // slot moved 0x4D0 -> 0x110 (schema-side vtable reshuffle). The offset bytes are
            // load-bearing; verified exactly-once.
            .template addPattern<SetAttributeStringFunctionOffset, CodePattern{"FF 90 ? ? ? ? 44 8B B5 ? ? ? ? 48 89 DF"}.add(2).read()>();
    }

    [[nodiscard]] static consteval auto addPanoramaPatterns(auto panoramaPatterns) noexcept
    {
        return panoramaPatterns
            // 2026-09-26 5GB update (build 68f386a6): the children storage split - the count read +
            // the array read adjacent in the game's children iterator (libpanorama):
            //   0x25CEB9: mov edi,[rax+0x2C8] / test edi,edi / jle(rel32) / mov rcx,[rax+0x2D0] /
            //             movsxd rsi,edi / xor edx,edx / jmp
            // (live-verified offsets: 'CSGOHud' count@+0x2C8 = 1, array@+0x2D0 = ['Hud'].)
            // The previous agent's "161 green" claim was FALSE - validate reported MISSING:
            // the count read recompiled ecx->edi (8B 88 -> 8B B8) so the old pattern hit 0, and
            // ChildPanelsArrayOffset was never re-added at all (= the tree did not even compile:
            // get<ChildPanelsArrayOffset>() static_asserts "Unknown type").
            // COUNT anchor: the bare "8B B8 ? ? ? ? 85 FF" shape matches 5 sites (the children
            // twins 0x25CEB9/0x277FF5 read 0x2C8, three decoys read 0x240/0x80/0xA04); extended
            // through the rel32 jle (0F 8E - the 0x277FF5 twin recompiled to a rel8 jle) into
            // the array read's first bytes -> exactly-once at 0x25CEB9.
            // 2026-10-05 03:xx re-forge for the 2026-10-05 build (the earlier 2026-10-04
            // re-forge anchored in libclient - WRONG MODULE: CUIPanel lives in libpanorama,
            // so both offsets zeroed again). New anchor: the CUIPanel children
            // iterate/remove pair at panorama .text+0x278035 (remove twin at +0x25cef9,
            // same count/array) - exactly-once with wildcards at the read offsets.
            // count = panel+0x2C8, array = +0x2D0 (count + 8 - the structural rule holds).
            .template addPattern<ChildPanelsCountOffset, CodePattern{"8B B8 ? ? ? ? 85 FF 7E 44 48 8B 88 D0 02 00 00"}.add(2).read()>()
            // ARRAY anchor: the same site's array load; 4-byte disp32 at +13 from the count.
            .template addPattern<ChildPanelsArrayOffset, CodePattern{"8B B8 ? ? ? ? 85 FF 7E 44 48 8B 88 ? ? ? ?"}.add(13).read()>()
            .template addPattern<PanelClassesVectorOffset, CodePattern{"97 ? ? ? ? 85 D2 7E ? 48 8B 87"}.add(1).read()>()
            .template addPattern<PanelStyleOffset, CodePattern{"67 ? 53 48 83"}.add(1).add(1).read8()>()
            .template addPattern<ParentWindowOffset, CodePattern{"? 48 85 D2 74 ? 48 89 53"}.read8()>()
            .template addPattern<OffsetToPanelId, CodePattern{"50 ? ? 83 ? ? 00 0F 84 ? ? ? ? ? 8D"}.add(5).add(5).read8()>()
            .template addPattern<OffsetToPanelFlags, CodePattern{"3E 41 F6 87 ? ? ? ?"}.add(4).read()>()
            .template addPattern<OffsetToPanelHandle, CodePattern{"48 8B 86 ? ? ? ? C3"}.add(3).read()>();
    }
};
