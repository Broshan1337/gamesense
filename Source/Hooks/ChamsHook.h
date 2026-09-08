#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <MemorySearch/TypeinfoVtableResolver.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>

// GeneratePrimitives vtable patches for the enemy chams (see Features/Visuals/Chams/Chams.h).
//
// GeneratePrimitives is the VIRTUAL method at byte offset +0x20 of every SceneObjectDesc vtable
// (slot 4 after the address point), signature (desc, CS2::SceneObject*, ISceneView*,
// CUtlVector<CMeshDrawPrimitive_t>*). Five descriptor classes route the scene's meshes; pawns
// specifically go through CAnimatableSceneObjectDesc (its OWN override, not the inherited base -
// the 2026-08-25 "gen=117462 enemy=0" lesson).
//
// The vtables belong to process-global descriptor singletons we never construct, so like the
// scene render hooks they are patched in place (one qword, saved original, restore on unload).
// Vtable resolution is typeinfo-name based (update-resilient); the 2026-08-20 static VAs are the
// fallback knowledge: Base 0xA0D0B8, MeshBuilder 0xA0CE00, InstancedMesh 0xA0CC50, Aggregate
// 0xA0C4D8, Animatable 0xA0C918 (address points, libscenesystem.so).
namespace chams_hook
{

// The replacement itself lives in EntryPoints.h next to the other scene hook bodies.
void onGeneratePrimitives(void* desc, void* sceneObject, void* sceneView, void* primitives) noexcept;

inline constexpr int kTargetCount = 5;
inline constexpr const char* kClassNames[kTargetCount] = {
    "CBaseSceneObjectDesc",
    "CMeshBuilderSceneObjectDesc",
    "CInstancedMeshSceneObjectDesc",
    "CAggregateSceneObjectDesc",
    "CAnimatableSceneObjectDesc",
};
inline constexpr std::size_t kGeneratePrimitivesSlotOffset = 0x20;

inline VTableSlotPatch patches[kTargetCount];
inline std::uintptr_t addressPoints[kTargetCount]{};

[[nodiscard]] inline std::uintptr_t originalFor(const void* desc) noexcept
{
    if (!desc)
        return 0;
    std::uintptr_t vptr = 0;
    std::memcpy(&vptr, desc, sizeof(vptr));
    for (int i = 0; i < kTargetCount; ++i) {
        if (addressPoints[i] == vptr)
            return patches[i].original();
    }
    return 0;
}

// Which patched descriptor class this call came from (0-4), or -1.
[[nodiscard]] inline int classIndexFor(const void* desc) noexcept
{
    if (!desc)
        return -1;
    std::uintptr_t vptr = 0;
    std::memcpy(&vptr, desc, sizeof(vptr));
    for (int i = 0; i < kTargetCount; ++i) {
        if (addressPoints[i] == vptr)
            return i;
    }
    return -1;
}

inline void uninstall() noexcept
{
    for (int i = 0; i < kTargetCount; ++i)
        patches[i].restore();
}

[[nodiscard]] inline bool install() noexcept
{
    const DynamicLibrary scenesystemLibrary{cs2::SCENESYSTEM_DLL};
    const auto params = scenesystemLibrary.getVmtFinderParams();
    const auto text = scenesystemLibrary.getCodeSection();
    bool allResolved = true;

    for (int i = 0; i < kTargetCount; ++i) {
        const auto vtable = typeinfo_vtable::findPrimaryVtable(params.rodataSection, params.dataRelRoSection, text, kClassNames[i]);
        if (!vtable) {
            StatusReport::record("Chams desc vtable not resolved (pass-through inactive)", false);
            allResolved = false;
            continue;
        }
        addressPoints[i] = vtable->addressPoint;
        if (patches[i].install(vtable->addressPoint + kGeneratePrimitivesSlotOffset, reinterpret_cast<std::uintptr_t>(&onGeneratePrimitives)))
            StatusReport::record("Chams GeneratePrimitives patch installed", true);
        else {
            StatusReport::record("Chams GeneratePrimitives patch failed", false);
            allResolved = false;
        }
    }
    return allResolved;
}

}
