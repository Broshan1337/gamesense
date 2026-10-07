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

    
    
    
    
    struct BoneTransform {
        cs2::Vector position;
        float rotation[4];
    };

    [[nodiscard]] Optional<BoneTransform> boneTransform(int boneIndex) const noexcept
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

        BoneTransform transform{};
        std::memcpy(&transform.position, bones + static_cast<std::ptrdiff_t>(boneIndex) * kBoneStride, sizeof(transform.position));
        
        
        std::memcpy(&transform.rotation, bones + static_cast<std::ptrdiff_t>(boneIndex) * kBoneStride + 16, sizeof(transform.rotation));
        return transform;
    }

private:
    
    static constexpr std::ptrdiff_t kModelStateBoneCountOffset = 0x5C;
    static constexpr std::ptrdiff_t kModelStateBonesOffset = 0x80;
    static constexpr std::ptrdiff_t kBoneStride = 0x20; 
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
