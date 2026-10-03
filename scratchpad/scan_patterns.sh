#!/bin/bash
# Extract .text from the current build's CS2 modules + run the offline pattern scanner.
# Usage: ./scratchpad/scan_patterns.sh   (game dir auto-detected)
set -euo pipefail
GAME=/mnt/disk2/SteamLibrary/steamapps/common/"Counter-Strike Global Offensive"
CSGO="$GAME/game/csgo/bin/linuxsteamrt64"
BIN="$GAME/game/bin/linuxsteamrt64"
OUT=/tmp/opencode/pattext
rm -rf "$OUT"; mkdir -p "$OUT"

extract() { # <libpath> <name>
    local sh_addr
    sh_addr=$(readelf -SW "$1" | awk '$2 == ".text" {print strtonum($4)}' | head -1)
    objcopy -O binary --only-section=.text "$1" "$OUT/$2"
    printf "%-18s .text %8d bytes  sh_addr 0x%s\n" "$2" "$(stat -c%s "$OUT/$2")" "$sh_addr"
}

extract "$CSGO/libclient.so" client
extract "$BIN/libtier0.so" tier0
extract "$BIN/libsoundsystem.so" soundsystem
extract "$BIN/libfilesystem_stdio.so" filesystem
extract "$BIN/libpanorama.so" panorama
extract "$BIN/libscenesystem.so" scenesystem
extract "$BIN/libschemasystem.so" schemasystem

echo "---- .text sh_addr values (for match-offset -> vaddr: vaddr = sh_addr + offset) ----"
for lib in "$CSGO/libclient.so" "$BIN/libtier0.so" "$BIN/libsoundsystem.so" "$BIN/libfilesystem_stdio.so" "$BIN/libpanorama.so" "$BIN/libscenesystem.so" "$BIN/libschemasystem.so"; do
    printf "%-40s .text sh_addr %s\n" "$(basename "$lib")" \
        "$(readelf -SW "$lib" | awk '$2 == ".text" {print $4}')"
done