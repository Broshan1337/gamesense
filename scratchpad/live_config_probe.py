import struct

PID = 280956
BASE = 0x7fb256fe7000

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]

gc = u64(BASE + 0xebc460)
FULL = gc + 24
# ConfigState @ FULL+16 (1600 B); ConfigVariables @ +88 within it:
#   oneByte[285] @ FULL+88, twoByte[128] @ FULL+373, fourByte[54] @ FULL+629
one = rd(FULL + 88, 285)
two = rd(FULL + 373, 256)
four = rd(FULL + 629, 54 * 4)
ones = [i for i, b in enumerate(one) if b]
twos = [i for i in range(128) if struct.unpack_from('<H', two, i * 2)[0] not in (0,)]
fours = [i for i in range(54) if struct.unpack_from('<I', four, i * 4)[0] not in (0,)]
print(f'in-memory config: oneByte nonzeros {len(ones)}/285 at {ones}')
print(f'twoByte non-default {len(twos)}/128 at {twos[:10]}')
print(f'fourByte non-default {len(fours)}/54 at {fours[:10]}')
# a properly loaded config has dozens of true bools (glow, esp, hud, misc...). If only a
# handful are set, the config did not load.
print()
print('reference: on-disk default.cfg has PlayerInfoInWorld.Enabled=true, ModelGlow=true,')
print('OutlineGlow=true, Triggerbot=true, NameAnimator=false, ClanTag*=false')
