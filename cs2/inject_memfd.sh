#!/bin/bash
#
# Stealthy injector using memfd + process_vm_writev
# No filesystem trace, no GDB, minimal ptrace exposure
#
# Usage: sudo ./inject_memfd.sh [release|debug]
#

set -e

project_dir=$(cd "$(dirname "$0")" && pwd)
lib_name="libMangoHud.so"
release_lib="$project_dir/build/Source/$lib_name"
debug_lib="$project_dir/build-dbg/Source/$lib_name"
injector_bin="$project_dir/inject_memfd"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root (sudo)"
    exit 1
fi

build_type="${1:-release}"
if [ "$build_type" = "debug" ]; then
    target_lib="$debug_lib"
else
    target_lib="$release_lib"
fi

# Rebuild whenever the source is newer than the binary (stale-binary trap)
if [ ! -x "$injector_bin" ] || [ "$project_dir/inject_memfd.c" -nt "$injector_bin" ]; then
    echo "[*] Building injector..."
    gcc -O2 -Wall -Wextra -o "$injector_bin" "$project_dir/inject_memfd.c" || exit 1
    strip "$injector_bin" 2>/dev/null || true
    chown --reference="$project_dir/inject_memfd.c" "$injector_bin" 2>/dev/null || true
fi

if [ ! -f "$target_lib" ]; then
    echo "Error: Built library not found at '$target_lib'"
    echo "Build it first:"
    echo "  cmake --build build --target Neversnooze      # release"
    echo "  cmake --build build-dbg --target Neversnooze  # debug"
    exit 1
fi

all_cs2_pids=$(pidof cs2)
cs2_pid=$(echo "$all_cs2_pids" | cut -d' ' -f1)
if [ -z "$cs2_pid" ]; then
    echo "Error: CS2 is not running"
    exit 1
fi

if [ "$(echo "$all_cs2_pids" | wc -w)" -gt 1 ]; then
    echo "Warning: Multiple CS2 processes found ($all_cs2_pids)"
    echo "         Injecting into: $cs2_pid"
fi

if grep -q "$lib_name" "/proc/$cs2_pid/maps" 2>/dev/null; then
    echo "Error: $lib_name is already loaded in CS2 (PID $cs2_pid)"
    echo "       Unload first, or wait for deferred unmap to complete"
    exit 1
fi

echo "=========================================="
echo "   Memfd Injector - Stealthy Injection"
echo "=========================================="
echo ""
echo "Target PID:    $cs2_pid"
echo "Library:       $target_lib"
echo "Library hash:  $(sha256sum "$target_lib" | cut -d' ' -f1)"
echo ""

"$injector_bin" "$cs2_pid" "$target_lib"

echo ""
echo "=========================================="
echo " Injection complete. Check game for menu."
echo "=========================================="
