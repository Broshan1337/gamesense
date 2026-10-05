# Shared key material for the whole Neversnooze chain.
#
# The loader's payload_packer keeps one keydir with three secrets:
#   payload_private.pem - Ed25519 signing key (NEVER shipped)
#   payload_sym.key     - ChaCha20-Poly1305 payload encryption key (embedded in the loader)
#   heartbeat.key       - session-binding key (loader stamps the trailer; modules verify)
#
# The module trees compile against that same heartbeat key, masked, as
# SessionBindKey.h (gitignored in the repos - the repo "doesn't build as-is"
# without it, see cs2/README.md "Session binding / heartbeats").
#
# KEY SOURCE (2026-10-05 fix): this derivation REUSES the dev keydir at
# ~/.config/neversnooze-keys (passed in as devKeydir from flake.nix) instead of
# minting fresh random keys per build. The earlier behavior (openssl rand on
# every build) silently re-keyed the whole chain per evaluation: a nix-built
# loader would refuse modules from a cmake build and vice versa (the module
# fail-closes on an unknown heartbeat key - zero visible errors, just dead
# features). With the dev keydir pinned, nix-built and cmake-built artifacts
# always pair, and key rotation is an explicit act (rotate the file, rebuild).
#
# The SessionBindKey.h emitted here is byte-identical to the machine-local one
# the cmake flow keeps in the repo (same gen_session_bind_key.py, same key).
{
  lib,
  stdenv,
  openssl,
  python3,
  devKeydir,   # store path of the dev keydir (flake: ~/.config/neversnooze-keys)
}:

stdenv.mkDerivation {
  pname = "neversnooze-key-material";
  version = "unstable";

  dontUnpack = true;

  nativeBuildInputs = [
    openssl
    python3
  ];

  buildPhase = ''
    runHook preBuild
    mkdir -p keys headers
    cp ${devKeydir}/payload_private.pem keys/payload_private.pem
    cp ${devKeydir}/payload_sym.key keys/payload_sym.key
    cp ${devKeydir}/heartbeat.key keys/heartbeat.key
    python3 ${./gen_session_bind_key.py} keys/heartbeat.key headers/SessionBindKey.h
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp -r keys headers $out/
    runHook postInstall
  '';

  meta = {
    description = "Neversnooze keydir (payload signing/encryption + session-bind keys) and module-side SessionBindKey.h - pinned to the dev keydir";
    license = lib.licenses.mit;
    platforms = ["x86_64-linux"];
  };
}
