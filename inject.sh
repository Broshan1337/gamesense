#!/bin/bash
#
# Unified injector: Steam module + CS2 cheat
#
# Usage: sudo ./inject.sh
#

set -e

project_dir=$(cd "$(dirname "$0")" && pwd)

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (use sudo)."
    exit 1
fi

echo "=========================================="
echo "   Unified Injector"
echo "=========================================="
echo ""

# ============================================
# STEP 0: Build modules
# ============================================

echo "[Build] Checking modules..."
echo ""

steam_module="$project_dir/build-steam/Source/libSteamModule.so"
cheat_module="$project_dir/build/Source/libMangoHud.so"

need_build=0

if [ ! -f "$steam_module" ]; then
    echo "[Build] Steam module not found"
    need_build=1
fi

if [ ! -f "$cheat_module" ]; then
    echo "[Build] Neversneeze module not found"
    need_build=1
fi

if [ "$need_build" -eq 0 ]; then
    # Check if source is newer
    newer_steam=$(find "$project_dir/Source/SteamModule" -type f -newer "$steam_module" -print -quit 2>/dev/null || true)
    newer_osiris=$(find "$project_dir/Source" -type f \( -name '*.h' -o -name '*.cpp' \) -newer "$cheat_module" -print -quit 2>/dev/null || true)
    
    if [ -n "$newer_steam" ] || [ -n "$newer_osiris" ]; then
        echo "[Build] Source files are newer than binaries"
        need_build=1
    fi
fi

if [ "$need_build" -eq 1 ]; then
    read -p "[Build] Rebuild modules? [Y/n]: " do_build
    do_build="${do_build:-y}"
    
    if [ "$do_build" = "y" ] || [ "$do_build" = "Y" ]; then
        echo ""
        echo "[Build] Building Steam module (32-bit)..."
        cmake --build "$project_dir/build-steam" --target SteamModule32 2>&1 | tail -5
        
        echo ""
        echo "[Build] Building Neversneeze (64-bit)..."
        cmake --build "$project_dir/build" --target Neversneeze 2>&1 | tail -5
        
        echo ""
        echo "[Build] Done"
    fi
fi

echo ""

# ============================================
# STEP 1: Steam Module
# ============================================

steam_pid=$(pidof steam 2>/dev/null | cut -d' ' -f1 || true)

if [ -n "$steam_pid" ]; then
    echo "[Steam] Found Steam (PID: $steam_pid)"
    echo ""
    read -p "Inject module into Steam? [Y/n]: " inject_steam
    inject_steam="${inject_steam:-y}"
    
    if [ "$inject_steam" = "y" ] || [ "$inject_steam" = "Y" ]; then
        steam_module="$project_dir/build-steam/Source/libSteamModule.so"
        
        if [ ! -f "$steam_module" ]; then
            echo "[Steam] Error: Module not found"
            echo "[Steam] Build it first: cmake --build build-steam --target SteamModule32"
        else
            echo "[Steam] Injecting..."
            
            module_tmp="/tmp/.X11-unix/.Xauthority-$(date +%s)"
            mkdir -p /tmp/.X11-unix 2>/dev/null || true
            cp "$steam_module" "$module_tmp"
            chmod 755 "$module_tmp"
            
            gdb -p "$steam_pid" -n -q -batch \
                -ex "handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint" \
                -ex "call ((void*(*)(const char*, int)) dlopen)(\"$module_tmp\", 1)" \
                -ex "call ((char*(*)(void)) dlerror)()" \
                -ex "detach" \
                -ex "quit" 2>&1 | grep -E "^\$|error|Error" || true
            
            rm -f "$module_tmp"
            echo "[Steam] Done"
        fi
    else
        echo "[Steam] Skipping"
    fi
else
    echo "[Steam] Steam not running"
fi

echo ""
echo "=========================================="
echo ""

# ============================================
# STEP 2: Wait for CS2
# ============================================

echo "[CS2] Ready to inject into CS2"
echo "[CS2] Press Enter when CS2 is running (or wait for auto-detect)..."
echo ""

# Wait for CS2 or user input
cs2_pid=""
for i in $(seq 1 30); do
    cs2_pid=$(pidof cs2 2>/dev/null | cut -d' ' -f1 || true)
    if [ -n "$cs2_pid" ]; then
        break
    fi
    
    # Check if user pressed Enter (non-blocking)
    if read -t 1 -N 1 2>/dev/null; then
        break
    fi
done

# Final check
cs2_pid=$(pidof cs2 2>/dev/null | cut -d' ' -f1 || true)

if [ -z "$cs2_pid" ]; then
    echo "[CS2] CS2 not found. Launch CS2 and run: sudo ./inject_memfd.sh"
    exit 1
fi

echo "[CS2] Found CS2 (PID: $cs2_pid)"

# ============================================
# STEP 3: CS2 Cheat Injection
# ============================================

lib_name="libMangoHud.so"
release_lib="$project_dir/build/Source/$lib_name"
debug_lib="$project_dir/build-dbg/Source/$lib_name"

# Build selection
build_lib="$release_lib"
if [ -t 0 ]; then
    while true; do
        printf "[CS2] Which build? 1) release  2) debug [1]: "
        IFS= read -r choice || break
        case "$choice" in
            "" | 1) build_lib="$release_lib" ; break ;;
            2) build_lib="$debug_lib" ; break ;;
            *) echo "[CS2] please enter 1 or 2" ;;
        esac
    done
fi

if [ ! -f "$build_lib" ]; then
    echo "[CS2] Error: Built library not found at '$build_lib'"
    echo "[CS2] Build it first: cmake --build build --target Neversneeze"
    exit 1
fi

# Re-injection guard
if grep -q "$lib_name" "/proc/$cs2_pid/maps" 2>/dev/null; then
    echo "[CS2] WARNING: $lib_name is already mapped in CS2 ($cs2_pid)"
    echo "[CS2] Unload first, or wait for deferred unmap to complete"
    exit 1
fi

# Staleness check
newer_source=$(find "$project_dir/Source" -type f \( -name '*.h' -o -name '*.cpp' \) -newer "$build_lib" -print -quit 2>/dev/null || true)
if [ -n "$newer_source" ]; then
    echo "[CS2] WARNING: source is newer than the built library"
    echo "[CS2] first newer file: $newer_source"
fi

echo "[CS2] Injecting: $build_lib"
echo "[CS2] Hash: $(sha256sum "$build_lib" | cut -d' ' -f1)"

# Block crash dumps
rm -rf /tmp/dumps
mkdir --mode=000 /tmp/dumps 2>/dev/null || true

# Inject using memfd for stealth
"$project_dir/inject_memfd" "$cs2_pid" "$build_lib" 2>&1 || {
    echo "[CS2] memfd injection failed, falling back to GDB..."
    
    inject_lib="/tmp/.font-unix/.fc-cache-$(date +%s)"
    mkdir -p /tmp/.font-unix 2>/dev/null || true
    cp "$build_lib" "$inject_lib"
    chmod 755 "$inject_lib"
    
    gdb -p "$cs2_pid" -n -q -batch \
        -ex "handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint" \
        -ex "call ((void*(*)(char*, int)) dlopen)(\"$inject_lib\", 1)" \
        -ex "call ((char*(*)(void)) dlerror)()" \
        -ex "detach" \
        -ex "quit" 2>&1 || true
    
    rm -f "$inject_lib"
}

echo ""
echo "=========================================="
echo " Injection complete!"
echo " Toggle menu: INSERT"
echo "=========================================="
