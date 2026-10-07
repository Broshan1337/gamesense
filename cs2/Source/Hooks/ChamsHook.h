#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <MemorySearch/TypeinfoVtableResolver.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/StatusReport.h>














namespace chams_hook
{


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
