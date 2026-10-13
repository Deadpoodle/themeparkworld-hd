"""Read-only inventory of Theme Park World UI art (re/ui-art-pipeline.md section 5).

  python -P tools/uiextract.py list <file.wad>             directory: dir, name, sizes, packing
  python -P tools/uiextract.py raw <file.wad> <outdir>     every entry, RefPack-unpacked, by path
  python -P tools/uiextract.py sprites [ui.wad]            UI sprites: name, UI hash, textures, sizes
  python -P tools/uiextract.py tga2png <in.tga> <out.png>  TGA (type 1/2, 8/24/32 bpp) to PNG

It reads game files and writes only under the output path you give. WCT pixels are not
decoded here (wavelet codec, see the report); WCT headers are. No third-party modules.
"""
import os
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
UI_WAD = os.path.join(ROOT, "testrun", "data", "ui.wad")


def refpack(src):
    """EA RefPack (QFS). Header: flags byte, 0xFB, 3- or 4-byte big-endian size."""
    flags, magic = src[0], src[1]
    if magic != 0xFB:
        raise ValueError("not RefPack")
    i = 6 if flags & 0x80 else 5
    if flags & 0x01:
        i += 4 if flags & 0x80 else 3
    out = bytearray()
    while i < len(src):
        b0 = src[i]
        if b0 < 0x80:
            b1 = src[i + 1]; i += 2
            n = b0 & 3; out += src[i:i + n]; i += n
            cl, off = ((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = src[i + 1], src[i + 2]; i += 3
            n = b1 >> 6; out += src[i:i + n]; i += n
            cl, off = (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = src[i + 1], src[i + 2], src[i + 3]; i += 4
            n = b0 & 3; out += src[i:i + n]; i += n
            cl, off = ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            n = ((b0 & 0x1F) << 2) + 4; i += 1; out += src[i:i + n]; i += n
            continue
        else:
            n = b0 & 3; i += 1; out += src[i:i + n]
            break
        for _ in range(cl):
            out.append(out[-off])
    return bytes(out)


def wad_entries(path):
    """DWFB: magic, version, 64 pad, count @0x48, name-table offset/len; 40-byte entries @0x54.
    Entry: 0, id/hash, name offset, name length, data offset, packed size, packing (4 = RefPack),
    unpacked size, 0, 0. A name 'dir\\file' sets the directory for it and the entries after it."""
    d = open(path, "rb").read()
    if d[:4] != b"DWFB":
        raise ValueError("not a DWFB wad")
    count = struct.unpack_from("<I", d, 0x48)[0]
    cur = ""
    for k in range(count):
        e = struct.unpack_from("<10I", d, 0x54 + 40 * k)
        name = d[e[2]:e[2] + e[3]].split(b"\0")[0].decode("latin1")
        if "\\" in name:
            cur, name = name.rsplit("\\", 1)
        raw = d[e[4]:e[4] + e[5]]
        yield cur, name, e, (lambda raw=raw, e=e: refpack(raw) if e[6] == 4 else raw)


def ui_hash(name):
    """UI_HashName 0x006588b6 with mul 0x2f (UI_FindImageByName): h = (h ^ (signed char)c) * 0x2f."""
    h = 0
    for c in name.encode("latin1"):
        h = ((h ^ (c - 256 if c > 127 else c)) * 0x2F) & 0xFFFFFFFF
    return h


def wct_header(d):
    """WCT header (0x1c bytes, read by 0x00564c00): codec 0x12/0x13, alpha flag, bits 24/32,
    block code, width, height, 4 x u16 quantisers, colour bytes, alpha bytes, 4 unknown."""
    codec, alpha, bits, block, w, h = struct.unpack_from("<BBBBHH", d, 0)
    q = struct.unpack_from("<4H", d, 8)
    csize, asize = struct.unpack_from("<II", d, 16)
    return dict(codec=codec, alpha=alpha, bits=bits, block=block, w=w, h=h, q=q, csize=csize, asize=asize)


def md2_info(d):
    """Bullfrog M3D2 sprite: texture count u16 @0x36, texture table offset @0x50 (8 bytes each,
    then 20-byte names), mesh @[0x78] with its name offset at mesh+0x54."""
    ntex = struct.unpack_from("<H", d, 0x36)[0]
    toff = struct.unpack_from("<I", d, 0x50)[0]
    names = [d[toff + 8 * ntex + 20 * k: toff + 8 * ntex + 20 * (k + 1)].split(b"\0")[0].decode("latin1")
             for k in range(ntex)]
    mesh = struct.unpack_from("<I", d, 0x78)[0]
    sname = ""
    if 0 < mesh < len(d) - 0x58:
        p = struct.unpack_from("<I", d, mesh + 0x54)[0]
        if 0 < p < len(d):
            sname = d[p:p + 64].split(b"\0")[0].decode("latin1")
    return sname, names


def cmd_list(path):
    for cur, name, e, _ in wad_entries(path):
        print(f"{cur or '.'}\t{name}\t{e[5]}\t{e[7]}\t{'refpack' if e[6] == 4 else 'stored'}")


def cmd_raw(path, outdir):
    for cur, name, e, get in wad_entries(path):
        p = os.path.join(outdir, cur, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(get())


def cmd_sprites(path=UI_WAD):
    tex = {}
    sprites = []
    for cur, name, e, get in wad_entries(path):
        low = name.lower()
        if low.endswith(".wct"):
            tex[(cur, low[:-4])] = wct_header(get())
        elif low.endswith(".md2"):
            sprites.append((name,) + md2_info(get()))
    print("sprite\tinternal_name\tui_hash\ttexture\ttextures_WxH\tstexture_WxH\talpha")
    for fname, sname, tnames in sprites:
        for t in tnames or [""]:
            base = t.lower().rsplit(".", 1)[0]
            hi, lo = tex.get(("textures", base)), tex.get(("stexture", base))
            print(f"{fname}\t{sname}\t{ui_hash(sname):08x}\t{t}\t"
                  f"{hi and '%dx%d' % (hi['w'], hi['h']) or '-'}\t"
                  f"{lo and '%dx%d' % (lo['w'], lo['h']) or '-'}\t"
                  f"{hi and int(hi['bits'] == 32) if hi else '-'}")


def read_tga(path):
    d = open(path, "rb").read()
    idlen, cmtype, itype = d[0], d[1], d[2]
    cmfirst, cmlen, cmbits = struct.unpack_from("<HHB", d, 3)
    w, h, bpp, desc = struct.unpack_from("<HHBB", d, 12)
    p = 18 + idlen
    pal = None
    if cmtype == 1:
        n = (cmbits + 7) // 8
        pal = [d[p + k * n:p + (k + 1) * n] for k in range(cmlen)]
        p += cmlen * n
    if itype not in (1, 2):
        raise ValueError(f"TGA type {itype} not handled")
    n = bpp // 8
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            px = d[p:p + n]; p += n
            if itype == 1:
                px = pal[px[0] - cmfirst]
            b, g, r = px[0], px[1], px[2]
            a = px[3] if len(px) == 4 else 255
            row += bytes((r, g, b, a))
        rows.append(bytes(row))
    if not desc & 0x20:  # bottom-left origin
        rows.reverse()
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd, args = argv[1], argv[2:]
    if cmd == "list":
        cmd_list(*args)
    elif cmd == "raw":
        cmd_raw(*args)
    elif cmd == "sprites":
        cmd_sprites(*args)
    elif cmd == "tga2png":
        write_png(args[1], *read_tga(args[0]))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
