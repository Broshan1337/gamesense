#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_Inferno.h>
#include <Utils/FieldOffset.h>
#include <Utils/StrongTypeAlias.h>

template <typename FieldType, typename OffsetType>
using InfernoOffset = FieldOffset<cs2::C_Inferno, FieldType, OffsetType>;

STRONG_TYPE_ALIAS(OffsetToFireEffectTickBegin, InfernoOffset<cs2::C_Inferno::m_nFireEffectTickBegin, std::int32_t>);
STRONG_TYPE_ALIAS(OffsetToFireLifetime, InfernoOffset<cs2::C_Inferno::m_nFireLifetime, std::int32_t>);
