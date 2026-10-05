# Neversnooze CS2 module: Source/libMangoHud.so (target `Neversnooze`) plus the
# 64-bit libSteamModule.so built by the same tree.
#
# Stock GCC only: the Arkari-obfuscated ship tree (build-obf-test) needs the external
# OLLVM fork and stays outside Nix - this is the reproducible plain build, i.e. the
# daily dev tree's artifact.
{
  lib,
  stdenv,
  cmake,
  ninja,
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
  pname = "neversnooze-cs2-module";
  version = "unstable";

  # Local build trees must not leak into the build: when this flake is consumed as a
  # path input (the loader does), Nix copies the FULL worktree - untracked build dirs
  # included - and a stale CMakeCache.txt inside <src>/build would poison configure
  # (the nixpkgs cmake hook also configures into <src>/build).
  src = lib.cleanSourceWith {
    src = ../cs2;
    filter = path: type: !(type == "directory" && lib.hasPrefix "build" (baseNameOf path));
  };

  nativeBuildInputs = [
    cmake
    ninja
  ];
  # Vulkan is header-only in the tree (IMGUI_IMPL_VULKAN_NO_PROTOTYPES); the loader
  # entry makes find_package(Vulkan REQUIRED) succeed.
  buildInputs = [
    vulkan-headers
    vulkan-loader
  ];

  preConfigure = ''
    # SessionBindKey.h is machine-local and gitignored in the repo (the module
    # verifies the loader-stamped trailer fail-closed against it). Inject the one
    # generated from the shared key-material derivation so this module pairs with
    # a loader built against the same keydir.
    cp ${keyMaterial}/headers/SessionBindKey.h Source/Utils/SessionBindKey.h
  '';

  cmakeFlags = [
    "-DNEVERSNOOZE_OBFUSCATE=OFF"
    "-DFETCHCONTENT_SOURCE_DIR_SDL3=${fc.sdl3}"
    "-DFETCHCONTENT_SOURCE_DIR_IMGUI=${fc.imgui}"
  ];

  # The tree has no install rules - collect the built modules directly. The output
  # names (libMangoHud.so) are load-bearing: injection, self-unload and the loader's
  # mapped-state checks key on them, so they must not be renamed.
  installPhase = ''
    runHook preInstall
    install -Dm555 Source/libMangoHud.so $out/lib/libMangoHud.so
    install -Dm555 Source/libSteamModule.so $out/lib/libSteamModule.so
    runHook postInstall
  '';

  meta = {
    description = "Neversnooze CS2 module (libMangoHud.so) and 64-bit Steam module";
    license = lib.licenses.mit;
    platforms = ["x86_64-linux"];
  };
}
