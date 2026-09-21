#pragma once

#include <Platform/Macros/PlatformSpecific.h>

namespace cs2
{

constexpr auto CLIENT_DLL = WIN64_LINUX("client.dll", "libclient.so");
constexpr auto ENGINE_DLL = WIN64_LINUX("engine2.dll", "libengine2.so");
constexpr auto NETWORKSYSTEM_DLL = WIN64_LINUX("networksystem.dll", "libnetworksystem.so");
constexpr auto FILESYSTEM_DLL = WIN64_LINUX("filesystem_stdio.dll", "libfilesystem_stdio.so");
constexpr auto PARTICLES_DLL = WIN64_LINUX("particles.dll", "libparticles.so");
constexpr auto PANORAMA_DLL = WIN64_LINUX("panorama.dll", "libpanorama.so");
constexpr auto SCENESYSTEM_DLL = WIN64_LINUX("scenesystem.dll", "libscenesystem.so");
constexpr auto SCHEMASYSTEM_DLL = WIN64_LINUX("schemasystem.dll", "libschemasystem.so");
constexpr auto SDL_DLL = WIN64_LINUX("SDL3.dll", "libSDL3.so.0");
constexpr auto SOUNDSYSTEM_DLL = WIN64_LINUX("soundsystem.dll", "libsoundsystem.so");
constexpr auto TIER0_DLL = WIN64_LINUX("tier0.dll", "libtier0.so");
constexpr auto VULKAN_DLL = WIN64_LINUX("vulkan-1.dll", "libvulkan.so.1");

}
