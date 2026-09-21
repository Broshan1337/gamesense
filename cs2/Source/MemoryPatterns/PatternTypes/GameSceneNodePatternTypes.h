#pragma once

#include <cstdint>

#include <CS2/Classes/CGameSceneNode.h>
#include <Utils/FieldOffset.h>
#include <Utils/StrongTypeAlias.h>

template <typename FieldType, typename OffsetType>
using GameSceneNodeOffset = FieldOffset<cs2::CGameSceneNode, FieldType, OffsetType>;

STRONG_TYPE_ALIAS(OffsetToGameSceneNodeOwner, GameSceneNodeOffset<cs2::CGameSceneNode::m_pOwner, std::int8_t>);
STRONG_TYPE_ALIAS(OffsetToChildGameSceneNode, GameSceneNodeOffset<cs2::CGameSceneNode::m_pChild, std::int8_t>);
STRONG_TYPE_ALIAS(OffsetToNextSiblingGameSceneNode, GameSceneNodeOffset<cs2::CGameSceneNode::m_pNextSibling, std::int8_t>);

// CSkeletonInstance mask-setter methods used by the lag-comp record system to force a skeleton
// rebuild before snapshotting the bone cache (velocity-cs2's game_scene_node_set_mesh_group /
// game_scene_node_set_skeleton). Identified on Linux libclient.so 2026-08-20 by disassembly:
// both take (this, u32 mask), clamp the mask with AND 0xFFFFF and OR it into a dirty-mask field,
// then trigger re-evaluation. The skeleton one allocates a unique-in-binary 0x4208-byte stack frame
// (per-bone scratch) and is the only one the game itself calls with the 0x100 mask; the mesh-group
// one special-cases the 0xC0000 streaming bits. Swap them if in-game records show frozen bones.
STRONG_TYPE_ALIAS(GameSceneNodeSetMeshGroupMaskFunction, void (*)(cs2::CGameSceneNode*, std::uint32_t));
STRONG_TYPE_ALIAS(GameSceneNodeSetSkeletonFunction, void (*)(cs2::CGameSceneNode*, std::uint32_t));
