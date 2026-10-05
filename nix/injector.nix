# inject_memfd - the ptrace/memfd injector the loader packs as payload p3.
#
# Built STATIC on purpose: the loader ships it inside its encrypted payloads and
# runs it on machines that may not have a Nix store, where a dynamically linked
# binary would carry the build host's interpreter and fail to start. The source
# is self-contained C (it resolves dlopen by parsing the target's libc .dynsym,
# no -ldl link needed), so static glibc is fine.
{
  lib,
  stdenv,
  glibc,
}:

stdenv.mkDerivation {
  pname = "inject-memfd";
  version = "unstable";

  dontUnpack = true;

  # glibc.static: -static needs libc.a, which the default glibc output does not ship
  buildInputs = [glibc.static];

  buildPhase = ''
    runHook preBuild
    $CC -O2 -Wall -Wextra -static -o inject_memfd ${../cs2/inject_memfd.c}
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm555 inject_memfd $out/bin/inject_memfd
    runHook postInstall
  '';

  meta = {
    description = "memfd_create + process_vm_writev library injector for CS2/TF2/Steam";
    license = lib.licenses.mit;
    platforms = ["x86_64-linux"];
    mainProgram = "inject_memfd";
  };
}
