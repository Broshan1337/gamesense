#pragma once
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
namespace popup_surface {
// Each popover owns an ImGui window: drawing AND hit testing may extend
// beyond the menu shell. Keep it on the display, not within the parent clip.
inline ImVec2 popupPosition(ImVec2 desired, ImVec2 size) noexcept
{
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 lo = viewport->Pos + ImVec2(4, 4);
    const ImVec2 hi = viewport->Pos + viewport->Size - size - ImVec2(4, 4);
    return {ImClamp(desired.x, lo.x, ImMax(lo.x, hi.x)),
        ImClamp(desired.y, lo.y, ImMax(lo.y, hi.y))};
}

struct PopupSurface {
    PopupSurface(int kind, ImVec2 p, ImVec2 size, ImDrawList*& draw) noexcept
    {
        char name[32];
        std::snprintf(name, sizeof(name), "##ns_popover_%d", kind);
        ImGui::SetNextWindowPos(p - ImVec2(2, 2));
        ImGui::SetNextWindowSize(size + ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        draw = ImGui::GetWindowDrawList();
        // Allow shadows to extend outside the popup's interactive window.
        draw->PushClipRect(ImGui::GetMainViewport()->Pos,
            ImGui::GetMainViewport()->Pos + ImGui::GetMainViewport()->Size, false);
    }
    ~PopupSurface() { ImGui::GetWindowDrawList()->PopClipRect(); ImGui::End(); ImGui::PopStyleVar(); }
};

}
