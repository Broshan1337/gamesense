#pragma once






































namespace VulkanHook
{





[[nodiscard]] bool tryInstall() noexcept;


void restorePointers() noexcept;




void destroyResources() noexcept;







void waitUntilDeviceIdle() noexcept;






namespace music_texture
{
void request(const void* pixelsRgba, int width, int height) noexcept;
[[nodiscard]] void* query() noexcept;
void release() noexcept;
}

namespace avatar_texture
{



void request(const void* pixelsRgba, int width, int height) noexcept;


[[nodiscard]] void* query() noexcept;

}



namespace logo_texture
{



void request(const void* pixelsRgba, int width, int height) noexcept;


[[nodiscard]] void* query() noexcept;

}





namespace shadow_texture
{


constexpr int kStampSize = 128; 
constexpr int kMargin = 24;     


[[nodiscard]] void* query() noexcept;

}






namespace lua_texture
{

inline constexpr int kMaxTextures = 8;



void request(int index, const void* pixelsRgba, int width, int height) noexcept;


[[nodiscard]] void* query(int index) noexcept;



void release(int index) noexcept;

}

}
