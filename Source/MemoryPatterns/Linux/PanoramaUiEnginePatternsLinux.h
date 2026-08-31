#pragma once

#include <MemoryPatterns/PatternTypes/UiEnginePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PanoramaUiEnginePatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<UiEnginePointer, CodePattern{"48 89 3D ? ? ? ? E8 ? ? ? ? 48 8B 3D ? ? ? ? E8 ? ? ? ? 48"}.add(3).abs()>();
    }

    [[nodiscard]] static consteval auto addPanoramaPatterns(auto panoramaPatterns) noexcept
    {
        return panoramaPatterns
            .template addPattern<GetPanelPointerFunctionPointer, CodePattern{"8B 16 31 C9"}>()
            .template addPattern<RunScriptFunctionPointer, CodePattern{"55 48 8D 05 ? ? ? ? 48 89 E5 41 57 49 89 FF 41 56 48 8D 3D ? ? ? ? 4D 89 C6 41 55 4C 8D 2D ? ? ? ?"}>()
            // CUIEngine::RunScript (compile+run) = sub_228950 as of the 2026-08-25 panorama update.
            // This is the SOURCE-TEXT variant our runScript() needs: rdi=this, rsi=contextPanel,
            // rdx=scriptSource, rcx=originFile, r8d=line. Confirmed by its own profiling-label
            // leas: rip->0x1C5828 "CUIEngine::RunScript (compile+run)" and rip->0x1B886F "RunScript".
            // CRASH LESSON (2026-08-25): the first re-derivation grabbed sub_2161D0 - the
            // "(pre-compiled)" VARIANT (found via its own label string). That variant expects a
            // COMPILED script object in its script arg and hands it straight to v8::Script::Run
            // (via import thunk 0x1DD930, v8 import #0xc06) - passing source text made V8 jump
            // through garbage = SIGSEGV "jump to invalid address" at cs2 startup. The two variants
            // even have sibling prologues; the discriminator is WHICH label string they reference.
            // The update also moved `line` from rsi to r8 (old pattern's trailing 49 89 F6 is now
            // 4D 89 C6) - that alone broke the old pattern's byte match.
            // Verified exactly-once via offline byte scan against the 2026-08-25 libpanorama.so.
            .template addPattern<MakeSymbolFunctionPointer, CodePattern{"48 85 ? 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 F3 48 83"}>()
            .template addPattern<OnDeletePanelFunctionPointer, CodePattern{"48 85 F6 0F 84 ? ? ? ? 55 48 89 E5 41 56 41 55 41 54 49 89 FC"}>()
            .template addPattern<RegisterEventHandlerFunctionPointer, CodePattern{"55 48 8D 05 ? ? ? ? 48 89 E5 41 ? 41 ? 49 89 ? 41 ? 48 8D 3D ? ? ? ? 49 89"}>();
    }
};
