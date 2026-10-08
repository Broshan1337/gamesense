#pragma once

#include <atomic>









namespace neverlose
{



void loadFonts() noexcept;





inline std::atomic<float> g_menuUiScale{1.0f};

[[nodiscard]] inline float uiScale() noexcept
{
    return g_menuUiScale.load(std::memory_order_relaxed);
}

[[nodiscard]] bool fontsLoaded() noexcept;


void render() noexcept;





void renderGameOverlay() noexcept;




void drawMenuGlow(float menuAlpha) noexcept;



void processDeferred() noexcept;


void restoreFeatureBinds() noexcept;
void cancelKeybindCapture() noexcept;


void beginReveal() noexcept;




void beginDismiss() noexcept;



[[nodiscard]] bool isDismissing() noexcept;

}
