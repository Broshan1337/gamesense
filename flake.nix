{
  description = "Neversnooze game modules (CS2 / TF2) and the memfd injector";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    packages.${system} = rec {
      # Fresh keydir (payload_private.pem / payload_sym.key / heartbeat.key) plus the
      # module-side SessionBindKey.h. Shared by the modules (compile-time trailer key)
      # and the loader (payload packing + verification) so the whole chain stays in sync.
      key-material = pkgs.callPackage ./nix/key-material.nix {};

      # libMangoHud.so (Neversnooze CS2 module) + 64-bit libSteamModule.so
      cs2-module = pkgs.callPackage ./nix/cs2-module.nix {
        keyMaterial = key-material;
      };

      # 32-bit libSteamModule.so - mirrors the build-steam tree (-DNEVERSNOOZE_BUILD_STEAM_MODULE32=ON);
      # this is the artifact the loader packs as its Steam payload
      steam-module = pkgs.callPackage ./nix/steam-module.nix {
        stdenv = pkgs.gccMultiStdenv;
        keyMaterial = key-material;
      };

      # libMangoHud.so from the TF2 tree (NeversnoozeTF2)
      tf2-module = pkgs.callPackage ./nix/tf2-module.nix {
        keyMaterial = key-material;
      };

      # inject_memfd - statically linked so it also runs on non-Nix machines
      # (the loader embeds it as payload p3)
      injector = pkgs.callPackage ./nix/injector.nix {};

      # NOTE: no combined default - cs2-module and tf2-module both install a
      # libMangoHud.so (required output names), so a symlinkJoin would collide.
      default = cs2-module;
    };

    devShells.${system} = rec {
      cs2 = pkgs.mkShell {
        packages = with pkgs; [
          cmake
          ninja
          git # FetchContent deps (SDL3 headers, imgui) for manual dev builds
          vulkan-headers
          vulkan-loader
        ];
      };
      tf2 = pkgs.mkShell {
        packages = with pkgs; [
          cmake
          ninja
          git
        ];
      };
      default = cs2;
    };
  };
}
