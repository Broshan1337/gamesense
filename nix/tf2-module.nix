# Neversnooze TF2 module: Source/libMangoHud.so (target `NeversnoozeTF2`).
# Output name is load-bearing, same disguise scheme as the CS2 module.
{
  lib,
  stdenv,
  cmake,
  ninja,
  keyMaterial,
}:

stdenv.mkDerivation {
  pname = "neversnooze-tf2-module";
  version = "unstable";

  # see cs2-module.nix for why build* dirs are filtered out
  src = lib.cleanSourceWith {
    src = ../tf2;
    filter = path: type: !(type == "directory" && lib.hasPrefix "build" (baseNameOf path));
  };

  nativeBuildInputs = [
    cmake
    ninja
  ];

  preConfigure = ''
    # tf2/Source/SessionBindKey.h is gitignored in the repo (same machine-local
    # key as the cs2 tree, see cs2/README.md) - inject the one generated from the
    # shared key-material derivation.
    cp ${keyMaterial}/headers/SessionBindKey.h Source/SessionBindKey.h
  '';

  installPhase = ''
    runHook preInstall
    install -Dm555 Source/libMangoHud.so $out/lib/libMangoHud.so
    runHook postInstall
  '';

  meta = {
    description = "Neversnooze TF2 module (libMangoHud.so)";
    license = lib.licenses.mit;
    platforms = ["x86_64-linux"];
  };
}
