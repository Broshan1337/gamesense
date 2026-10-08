#include <gtest/gtest.h>
#include <UI/ImGui/MenuInput.h>

namespace {
SDL_Event key(bool repeat = false)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_INSERT;
    event.key.repeat = repeat;
    return event;
}
TEST(MenuInputTests, OnlyPhysicalInsertPressToggles)
{
    EXPECT_TRUE(menu_input::toggles(key()));
    EXPECT_FALSE(menu_input::toggles(key(true)));
    auto up = key();
    up.type = SDL_EVENT_KEY_UP;
    EXPECT_FALSE(menu_input::toggles(up));
}
TEST(MenuInputTests, AltIWorksWithAdditionalModifierBits)
{
    auto event = key();
    event.key.key = SDLK_I;
    event.key.mod = SDL_KMOD_LALT | SDL_KMOD_LSHIFT;
    EXPECT_TRUE(menu_input::toggles(event));
}
TEST(MenuInputTests, MenuCapturesInputAndPreservesWindowAndQuitEvents)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    EXPECT_TRUE(menu_input::captured(event, true));
    EXPECT_FALSE(menu_input::captured(event, false));
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    EXPECT_FALSE(menu_input::captured(event, true));
    event.type = SDL_EVENT_QUIT;
    EXPECT_FALSE(menu_input::captured(event, true));
    EXPECT_TRUE(menu_input::captured(key(), false));
}
TEST(MenuInputTests, DrainsMouseBacklogToInsertInOnePollCall)
{
    SDL_Event output{};
    int calls = 0;
    bool open = true;
    const auto poll = [&](void* events, int, int, unsigned, unsigned) {
        if (++calls > 501)
            return 0;
        auto& event = *static_cast<SDL_Event*>(events);
        event = calls == 501 ? key() : SDL_Event{};
        if (calls != 501)
            event.type = SDL_EVENT_MOUSE_MOTION;
        return 1;
    };
    const auto filter = [&](SDL_Event* events, int count) {
        if (menu_input::toggles(*events))
            open = !open;
        return menu_input::captured(*events, open) ? 0 : count;
    };
    EXPECT_EQ(menu_input::drain(&output, 1, SDL_GETEVENT, 0, ~0u, poll, filter), 0);
    EXPECT_FALSE(open);
    EXPECT_EQ(calls, 502);
}
TEST(MenuInputTests, NullCountOnlyCallDoesNotFilterOrLoop)
{
    int calls = 0, filters = 0;
    const auto poll = [&](void*, int, int, unsigned, unsigned) { ++calls; return 4; };
    const auto filter = [&](SDL_Event*, int) { ++filters; return 0; };
    EXPECT_EQ(menu_input::drain(nullptr, 4, SDL_GETEVENT, 0, ~0u, poll, filter), 4);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(filters, 0);
}
TEST(MenuInputTests, PreservesPartialBatchAndOriginalErrors)
{
    SDL_Event output[2]{};
    const auto filter = [](SDL_Event*, int) { return 1; };
    EXPECT_EQ(menu_input::drain(output, 2, SDL_GETEVENT, 0, ~0u,
        [](void*, int, int, unsigned, unsigned) { return 2; }, filter), 1);
    EXPECT_EQ(menu_input::drain(output, 2, SDL_GETEVENT, 0, ~0u,
        [](void*, int, int, unsigned, unsigned) { return -1; }, filter), -1);
}
}
