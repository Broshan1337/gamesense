import subprocess, sys, struct

PID = int(sys.argv[1])
BASE = int(sys.argv[2], 16)  # libMangoHud.so base addr from /proc/PID/maps

RING = BASE + 0xf1d760   # _ZN11CrashLogger9traceRingE (debug build, non-PIE-offset vaddr)
WIDX = BASE + 0xf1d960   # _ZN11CrashLogger15traceWriteIndexE

with open(f'/proc/{PID}/mem', 'rb', 0) as f:
    f.seek(RING)
    ring = f.read(64 * 8)
    f.seek(WIDX)
    widx = struct.unpack('<I', f.read(4))[0]

print(f'traceWriteIndex = {widx}')
vals = struct.unpack('<64Q', ring)
# last-written first: walk backwards from widx-1
for n in range(64):
    i = (widx - 1 - n) % 64
    v = vals[i]
    if v:
        print(f'{i:02d}: 0x{v:x}')
