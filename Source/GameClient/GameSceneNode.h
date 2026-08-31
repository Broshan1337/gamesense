#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CGameSceneNode.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <MemoryPatterns/PatternTypes/GameSceneNodePatternTypes.h>
#include <Utils/Optional.h>
#include <Utils/RetAddrSpoofer.h>

template <typename HookContext>
class BaseEntity;

template <typename HookContext>
class GameSceneNode {
public:
    GameSceneNode(HookContext& hookContext, cs2::CGameSceneNode* gameSceneNode) noexcept
        : hookContext{&hookContext}
        , gameSceneNode{gameSceneNode}
    {
    }

    explicit operator bool() const noexcept
    {
        return gameSceneNode != nullptr;
    }

    // Raw node pointer for code that walks fixed offsets itself (lag-comp records, hitbox query).
    [[nodiscard]] cs2::CGameSceneNode* raw() const noexcept
    {
        return gameSceneNode;
    }

    [[nodiscard]] decltype(auto) owner() const noexcept
    {
        return hookContext->template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(hookContext->patternSearchResults().template get<OffsetToGameSceneNodeOwner>().of(gameSceneNode).valueOr(nullptr)));
    }

    template <typename F>
    void forEachChild(F f) const noexcept
    {
        for (auto&& child = this->child(); child; child = child.nextSibling())
            f(child);
    }

    // EXPERIMENTAL - CGameSceneNode's own vtable slot 26 (byte offset 208), found via IDA RTTI
    // (_ZTV14CGameSceneNode) plus a structural cross-check: the entity-level PostDataUpdate
    // this project already found by string landmark (a large per-entity function handling
    // interpolation/hierarchy refresh) calls this exact function (sub_16A1C90) directly, with
    // its own (this, updateType, outputList) arguments passed through unchanged, several times
    // - the classic "call the base class's own virtual" pattern a derived PostDataUpdate uses
    // to chain into its base. Strong circumstantial evidence this IS
    // CGameSceneNode::PostDataUpdate(), matching a reference implementation (a friend's
    // production Windows skin changer) that calls exactly this - weapon->m_pGameSceneNode()->
    // PostDataUpdate() - as the final step after the composite-material rebuild, a call this
    // project never made before. Internally it clears a block of per-node dirty-state bits
    // (offset+261) and, depending on which bits were already set, conditionally re-derives
    // interpolation/render state and dispatches through the node's own vtable+128 - every
    // branch only touches this instance's own fixed-offset fields plus its own vtable, so an
    // untested updateType value should be behaviorally inert at worst, not unsafe. updateType=0
    // (the conservative "normal update, not creation" guess, matching classic Source's
    // DATA_UPDATE_DATATABLE_CHANGED) is a placeholder pending live confirmation of the real
    // holding-animation effect and the correct flag value.
    void postDataUpdate(unsigned int updateType = 0) const noexcept
    {
        if (!gameSceneNode)
            return;

        const auto vtable = *reinterpret_cast<void* const*>(gameSceneNode);
        if (!vtable)
            return;

        const auto postDataUpdateFn = *reinterpret_cast<void* const*>(reinterpret_cast<const unsigned char*>(vtable) + 208);
        if (!postDataUpdateFn)
            return;

        using PostDataUpdateFn = void (*)(cs2::CGameSceneNode*, unsigned int);
        RetAddrSpoofer::spoof(reinterpret_cast<PostDataUpdateFn>(postDataUpdateFn))(gameSceneNode, updateType);
    }

    // World-space position of a skeleton bone, for aim assist. An animated entity's CGameSceneNode
    // is really a CSkeletonInstance, which embeds a CModelState at m_modelState (schema-resolved
    // below). The live bone transforms hang off that model state as a runtime array: a uint32
    // bone_count and a pointer to `bone_count` 32-byte entries (pos: vec3, scale: float,
    // rotation: vec4 - world position first). Those two fields are NOT in the schema (the first
    // schema field, m_hModel, sits above them at 0xA0), so their offsets inside CModelState are
    // hardcoded from the internal CS2 base (0x5C count, 0x80 array) and MUST be re-checked in-game
    // after a game update. Everything else is schema-resolved or guarded. Returns {} for a
    // non-skeletal node, an out-of-range index, or an implausible bone_count (a wrong offset).
    [[nodiscard]] Optional<cs2::Vector> bonePosition(int boneIndex) const noexcept
    {
        if (!gameSceneNode || boneIndex < 0)
            return {};

        const auto modelStateOffset = hookContext->schemaSystem().getFieldOffset("CSkeletonInstance", "m_modelState");
        if (!modelStateOffset.has_value() || *modelStateOffset <= 0)
            return {};

        auto* const modelState = reinterpret_cast<std::byte*>(gameSceneNode) + *modelStateOffset;

        std::uint32_t boneCount{};
        std::memcpy(&boneCount, modelState + kModelStateBoneCountOffset, sizeof(boneCount));
        if (boneCount == 0 || boneCount > kMaxSaneBoneCount || static_cast<std::uint32_t>(boneIndex) >= boneCount)
            return {};

        std::byte* bones{};
        std::memcpy(&bones, modelState + kModelStateBonesOffset, sizeof(bones));
        if (!bones)
            return {};

        cs2::Vector position{};
        std::memcpy(&position, bones + static_cast<std::ptrdiff_t>(boneIndex) * kBoneStride, sizeof(position));
        return position;
    }

private:
    // Non-schema offsets inside CModelState (see bonePosition). Build-specific - re-check in-game.
    static constexpr std::ptrdiff_t kModelStateBoneCountOffset = 0x5C;
    static constexpr std::ptrdiff_t kModelStateBonesOffset = 0x80;
    static constexpr std::ptrdiff_t kBoneStride = 0x20; // pos(12) + scale(4) + rotation(16)
    static constexpr std::uint32_t kMaxSaneBoneCount = 256;

    [[nodiscard]] decltype(auto) child() const noexcept
    {
        return hookContext->template make<GameSceneNode<HookContext>>(hookContext->patternSearchResults().template get<OffsetToChildGameSceneNode>().of(gameSceneNode).valueOr(nullptr));
    }

    [[nodiscard]] decltype(auto) nextSibling() const noexcept
    {
        return hookContext->template make<GameSceneNode<HookContext>>(hookContext->patternSearchResults().template get<OffsetToNextSiblingGameSceneNode>().of(gameSceneNode).valueOr(nullptr));
    }

    HookContext* hookContext;
    cs2::CGameSceneNode* gameSceneNode;
};
