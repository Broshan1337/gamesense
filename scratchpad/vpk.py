import struct, sys, os

VPK = "/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk"
GAME = "/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo"

def tree(f):
    f.seek(0)
    sig, ver, treesize = struct.unpack("<III", f.read(12))
    assert sig == 0x55AA1234, hex(sig)
    f.seek(28)  # full v2 header is 28 bytes; tree starts after it
    end = 28 + treesize
    entries = {}
    while f.tell() < end:
        ext = read_cstr(f)
        if not ext: break
        while True:
            path = read_cstr(f)
            if not path: break
            while True:
                name = read_cstr(f)
                if not name: break
                crc, preload, idx, off, length, term = struct.unpack("<IHHIIH", f.read(18))
                data = f.read(preload)
                full = (path + "/" if path else "") + name + ("." + ext if ext else "")
                entries[full] = (idx, off, length, data)
    return entries, 28 + treesize

def read_cstr(f):
    b = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b"\0": return b.decode("latin1")
        b += c

def extract(entries, base, wanted):
    out = {}
    for name in wanted:
        hits = [k for k in entries if k.endswith(name)]
        if not hits:
            print("MISS", name); continue
        for k in hits:
            idx, off, length, preload = entries[k]
            if idx == 0x7FFF:
                with open(VPK, "rb") as f:
                    f.seek(base + off); out[k] = f.read(length)
            else:
                with open(os.path.join(GAME, "pak01_%03d.vpk" % idx), "rb") as f:
                    f.seek(off); out[k] = f.read(length)
            print("OK", k, len(out[k]))
    return out

if __name__ == "__main__":
    with open(VPK, "rb") as f:
        entries, base = tree(f)
    print(len(entries), "files")
    out = extract(entries, base, sys.argv[1:] or ["items_game.txt", "csgo_english.txt"])
    for k, v in out.items():
        p = "/tmp/opencode/" + os.path.basename(k)
        open(p, "wb").write(v)
        print("->", p)
