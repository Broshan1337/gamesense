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
# This derivation generates a fresh, self-consistent keydir + module header in the
# exact layout payload_packer expects (openssl genpkey emits the same unencrypted
# PKCS#8 PEM that the packer's PEM_read_PrivateKey path reads), so the loader
# derivation can pack and emit payload_keys.h against it directly.
#
# NOTE: derivation outputs are world-readable, so the private/sym keys end up in
# the Nix store. For the dev flow that keeps keys in ~/.config/neversnooze-keys
# this is a real trade-off - a leaked build key only ever signs payloads for the
# one loader build that embeds it, and module verification still fails closed for
# anything re-injected standalone.
{
  lib,
  stdenv,
  openssl,
  python3,
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
    openssl genpkey -algorithm ED25519 -out keys/payload_private.pem
    openssl rand -out keys/payload_sym.key 32
    openssl rand -out keys/heartbeat.key 32
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
    description = "Fresh Neversnooze keydir (payload signing/encryption + session-bind keys) and module-side SessionBindKey.h";
    license = lib.licenses.mit;
    platforms = ["x86_64-linux"];
  };
}
