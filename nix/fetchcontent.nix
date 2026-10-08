# Pinned sources for the two FetchContent dependencies in cs2/Source/CMakeLists.txt.
#
# The Nix build sandbox has no network, so CMake is pointed at these checkouts via
# FETCHCONTENT_SOURCE_DIR_SDL3 / FETCHCONTENT_SOURCE_DIR_IMGUI. When the source dir
# is overridden, FetchContent skips its download AND patch steps - so imgui carries
# the same four patches here, in the same order as the tree's PATCH_COMMAND.
{
  lib,
  fetchFromGitHub,
  applyPatches,
}: {
  # Headers only (SOURCE_SUBDIR include in the tree): nothing is linked, the UI
  # backend resolves SDL functions from the game's own libSDL3 at runtime.
  sdl3 = fetchFromGitHub {
    owner = "libsdl-org";
    repo = "SDL";
    rev = "release-3.2.18";
    hash = "sha256-z3SMxPoO5zWOvJvgkla3vMg51qdKqbMGudIwOr3265s=";
  };

  imgui = applyPatches {
    name = "imgui-1.91.7-neversnooze-patched";
    src = fetchFromGitHub {
      owner = "ocornut";
      repo = "imgui";
      rev = "v1.91.7";
      hash = "sha256-BuQB3e0NhMiFqLynA/AXqAzAkTlZrw+Faysqxc9iTLY=";
    };
    patches = [
      ../cs2/Patches/ImGUI/FullyRoundedTabs.patch
      ../cs2/Patches/ImGUI/RoundedImageButtons.patch
      ../cs2/Patches/ImGUI/PrependDrawList.patch
      ../cs2/Patches/ImGUI/ExposeSetupDrawListSharedData.patch
    ];
  };
}
