#!/bin/sh
#
# Force-unloads a stuck libOsiris.so from a running CS2.
#
# The normal unload path (the in-game Unload button) drops exactly two dlopen references - the
# injector's and its own - and unmaps. If the library STILL shows up in the process maps after
# that, the loader's reference count was higher than two: something took an extra dlopen
# reference at some point, and dlclose silently does nothing until the count reaches zero.
#
# This script walks the loader's own link_map list (struct r_debug._r_debug is exported by the
# dynamic linker; the first five link_map fields are ABI-stable: l_addr @0, l_name @8, l_ld @16,
# l_next @24, l_prev @32), finds our entry and calls libc dlclose on its address once per
# reference - which is exactly what a handle is under the hood (dlopen returns link_map*).
# /proc/pid/maps is checked after every call, so the script stops the moment the library is
# really gone. The number of calls printed at the end is the measurement: "1" would mean the
# regular unload never dropped anything; "2" means exactly the two expected references somehow
# survived; "3+" means an extra reference was (or still is) in play.
#
# Must run as root (ptrace), same as inject.sh.

lib_name="libOsiris.so"

if [ "$(id -u)" -ne 0 ]; then
	echo "This script must be run as root (use sudo)."
	exit 1
fi

cs2_pid=$(pidof cs2 | cut -d' ' -f1)
if [ -z "$cs2_pid" ]; then
	echo "CS2 can't be found, is the game running?"
	exit 1
fi

if ! grep -q "$lib_name" "/proc/$cs2_pid/maps" 2>/dev/null; then
	echo "$lib_name is not mapped in CS2 ($cs2_pid) - nothing to do."
	exit 0
fi

echo "Searching the loader's link_map for $lib_name in CS2 ($cs2_pid)..."

# gdb walks the link_map list and prints one "ADDR=<address> NAME=<path>" line per entry.
# l_next is at +24 and l_name at +8 - the ABI-stable public fields, so no debug info needed.
link_map_addr=$(gdb -p "$cs2_pid" -n -q -batch \
	-ex "set \$m = *(unsigned long*)((char*)&_r_debug + 8)" \
	-ex "while \$m != 0" \
	-ex "if *(unsigned long*)(\$m + 8) != 0" \
	-ex 'printf "ADDR=%p NAME=", *(void**)$m' \
	-ex "x/s *(char**)(\$m + 8)" \
	-ex "end" \
	-ex "set \$m = *(unsigned long*)(\$m + 24)" \
	-ex "end" 2>/dev/null | awk '
	{ if (match($0, /ADDR=0x[0-9a-f]+/)) addr = substr($0, RSTART + 5, RLENGTH - 5) }
	index($0, "libOsiris") > 0 { print addr; exit }')

if [ -z "$link_map_addr" ]; then
	echo "Could not find $lib_name in the loader's link map (it may be mapped but unregistered)."
	exit 1
fi

echo "Found link_map entry at $link_map_addr. Dropping references one by one..."

i=0
while [ $i -lt 8 ]; do
	i=$((i + 1))
	if ! grep -q "$lib_name" "/proc/$cs2_pid/maps" 2>/dev/null; then
		echo ""
		echo "Unmapped after $i dlclose call(s)."
		echo "(1 call = the regular unload had dropped nothing; 2 = only the two expected"
		echo " references survived somehow; 3+ = extra references were still held.)"
		exit 0
	fi
	echo -n "dlclose #$i... "
	gdb -p "$cs2_pid" -n -q -batch \
		-ex "handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint" \
		-ex "call ((int(*)(void*)) dlclose)((void*)$link_map_addr)" \
		-ex "detach" \
		-ex "quit" 2>&1 | grep -E '\$1|value' | head -1
	sleep 1
done

if grep -q "$lib_name" "/proc/$cs2_pid/maps" 2>/dev/null; then
	echo ""
	echo "STILL mapped after 8 dlclose calls - this is NOT a reference-count problem."
	echo "The loader itself is refusing the unmap; report the unload log output."
fi
