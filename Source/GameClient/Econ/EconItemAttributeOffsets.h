#pragma once

#include <cstdint>

#include <CS2/Classes/CAttributeContainer.h>
#include <CS2/Classes/CAttributeList.h>
#include <CS2/Classes/CEconItemView.h>
#include <CS2/Classes/Entities/C_EconEntity.h>
#include <Utils/FieldOffset.h>

// Resolved once (via SchemaSystem) and cached for the lifetime of the process. This is
// the C_EconEntity -> C_AttributeContainer -> C_EconItemView -> CAttributeList ->
// m_Attributes chain that drives the live gun-skin render path - unlike the fallback
// fields (see EconEntityOffsets), which only affect the menu/preview render path.
struct EconItemAttributeOffsets {
    explicit EconItemAttributeOffsets(auto&& schemaSystem) noexcept
        : attributeManager{resolve(schemaSystem, "C_EconEntity", "m_AttributeManager")}
        , item{resolve(schemaSystem, "C_AttributeContainer", "m_Item")}
        , attributeList{resolve(schemaSystem, "C_EconItemView", "m_AttributeList")}
        , networkedDynamicAttributes{resolve(schemaSystem, "C_EconItemView", "m_NetworkedDynamicAttributes")}
        , itemDefinitionIndex{resolve(schemaSystem, "C_EconItemView", "m_iItemDefinitionIndex")}
        , entityQuality{resolve(schemaSystem, "C_EconItemView", "m_iEntityQuality")}
        , attributes{resolve(schemaSystem, "CAttributeList", "m_Attributes")}
        , accountID{resolve(schemaSystem, "C_EconItemView", "m_iAccountID")}
        , disallowSOC{resolve(schemaSystem, "C_EconItemView", "m_bDisallowSOC")}
        , restoreCustomMaterialAfterPrecache{resolve(schemaSystem, "C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache")}
        , itemIDHigh{resolve(schemaSystem, "C_EconItemView", "m_iItemIDHigh")}
        , itemIDLow{resolve(schemaSystem, "C_EconItemView", "m_iItemIDLow")}
        , initialized{resolve(schemaSystem, "C_EconItemView", "m_bInitialized")}
        , originalOwnerXuidLow{resolve(schemaSystem, "C_EconEntity", "m_OriginalOwnerXuidLow")}
    {
    }

    FieldOffset<cs2::C_EconEntity, cs2::C_EconEntity::m_AttributeManager, std::int32_t> attributeManager;
    FieldOffset<cs2::CAttributeContainer, cs2::CAttributeContainer::m_Item, std::int32_t> item;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_AttributeList, std::int32_t> attributeList;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_NetworkedDynamicAttributes, std::int32_t> networkedDynamicAttributes;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_iItemDefinitionIndex, std::int32_t> itemDefinitionIndex;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_iEntityQuality, std::int32_t> entityQuality;
    FieldOffset<cs2::CAttributeList, cs2::CAttributeList::m_Attributes, std::int32_t> attributes;
    // Added to faithfully port the reference's full ProcessKnife field-write sequence (m_paint_kit/
    // m_wear/m_seed's siblings), after two cheaper animation-fix hypotheses (SetModel presence,
    // an ownership-toggle nudge) were both tested live and disproven - the reference is confirmed
    // to work correctly, so a real, still-missing field write is the leading remaining theory.
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_iAccountID, std::int32_t> accountID;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_bDisallowSOC, std::int32_t> disallowSOC;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_bRestoreCustomMaterialAfterPrecache, std::int32_t> restoreCustomMaterialAfterPrecache;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_iItemIDHigh, std::int32_t> itemIDHigh;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_iItemIDLow, std::int32_t> itemIDLow;
    FieldOffset<cs2::CEconItemView, cs2::CEconItemView::m_bInitialized, std::int32_t> initialized;
    FieldOffset<cs2::C_EconEntity, cs2::C_EconEntity::m_OriginalOwnerXuidLow, std::int32_t> originalOwnerXuidLow;

    [[nodiscard]] bool isFullyResolved() const noexcept
    {
        return attributeManager && item && attributeList && networkedDynamicAttributes
            && itemDefinitionIndex && entityQuality && attributes && accountID && disallowSOC
            && restoreCustomMaterialAfterPrecache && itemIDHigh && itemIDLow && initialized
            && originalOwnerXuidLow;
    }

private:
    [[nodiscard]] static std::int32_t resolve(auto&& schemaSystem, const char* className, const char* fieldName) noexcept
    {
        return schemaSystem.getFieldOffset(className, fieldName).value_or(0);
    }
};
