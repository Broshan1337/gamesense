#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <MemorySearch/TypeinfoVtableResolver.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>


















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

        
        if (const auto vtable = typeinfo_vtable::findPrimaryVtable(rodata, dataRelRo, text, "CSkyBoxObjectDesc")) {
            if (hooks.skyBoxDrawArray.install(vtable->addressPoint + sizeof(void*), reinterpret_cast<std::uintptr_t>(&onSkyBoxDrawArray)))
                StatusReport::record("SkyBoxDrawArray hook (libscenesystem CSkyBoxObjectDesc slot 1)", true);
            else
                StatusReport::record("SkyBoxDrawArray hook install failed", false);
        } else {
            StatusReport::record("CSkyBoxObjectDesc vtable not resolved - sky recolor inactive", false);
            allResolved = false;
        }

        
        
        
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
