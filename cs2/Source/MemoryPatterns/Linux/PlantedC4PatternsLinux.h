#pragma once

#include <MemoryPatterns/PatternTypes/PlantedC4PatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PlantedC4Patterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            // 2026-09-23 1.41.8.2: two byte-identical guard+global templates exist; the planted
            // C4 singleton is the one whose consumer reads m_hBombDefuser (0x1268) off the
            // loaded pointer and compares it against -1 (the defuse code at 0x1B4E490). The
            // guard's literal offset (cmp byte [rdi+0x8BF],0) + the test/je tail disambiguate.
            .template addPattern<PlantedC4sPointer, CodePattern{"80 BF BF 08 00 00 00 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 85 C0 0F 84"}.add(16).abs()>()
            // 2026-09-23 1.41.8.2: the site recompiled (rbx -> r14: 8B 83 -> 41 8B 86) and the
            // bomb-site offset moved to 0x1214 (schema m_nBombSite 4628). Offset bytes are
            // load-bearing; verified exactly-once.
            .template addPattern<BombSiteOffset, CodePattern{"0F 86 ? ? ? ? 41 8B 86 ? ? ? ? 41 B9"}.add(9).read()>()
            // 2026-09-23 1.41.8.2: the site recompiled (the `mov byte [reg],0; test al,al` head
            // became a `cmp/je` pair); re-anchored on the ticking-flag chain
            // `cmp byte [rbx+0x1210],0; je; mov byte [rbx+0x1210],0; cmp byte [rbx+0x1264],0`
            // (m_bBombTicking 0x1210 schema-confirmed). add(2).read() = the cmp disp32.
            .template addPattern<BombTickingOffset, CodePattern{"80 BB ? ? ? ? 00 74 ? C6 83 ? ? ? ? 00 80 BB ? ? ? ? 00 74 07 C6 83 ? ? ? ? 00 80 BB 64 12 00 00 00"}.add(18).read()>()
            // 2026-09-23 1.41.8.2: the subss collapsed into a direct comiss and the register
            // shuffled (rbx kept); m_flC4Blow = 0x1240 (schema 4672). Offset bytes load-bearing;
            // verified exactly-once.
            .template addPattern<BombBlowTimeOffset, CodePattern{"F3 0F 10 8B ? ? ? ? 0F 2F C8 0F 8A"}.add(4).read()>()
            // 2026-09-23 1.41.8.2: re-anchored on the defuser resolution call
            // `lea rdi,[rbx+0x1268] (m_hBombDefuser, schema 4712); call <entity-by-handle>;
            // test; je`. Offset bytes load-bearing; verified exactly-once.
            .template addPattern<BombDefuserOffset, CodePattern{"48 8D BB ? ? ? ? E8 ? ? ? ? 48 85 C0 0F 84 03 01 00 00 4C 8B"}.add(3).read()>()
            .template addPattern<BombDefuseEndTimeOffset, CodePattern{"74 ? F3 0F 10 80 ? ? ? ? 4C"}.add(6).read()>();
    }
};
