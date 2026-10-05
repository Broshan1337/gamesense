# 32-bit libSteamModule.so - the same cs2 tree configured the way the build-steam
# tree is (-DNEVERSNOOZE_BUILD_STEAM_MODULE32=ON), building only the SteamModule32
# target. Steam runs as a 32-bit process, so this is the artifact the loader packs
# as its Steam payload. Needs a multilib toolchain for -m32.
{
  lib,
  stdenv,
  cmake,
  fetchFromGitHub,
  applyPatches,
  vulkan-headers,
  vulkan-loader,
  keyMaterial,
}:

let
  fc = import ./fetchcontent.nix { inherit lib fetchFromGitHub applyPatches; };
in
stdenv.mkDerivation {
  pname = "neversnooze-steam-module";
  version = "unstable";

  # see cs2-module.nix for why build* dirs are filtered out
  src = lib.cleanSourceWith {
    src = ../cs2;
    filter = path: type: !(type == "directory" && lib.hasPrefix "build" (baseNameOf path));
  };

  # Unix Makefiles, NOT Ninja: with both SteamModule targets defined the tree has two
  # rules for Source/libSteamModule.so (same output name), which ninja refuses outright
  # while Make tolerates it - exactly how the local build-steam tree gets away with it
  # (only SteamModule32 is ever built there). No ninja in nativeBuildInputs makes the
  # cmake hook fall back to the default Unix Makefiles generator; make is in stdenv PATH.
  nativeBuildInputs = [
    cmake
  ];
  # find_package(Vulkan REQUIRED) runs at configure time for the whole tree; only
  # headers are actually used and SteamModule32 links nothing beyond libc.
  buildInputs = [
    vulkan-headers
    vulkan-loader
  ];

  preConfigure = ''
    # SessionBindKey.h is machine-local and gitignored in the repo; inject the one
    # from the shared key-material derivation (same file as the cs2 module's copy).
    cp ${keyMaterial}/headers/SessionBindKey.h Source/Utils/SessionBindKey.h
  '';

  cmakeFlags = [
    "-DNEVERSNOOZE_OBFUSCATE=OFF"
    "-DNEVERSNOOZE_BUILD_STEAM_MODULE32=ON"
    "-DFETCHCONTENT_SOURCE_DIR_SDL3=${fc.sdl3}"
    "-DFETCHCONTENT_SOURCE_DIR_IMGUI=${fc.imgui}"
  ];

  # build only the 32-bit module - the rest of the tree stays 64-bit
  buildFlags = ["SteamModule32"];

  installPhase = ''
    runHook preInstall
    install -Dm555 Source/libSteamModule.so $out/lib/libSteamModule.so
    runHook postInstall
  '';

  meta = {
    description = "Neversnooze 32-bit Steam module (libSteamModule.so)";
    license = lib.licenses.mit;
    platforms = ["x86_64-linux"];
  };
}
