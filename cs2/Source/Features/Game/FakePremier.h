#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/PlayerRankingData.h>
#include <Features/Game/FakePremierConfigVariables.h>
#include <GameClient/Entities/PlayerController.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
















template <typename HookContext>
class FakePremier {
public:
    explicit FakePremier(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        const bool premier = GET_CONFIG_VAR(FakePremierEnabled);
        const bool wingman = GET_CONFIG_VAR(FakeWingmanEnabled);
        // The controller carries ONE ranking slot - wingman takes the slot when both are
        // enabled (the two features are mutually exclusive by construction).
        const bool wingmanMode = wingman;

        applyRankingBlock(premier, wingmanMode);
        applyRankTree(premier, wingmanMode);

        auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(hookContext.localPlayerController().baseEntity());
        if (!controllerEntity)
            return;

        const auto rankingOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRanking");
        const auto rankTypeOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveRankType");
        const auto winsOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iCompetitiveWins");
        if (!rankingOffset.has_value() || *rankingOffset <= 0 || !rankTypeOffset.has_value() || *rankTypeOffset <= 0 || !winsOffset.has_value() || *winsOffset <= 0)
            return;

        cachedController = controllerEntity;
        cachedRankingOffset = *rankingOffset;
        cachedRankTypeOffset = *rankTypeOffset;
        cachedWinsOffset = *winsOffset;

        if (premier || wingmanMode) {
            captureOriginal(controllerEntity, *rankingOffset, *rankTypeOffset, *winsOffset);
            apply(controllerEntity, *rankingOffset, *rankTypeOffset, *winsOffset, premier, wingmanMode);
        } else if (hasOriginal) {
            restore(controllerEntity, *rankingOffset, *rankTypeOffset, *winsOffset);
        }
    }

    void onUnload() const noexcept
    {
        if (hasOriginal && cachedController && cachedRankingOffset > 0 && cachedRankTypeOffset > 0 && cachedWinsOffset > 0)
            restore(cachedController, cachedRankingOffset, cachedRankTypeOffset, cachedWinsOffset);
        restoreRankingBlock();
    }

private:
    
    
    
    
    
    
    
    void applyRankingBlock(bool premier, bool wingmanMode) const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;

        void* sub{};
        std::memcpy(&sub, block + cs2::PlayerRankingData::kRankingSubstructOffset, sizeof(sub));
        cachedRankingSub = sub;

        if (premier || wingmanMode) {
            if (!sub)
                return; 
            auto* const subBytes = static_cast<std::byte*>(sub);
            const auto rankingBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            const auto winsBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructWinsOffset;
            std::int32_t currentRanking{};
            std::memcpy(&currentRanking, rankingBytes, sizeof(currentRanking));
            std::int32_t currentWins{};
            std::memcpy(&currentWins, winsBytes, sizeof(currentWins));
            const std::int32_t score = wingmanMode
                ? static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank))
                : static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore));
            // Premier mode: also satisfy the >= 10 competitive-wins gate or the UI keeps
            // the skill group hidden regardless of the score.
            const std::int32_t wins = premier && GET_CONFIG_VAR(FakePremierWins)
                ? static_cast<std::int32_t>(GET_CONFIG_VAR(PremierWins))
                : currentWins;
            
            if (!(hasBlockOriginal && currentRanking == score && (!premier || wins == currentWins))) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                blockOriginalHadFlag = (flags & cs2::PlayerRankingData::kRankingPresentFlag) != 0;
                blockOriginalRanking = currentRanking;
                blockOriginalWins = currentWins;
                hasBlockOriginal = true;
            }
            std::memcpy(rankingBytes, &score, sizeof(score));
            if (premier)
                std::memcpy(winsBytes, &wins, sizeof(wins));
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags |= cs2::PlayerRankingData::kRankingPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        } else if (hasBlockOriginal && cachedRankingSub) {
            auto* const subBytes = static_cast<std::byte*>(cachedRankingSub);
            const auto rankingBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructRankingOffset;
            const auto winsBytes = subBytes + cs2::PlayerRankingData::kRankingSubstructWinsOffset;
            std::int32_t currentRanking{};
            std::memcpy(&currentRanking, rankingBytes, sizeof(currentRanking));
            std::int32_t currentWins{};
            std::memcpy(&currentWins, winsBytes, sizeof(currentWins));
            if (currentRanking == static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore))
                || currentRanking == static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank))) {
                std::memcpy(rankingBytes, &blockOriginalRanking, sizeof(blockOriginalRanking));
                std::memcpy(winsBytes, &blockOriginalWins, sizeof(blockOriginalWins));
            }
            if (!blockOriginalHadFlag) {
                std::uint32_t flags{};
                std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
                flags &= ~cs2::PlayerRankingData::kRankingPresentFlag;
                std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
            }
            hasBlockOriginal = false;
        }
    }

    // The GC rank cache tree (see cs2::GcRankCacheTree): what the display actually reads.
    // Faithful port of the reference Lua's find/insert/write - the tree only carries keys
    // the GC sent, so a missing premier/wingman node needs the detached-slot re-link.
    void applyRankTree(bool premier, bool wingmanMode) const noexcept
    {
        using Tree = cs2::GcRankCacheTree;
        auto&& ps = hookContext.patternSearchResults();
        auto* const baseSlot = static_cast<std::byte*>(ps.template get<RankCacheBasePointer>());
        auto* const rootSlot = static_cast<std::byte*>(ps.template get<RankCacheRootPointer>());
        auto* const flagSlot = static_cast<std::byte*>(ps.template get<RankCacheFlagPointer>());
        if (!baseSlot || !rootSlot || !flagSlot)
            return;

        void* base{};
        std::memcpy(&base, baseSlot, sizeof(base));
        std::uint32_t flag{};
        std::memcpy(&flag, flagSlot, sizeof(flag));
        if (!base || (flag & Tree::kFlagMask) == 0)
            return;
        auto* const nodes = static_cast<std::byte*>(base);
        std::int32_t rootIdx{};
        std::memcpy(&rootIdx, rootSlot, sizeof(rootIdx));
        if (rootIdx == Tree::kInvalidIndex)
            return;

        const bool spoofWins = GET_CONFIG_VAR(FakePremierWins);
        const std::int32_t winsValue = static_cast<std::int32_t>(GET_CONFIG_VAR(PremierWins));
        const std::int32_t ratingValue = wingmanMode
            ? static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank))
            : static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore));

        // Main node: wingman (key 7) in wingman mode, premier (key 11) in premier mode.
        const std::int32_t mainKey = wingmanMode ? Tree::kRankTypeWingman : Tree::kRankTypePremier;
        auto* node = findTreeNode(nodes, rootIdx, mainKey);
        if (!node)
            node = insertTreeNode(nodes, rootIdx, mainKey);
        if (node) {
            std::memcpy(node + Tree::kNodeRatingOffset, &ratingValue, sizeof(ratingValue));
            if (spoofWins)
                std::memcpy(node + Tree::kNodeWinsOffset, &winsValue, sizeof(winsValue));
        }

        // Premier display gate: the >= 10 competitive-wins check reads the competitive
        // node's (key 12) wins field - satisfy it WITHOUT touching the classic comp
        // rating (we have no comp-rank slider; writing the premier score there would
        // show a nonsense classic badge).
        if (premier) {
            auto* compNode = findTreeNode(nodes, rootIdx, Tree::kRankTypeCompetitive);
            if (!compNode)
                compNode = insertTreeNode(nodes, rootIdx, Tree::kRankTypeCompetitive);
            if (compNode && spoofWins)
                std::memcpy(compNode + Tree::kNodeWinsOffset, &winsValue, sizeof(winsValue));
        }
    }

    // Returns the NODE BASE (add GcRankCacheTree offsets at the use site - the first
    // version returned node+84 and the caller added +84 AGAIN, landing writes past the
    // 136-byte node into the next slot's fields; live-verified 2026-10-10).
    [[nodiscard]] static std::byte* findTreeNode(std::byte* nodes, std::int32_t idx, std::int32_t wanted) noexcept
    {
        using Tree = cs2::GcRankCacheTree;
        for (int step = 0; step < Tree::kMaxWalkSteps; ++step) {
            auto* const node = nodes + Tree::kNodeStride * idx;
            std::int32_t key{};
            std::memcpy(&key, node + Tree::kNodeKeyOffset, sizeof(key));
            if (key == wanted)
                return node;
            std::int32_t next{};
            std::memcpy(&next, node + (wanted > key ? Tree::kNodeRightOffset : Tree::kNodeLeftOffset), sizeof(next));
            if (next == Tree::kInvalidIndex)
                return nullptr;
            idx = next;
        }
        return nullptr;
    }

    // Re-links a detached stale slot for a missing key (the GC prunes empty entries
    // between snapshots). Mirrors the reference Lua's insert: complete the leaf before
    // linking, so readers only ever see a valid leaf or the old tree. Returns the NODE
    // BASE like findTreeNode - the caller stamps rating/wins.
    [[nodiscard]] static std::byte* insertTreeNode(std::byte* nodes, std::int32_t rootIdx, std::int32_t wanted) noexcept
    {
        using Tree = cs2::GcRankCacheTree;
        std::byte* link = nullptr;
        std::int32_t idx = rootIdx;
        for (int step = 0; step < Tree::kMaxWalkSteps; ++step) {
            auto* const node = nodes + Tree::kNodeStride * idx;
            std::int32_t key{};
            std::memcpy(&key, node + Tree::kNodeKeyOffset, sizeof(key));
            if (key == wanted)
                return node;
            link = node + (wanted > key ? Tree::kNodeRightOffset : Tree::kNodeLeftOffset);
            std::int32_t child{};
            std::memcpy(&child, link, sizeof(child));
            if (child == Tree::kInvalidIndex)
                break;
            idx = child;
        }
        if (!link)
            return nullptr;

        bool seen[Tree::kMaxSlots]{};
        std::int32_t stack[Tree::kMaxWalkSteps * 2]{};
        int stackSize = 0;
        stack[stackSize++] = rootIdx;
        int steps = 0;
        while (stackSize > 0 && steps < Tree::kMaxWalkSteps * 4) {
            ++steps;
            const std::int32_t j = stack[--stackSize];
            if (j == Tree::kInvalidIndex || j < 0 || j >= Tree::kMaxSlots || seen[j])
                continue;
            seen[j] = true;
            auto* const node = nodes + Tree::kNodeStride * j;
            std::int32_t left{}, right{};
            std::memcpy(&left, node + Tree::kNodeLeftOffset, sizeof(left));
            std::memcpy(&right, node + Tree::kNodeRightOffset, sizeof(right));
            if (stackSize + 2 <= static_cast<int>(sizeof(stack) / sizeof(stack[0]))) {
                stack[stackSize++] = left;
                stack[stackSize++] = right;
            }
        }
        for (int i = 0; i < Tree::kDetachedSlotScan; ++i) {
            if (seen[i])
                continue;
            auto* const slot = nodes + Tree::kNodeStride * i;
            std::int32_t key{};
            std::memcpy(&key, slot + Tree::kNodeKeyOffset, sizeof(key));
            if (key != wanted)
                continue;
            const std::int32_t leaf = Tree::kInvalidIndex;
            std::memcpy(slot + Tree::kNodeLeftOffset, &leaf, sizeof(leaf));
            std::memcpy(slot + Tree::kNodeRightOffset, &leaf, sizeof(leaf));
            std::int32_t neutral{};
            std::memcpy(slot + Tree::kNodeRatingOffset, &neutral, sizeof(neutral));
            std::memcpy(slot + Tree::kNodeWinsOffset, &neutral, sizeof(neutral));
            const std::int32_t slotIdx = i;
            std::memcpy(link, &slotIdx, sizeof(slotIdx));
            return slot;
        }
        return nullptr;
    }

    void restoreRankingBlock() const noexcept
    {
        auto* const block = static_cast<std::byte*>(hookContext.patternSearchResults().template get<PlayerRankingDataPointer>());
        if (!block)
            return;
        if (hasBlockOriginal && cachedRankingSub) {
            auto* const subBytes = static_cast<std::byte*>(cachedRankingSub);
            std::memcpy(subBytes + cs2::PlayerRankingData::kRankingSubstructRankingOffset, &blockOriginalRanking, sizeof(blockOriginalRanking));
            std::memcpy(subBytes + cs2::PlayerRankingData::kRankingSubstructWinsOffset, &blockOriginalWins, sizeof(blockOriginalWins));
        }
        if (hasBlockOriginal && !blockOriginalHadFlag) {
            std::uint32_t flags{};
            std::memcpy(&flags, block + cs2::PlayerRankingData::kFlagsOffset, sizeof(flags));
            flags &= ~cs2::PlayerRankingData::kRankingPresentFlag;
            std::memcpy(block + cs2::PlayerRankingData::kFlagsOffset, &flags, sizeof(flags));
        }
        hasBlockOriginal = false;
    }
    void apply(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset, int winsOffset, bool premier, bool wingmanMode) const noexcept
    {
        const auto bytes = reinterpret_cast<std::byte*>(controllerEntity);
        if (wingmanMode) {
            const std::int32_t rank = static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank));
            std::memcpy(bytes + rankingOffset, &rank, sizeof(rank));
            const std::int8_t rankType = kWingmanRankType;
            std::memcpy(bytes + rankTypeOffset, &rankType, sizeof(rankType));
            return;
        }
        const std::int32_t score = GET_CONFIG_VAR(FakePremierScore);
        std::memcpy(bytes + rankingOffset, &score, sizeof(score));
        const std::int8_t premierRankType = kPremierRankType;
        std::memcpy(bytes + rankTypeOffset, &premierRankType, sizeof(premierRankType));
        // The competitive-wins gate on the controller side (schema m_iCompetitiveWins):
        // keep the real value unless the wins spoof is on.
        std::int32_t wins{};
        std::memcpy(&wins, bytes + winsOffset, sizeof(wins));
        const std::int32_t spoofedWins = GET_CONFIG_VAR(FakePremierWins)
            ? static_cast<std::int32_t>(GET_CONFIG_VAR(PremierWins))
            : wins;
        std::memcpy(bytes + winsOffset, &spoofedWins, sizeof(spoofedWins));
    }

    
    
    
    void captureOriginal(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset, int winsOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<const std::byte*>(controllerEntity);
        std::int32_t ranking{};
        std::memcpy(&ranking, bytes + rankingOffset, sizeof(ranking));
        std::int8_t rankType{};
        std::memcpy(&rankType, bytes + rankTypeOffset, sizeof(rankType));
        std::int32_t wins{};
        std::memcpy(&wins, bytes + winsOffset, sizeof(wins));
        if (hasOriginal && rankType == kPremierRankType && ranking == static_cast<std::int32_t>(GET_CONFIG_VAR(FakePremierScore)))
            return;
        if (hasOriginal && rankType == kWingmanRankType && ranking == static_cast<std::int32_t>(GET_CONFIG_VAR(WingmanRank)))
            return;

        originalRanking = ranking;
        originalRankType = rankType;
        originalWins = wins;
        hasOriginal = true;
    }

    void restore(cs2::C_BaseEntity* controllerEntity, int rankingOffset, int rankTypeOffset, int winsOffset) const noexcept
    {
        const auto bytes = reinterpret_cast<std::byte*>(controllerEntity);
        std::memcpy(bytes + rankingOffset, &originalRanking, sizeof(originalRanking));
        std::memcpy(bytes + rankTypeOffset, &originalRankType, sizeof(originalRankType));
        std::memcpy(bytes + winsOffset, &originalWins, sizeof(originalWins));
        hasOriginal = false;
    }

    static constexpr std::int8_t kPremierRankType = 11; 
    // The game's own rankType jump table (0x1EE5540 on 11106093): 7=Wingman, 8=ScrimComp2v2,
    // 9=ScrimComp5v5, 10=DangerZone, 11=Premier, 12=Competitive.
    static constexpr std::int8_t kWingmanRankType = 7;
    // The premier skill group is gated behind >= 10 competitive wins - below that the UI
    // hides the rank no matter what the score says.
    static constexpr std::int32_t kMinPremierWins = 10;

    inline static cs2::C_BaseEntity* cachedController{nullptr};
    inline static int cachedRankingOffset{0};
    inline static int cachedRankTypeOffset{0};
    inline static int cachedWinsOffset{0};
    inline static std::int32_t originalRanking{};
    inline static std::int8_t originalRankType{};
    inline static std::int32_t originalWins{};
    inline static bool hasOriginal{false};

    inline static void* cachedRankingSub{nullptr};
    inline static std::int32_t blockOriginalRanking{};
    inline static std::int32_t blockOriginalWins{};
    inline static bool blockOriginalHadFlag{false};
    inline static bool hasBlockOriginal{false};

    HookContext& hookContext;
};
