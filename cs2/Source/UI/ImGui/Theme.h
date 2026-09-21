#pragma once

#include <imgui.h>

// Anti-purple theme, values carried over from the UI donor (somecs2baseforlinux Theme.cpp).
// Warm sand accent on near-black surfaces; kept as pure data so it stays trivial to re-tune.
// (The GTK recolor experiment was reverted - design metrics below stayed GTK-shaped instead.)
namespace gui_theme
{

inline void apply() noexcept
{
    constexpr ImU32 BACKGROUND = IM_COL32(10, 10, 10, 255);
    constexpr ImU32 BACKGROUND2 = IM_COL32(18, 18, 18, 255);
    constexpr ImU32 BACKGROUND3 = IM_COL32(23, 23, 23, 255);
    constexpr ImU32 ACCENT = IM_COL32(200, 206, 164, 255);
    constexpr ImU32 BORDER = IM_COL32(56, 56, 56, 255);
    constexpr ImU32 BORDER2 = IM_COL32(56, 56, 56, 255);

    auto scaled = [](ImU32 color, int alpha) {
        const ImU32 withAlpha = (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
        return ImGui::ColorConvertU32ToFloat4(withAlpha);
    };

    auto solid = [](ImU32 color) {
        return ImGui::ColorConvertU32ToFloat4(color);
    };

    auto& colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_Text] = solid(IM_COL32_WHITE);
    colors[ImGuiCol_TextDisabled] = solid(BORDER2);

    colors[ImGuiCol_WindowBg] = scaled(BACKGROUND, 240);
    colors[ImGuiCol_ChildBg] = solid(IM_COL32_BLACK_TRANS);
    colors[ImGuiCol_PopupBg] = scaled(BACKGROUND, 240);

    colors[ImGuiCol_Border] = scaled(BORDER, 128);
    colors[ImGuiCol_BorderShadow] = solid(IM_COL32_BLACK_TRANS);

    colors[ImGuiCol_FrameBg] = solid(BACKGROUND3);
    colors[ImGuiCol_FrameBgHovered] = scaled(ACCENT, 180);
    colors[ImGuiCol_FrameBgActive] = solid(ACCENT);

    colors[ImGuiCol_TitleBg] = solid(BACKGROUND2);
    colors[ImGuiCol_TitleBgActive] = solid(BACKGROUND2);
    colors[ImGuiCol_TitleBgCollapsed] = solid(BACKGROUND2);

    colors[ImGuiCol_MenuBarBg] = solid(BACKGROUND3);

    colors[ImGuiCol_ScrollbarBg] = scaled(BACKGROUND3, 100);
    colors[ImGuiCol_ScrollbarGrab] = solid(BORDER);
    colors[ImGuiCol_ScrollbarGrabHovered] = scaled(ACCENT, 200);
    colors[ImGuiCol_ScrollbarGrabActive] = solid(ACCENT);

    colors[ImGuiCol_CheckMark] = solid(ACCENT);

    colors[ImGuiCol_SliderGrab] = solid(BORDER2);
    colors[ImGuiCol_SliderGrabActive] = solid(BORDER2);

    colors[ImGuiCol_Button] = solid(BACKGROUND2);
    colors[ImGuiCol_ButtonHovered] = solid(BACKGROUND3);
    colors[ImGuiCol_ButtonActive] = solid(BACKGROUND2);

    colors[ImGuiCol_Header] = solid(BACKGROUND2);
    colors[ImGuiCol_HeaderHovered] = scaled(ACCENT, 180);
    colors[ImGuiCol_HeaderActive] = solid(ACCENT);

    colors[ImGuiCol_Separator] = solid(BORDER);
    colors[ImGuiCol_SeparatorHovered] = solid(BORDER);
    colors[ImGuiCol_SeparatorActive] = solid(BORDER);

    colors[ImGuiCol_ResizeGrip] = solid(IM_COL32_BLACK_TRANS);
    colors[ImGuiCol_ResizeGripHovered] = solid(BORDER2);
    colors[ImGuiCol_ResizeGripActive] = solid(BORDER2);

    colors[ImGuiCol_Tab] = scaled(BACKGROUND3, 140);
    colors[ImGuiCol_TabHovered] = solid(ACCENT);
    colors[ImGuiCol_TabActive] = scaled(ACCENT, 180);
    colors[ImGuiCol_TabUnfocused] = solid(BACKGROUND3);
    colors[ImGuiCol_TabUnfocusedActive] = solid(BACKGROUND3);

    colors[ImGuiCol_TableHeaderBg] = solid(BACKGROUND3);
    colors[ImGuiCol_TableRowBgAlt] = scaled(BACKGROUND2, 128);
    colors[ImGuiCol_TableBorderLight] = solid(BORDER);

    colors[ImGuiCol_DragDropTarget] = solid(ACCENT);
    colors[ImGuiCol_NavCursor] = solid(ACCENT);

    colors[ImGuiCol_ModalWindowDimBg] = scaled(BACKGROUND, 128);

    auto& style = ImGui::GetStyle();
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = style.FrameBorderSize = style.TabBorderSize = 1.0f;
    style.WindowRounding = style.ChildRounding = style.PopupRounding = 12.0f;
    style.FrameRounding = style.GrabRounding = style.TabRounding = 7.0f;
    style.WindowTitleAlign.x = 0.5f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.TabBarBorderSize = 0.0f;
    style.FramePadding.x = 10.0f;
}

}
