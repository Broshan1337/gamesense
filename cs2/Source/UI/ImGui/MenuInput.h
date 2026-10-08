#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

namespace menu_input {
[[nodiscard]] inline bool isToggleKey(const SDL_Event& event) noexcept
{
    return (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        && (event.key.key == SDLK_INSERT
            || ((event.key.mod & SDL_KMOD_LALT) != 0 && event.key.key == SDLK_I));
}
[[nodiscard]] inline bool toggles(const SDL_Event& event) noexcept
{
    return event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && isToggleKey(event);
}
[[nodiscard]] inline bool captured(const SDL_Event& event, bool open) noexcept
{
    if (isToggleKey(event))
        return true;
    if (!open)
        return false;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    default:
        return false;
    }
}

// Keep draining batches consumed by the menu: a zero result ends SDL_PollEvent's loop.
// Count-only calls and PEEK/ADD calls must never consume events.
template <typename Poll, typename Filter>
[[nodiscard]] int drain(void* events, int capacity, int action, unsigned minType,
    unsigned maxType, Poll poll, Filter filter) noexcept
{
    for (;;) {
        const int count = poll(events, capacity, action, minType, maxType);
        if (action != SDL_GETEVENT || !events || count <= 0)
            return count;
        const int kept = filter(static_cast<SDL_Event*>(events), count);
        if (kept > 0)
            return kept;
    }
}
}
