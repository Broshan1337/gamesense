#pragma once

#include <cstddef>
#include <cstdint>

#include <CS2/Classes/EntitySystem/CEntityIndex.h>
#include <CS2/Classes/EntitySystem/CConcreteEntityList.h>
#include <CS2/Classes/EntitySystem/CGameEntitySystem.h>
#include <Utils/FieldFieldOffset.h>
#include <Utils/FieldOffset.h>
#include <Utils/StrongTypeAlias.h>

template <typename FieldType, typename OffsetType>
using EntitySystemOffset = FieldOffset<cs2::CGameEntitySystem, FieldType, OffsetType>;

STRONG_TYPE_ALIAS(EntitySystemPointer, cs2::CGameEntitySystem**);
// (HighestEntityIndexOffset removed 2026-10-06: it was declared but never resolved by any
// pattern and never consumed - a dead alias. If bounding the entity walk by the engine's own
// live highest-index is ever wanted, re-add it together with the pattern; the field sits at
// CGameEntitySystem+0x2120 per the dumper, far above the 64-chunk list at +0x10.)
STRONG_TYPE_ALIAS(EntityListOffset, EntitySystemOffset<cs2::CConcreteEntityList, std::int8_t>);
STRONG_TYPE_ALIAS(OffsetToEntityClasses, FieldFieldOffset<cs2::CGameEntitySystem, cs2::CGameEntitySystem::EntityClasses, std::int32_t, offsetof(cs2::CGameEntitySystem::EntityClasses, memory)>);
