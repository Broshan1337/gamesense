#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <MemorySearch/TypeinfoVtableResolver.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>

// Scene-render function hooks for the ported FrameworkCS2 visuals (WorldColors recolors,
// Removals). Unlike the VMT hooks in this project these vtables belong to process-global
// descriptor/singleton classes we never construct ourselves, so they are patched in place:
// one qword per hooked slot, saved original restored on uninstall (the same mechanism the
// chams experiments used - see project notes).
//
//   - CParticleObjectDesc::DrawArray        libparticles.so  vtable slot 1 (count in ECX,
//     stride-0x70 CMeshDrawPrimitive array) - inferno/molotov particle recolor pass-through.
//   - CLightBinnerGPU::ProcessLights        libscenesystem.so vtable slot 3 (jmp thunk target
//     behind it) - per-light color recolor pass-through.
//   - RenderingPipelineCsgoPostHud          libclient.so     vtable slot 0 - flash overlay
//     removal by skipping the whole layer (reference does the same early-return).
//     Typeinfo name is "29CRenderingPipelineCsgoPostHud" (class has the leading C).
//
// Slot indices and class names were verified offline against the live binaries (capstone; see
// project_frameworkcs2_port memory). Everything fails closed with a StatusReport entry instead
// of guessing.
namespace scene_render_hooks
{

void onParticlesDrawArray(void* particleObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept;
void onProcessLights(void* lightBinner, void* sceneLightObject, void* unknown) noexcept;
void onSkyBoxDrawArray(void* skyBoxObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept;
void onSceneObjectDrawArray(void* sceneObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept;
void onAggregateSceneObjectDrawArray(void* sceneObjectDesc, void* renderContext, void* primitives, unsigned primitiveCount, void* sceneView, void* sceneLayer, void* perFrameStats) noexcept;

void addLayersPostHudSkipStub(void*, void*, void*, void*, void*) noexcept
{
}

struct Hooks {
    VTableSlotPatch particlesDrawArray;
    VTableSlotPatch lightBinnerProcessLights;
    VTableSlotPatch skyBoxDrawArray;
    VTableSlotPatch sceneObjectDrawArray;
    VTableSlotPatch aggregateSceneObjectDrawArray;
    VTableSlotPatch addLayersPostHud;
    SingleBytePatch renderLegs;
};

inline Hooks hooks;

inline std::uintptr_t flashOverlaySlot{0};
inline std::uintptr_t renderLegsFunction{0};

[[nodiscard]] inline bool install() noexcept
{
    bool allResolved = true;

    // Particles: libparticles.so CParticleObjectDesc vtable slot 1.
    {
        const DynamicLibrary particlesLibrary{cs2::PARTICLES_DLL};
        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(particlesLibrary.getVmtFinderParams().rodataSection, particlesLibrary.getVmtFinderParams().dataRelRoSection, particlesLibrary.getCodeSection(), "CParticleObjectDesc")) {
            if (hooks.particlesDrawArray.install(vtable->addressPoint + sizeof(void*), reinterpret_cast<std::uintptr_t>(&onParticlesDrawArray)))
                StatusReport::record("ParticlesDrawArray hook (libparticles CParticleObjectDesc slot 1)", true);
            else
                StatusReport::record("ParticlesDrawArray hook install failed", false);
        } else {
            StatusReport::record("CParticleObjectDesc vtable not resolved - inferno recolor inactive", false);
            allResolved = false;
        }
    }

    // Lights: libscenesystem.so CLightBinnerGPU vtable slot 3.
    {
        const DynamicLibrary scenesystemLibrary{cs2::SCENESYSTEM_DLL};
        const auto rodata = scenesystemLibrary.getVmtFinderParams().rodataSection;
        const auto dataRelRo = scenesystemLibrary.getVmtFinderParams().dataRelRoSection;
        const auto text = scenesystemLibrary.getCodeSection();
        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(rodata, dataRelRo, text, "CLightBinnerGPU")) {
            if (hooks.lightBinnerProcessLights.install(vtable->addressPoint + 3 * sizeof(void*), reinterpret_cast<std::uintptr_t>(&onProcessLights)))
                StatusReport::record("LightBinner ProcessLights hook (libscenesystem CLightBinnerGPU slot 3)", true);
            else
                StatusReport::record("LightBinner hook install failed", false);
        } else {
            StatusReport::record("CLightBinnerGPU vtable not resolved - light recolor inactive", false);
            allResolved = false;
        }

        // Sky: libscenesystem.so CSkyBoxObjectDesc vtable slot 1 (DrawArray; slot 0 is a getter).
        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(rodata, dataRelRo, text, "CSkyBoxObjectDesc")) {
            if (hooks.skyBoxDrawArray.install(vtable->addressPoint + sizeof(void*), reinterpret_cast<std::uintptr_t>(&onSkyBoxDrawArray)))
                StatusReport::record("SkyBoxDrawArray hook (libscenesystem CSkyBoxObjectDesc slot 1)", true);
            else
                StatusReport::record("SkyBoxDrawArray hook install failed", false);
        } else {
            StatusReport::record("CSkyBoxObjectDesc vtable not resolved - sky recolor inactive", false);
            allResolved = false;
        }

        // World geometry: libscenesystem.so CBaseSceneObjectDesc + CAggregateSceneObjectDesc
        // DrawArray (slot 1). Live-map instance counting showed the bulk of scene objects run
        // through these two descriptor types (world color recolor pass-through).
        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(rodata, dataRelRo, text, "CBaseSceneObjectDesc")) {
            if (hooks.sceneObjectDrawArray.install(vtable->addressPoint + sizeof(void*), reinterpret_cast<std::uintptr_t>(&onSceneObjectDrawArray)))
                StatusReport::record("SceneObjectDrawArray hook (libscenesystem CBaseSceneObjectDesc slot 1)", true);
            else
                StatusReport::record("SceneObjectDrawArray hook install failed", false);
        } else {
            StatusReport::record("CBaseSceneObjectDesc vtable not resolved - world recolor partial", false);
            allResolved = false;
        }

        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(rodata, dataRelRo, text, "CAggregateSceneObjectDesc")) {
            if (hooks.aggregateSceneObjectDrawArray.install(vtable->addressPoint + sizeof(void*), reinterpret_cast<std::uintptr_t>(&onAggregateSceneObjectDrawArray)))
                StatusReport::record("AggregateSceneObjectDrawArray hook (libscenesystem CAggregateSceneObjectDesc slot 1)", true);
            else
                StatusReport::record("AggregateSceneObjectDrawArray hook install failed", false);
        } else {
            StatusReport::record("CAggregateSceneObjectDesc vtable not resolved - world recolor partial", false);
            allResolved = false;
        }
    }

    // Flash overlay: libclient.so RenderingPipelineCsgoPostHud vtable slot 0, patched on demand
    // by Removals (install()/uninstall() only manage the always-on pass-through hooks).
    {
        const DynamicLibrary clientLibrary{cs2::CLIENT_DLL};
        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(clientLibrary.getVmtFinderParams().rodataSection, clientLibrary.getVmtFinderParams().dataRelRoSection, clientLibrary.getCodeSection(), "CRenderingPipelineCsgoPostHud")) {
            flashOverlaySlot = vtable->addressPoint;
            StatusReport::record("RenderingPipelineCsgoPostHud vtable resolved (slot 0 = add_layers)", true);
        } else {
            StatusReport::record("RenderingPipelineCsgoPostHud vtable not resolved - flash removal inactive", false);
            allResolved = false;
        }
    }

    // First-person legs: libclient.so function referencing the "FirstpersonLegsPrepass" string.
    {
        const DynamicLibrary clientLibrary{cs2::CLIENT_DLL};
        if (const auto function = string_xref::findFunctionStart(clientLibrary.getStringLiteralsSection(), clientLibrary.getCodeSection(), "FirstpersonLegsPrepass")) {
            renderLegsFunction = *function;
            StatusReport::record("First-person legs render function resolved", true);
        } else {
            StatusReport::record("Legs render function not resolved - legs removal inactive", false);
            allResolved = false;
        }
    }

    return allResolved;
}


// Removals feature toggles (idempotent; no-ops when the anchor never resolved).
inline void setFlashOverlayRemoved(bool removed) noexcept
{
    if (removed && !flashOverlaySlot)
        return;
    if (removed && !hooks.addLayersPostHud.isActive())
        (void)hooks.addLayersPostHud.install(flashOverlaySlot, reinterpret_cast<std::uintptr_t>(&addLayersPostHudSkipStub));
    else if (!removed && hooks.addLayersPostHud.isActive())
        hooks.addLayersPostHud.restore();
}

inline void setLegsRenderRemoved(bool removed) noexcept
{
    if (removed && !renderLegsFunction)
        return;
    if (removed && !hooks.renderLegs.isActive())
        (void)hooks.renderLegs.install(renderLegsFunction);
    else if (!removed && hooks.renderLegs.isActive())
        hooks.renderLegs.restore();
}

inline void uninstall() noexcept
{
    setFlashOverlayRemoved(false);
    setLegsRenderRemoved(false);
    hooks.particlesDrawArray.restore();
    hooks.lightBinnerProcessLights.restore();
    hooks.skyBoxDrawArray.restore();
    hooks.sceneObjectDrawArray.restore();
    hooks.aggregateSceneObjectDrawArray.restore();
    hooks.addLayersPostHud.restore();
    hooks.renderLegs.restore();
}

}
