#include <cstdlib>
#include <gtest/gtest.h>
#include <UI/ImGui/Neverlose/PopupSurface.h>
namespace {
class PopupSurfaceTest : public testing::Test {
protected:
    void SetUp() override {
        ImGui::SetAllocatorFunctions([](std::size_t n, void*) { return std::malloc(n); }, [](void* p, void*) { std::free(p); });
        ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {640, 720};
        io.Fonts->AddFontDefault();
        unsigned char* pixels; int w, h; io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    }
    void TearDown() override { ImGui::DestroyContext(); }
    bool frame(bool down) {
        auto& io = ImGui::GetIO(); io.AddMousePosEvent(250, 509); io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20}); ImGui::SetNextWindowSize({200, 200});
        ImGui::Begin("shell", nullptr, ImGuiWindowFlags_NoDecoration);
        auto* draw = ImGui::GetWindowDrawList(); bool clicked = false;
        {
            popup_surface::PopupSurface popup{4, {180, 80}, {160, 450}, draw};
            for (int row = 0; row < 17; ++row) {
                ImGui::PushID(row); ImGui::SetCursorScreenPos({184, 83 + row * 26.0f});
                const bool hit = ImGui::InvisibleButton("option", {152, 22});
                if (row == 16) clicked = hit;
                ImGui::PopID();
            }
        }
        ImGui::End(); ImGui::Render(); return clicked;
    }
};
}
TEST_F(PopupSurfaceTest, SeventeenthOptionIsClickableOutsideMainWindow) {
    frame(false); frame(false);
    EXPECT_FALSE(frame(true)); EXPECT_TRUE(frame(false));
}
TEST_F(PopupSurfaceTest, PositionClampsToDisplayRatherThanMenuShell) {
    ImGui::NewFrame();
    const auto p = popup_surface::popupPosition({600, 680}, {160, 450});
    EXPECT_FLOAT_EQ(p.x, 476); EXPECT_FLOAT_EQ(p.y, 266);
    ImGui::EndFrame();
}
