"""Convert the texture dump (ThemeParkWorldHD [Debug] DumpTextures=1, written to
<game>\\dump\\textures\\<dir>\\<name>.tga by the game's own decoder) into PNGs,
plus the splash/legal/welcome art from data\\Init\\1024, with a manifest.

  python -P tools/dump2png.py <game folder> <output folder>

The PNG names are what the HD art loader looks for: data\\hd\\<dir>\\<name>.png
(e.g. data\\hd\\textures\\b_login.png). UI button plates carry no text (labels are
drawn at run time); the splash, legal and welcome images have text baked in.
"""
import os
import sys

from PIL import Image


def main(game, out):
    rows = []
    src = os.path.join(game, "dump", "textures")
    for d in sorted(os.listdir(src)) if os.path.isdir(src) else []:
        for f in sorted(os.listdir(os.path.join(src, d))):
            if not f.lower().endswith(".tga"):
                continue
            im = Image.open(os.path.join(src, d, f))
            os.makedirs(os.path.join(out, d), exist_ok=True)
            name = f[:-4] + ".png"
            im.save(os.path.join(out, d, name))
            alpha = im.mode == "RGBA" and im.getextrema()[3][0] < 255
            rows.append((d, name, im.width, im.height, "alpha" if alpha else "", ""))
    init = os.path.join(game, "data", "Init", "1024")
    if os.path.isdir(init):
        os.makedirs(os.path.join(out, "Init"), exist_ok=True)
        for f in sorted(os.listdir(init)):
            if f.lower().endswith(".tga"):
                im = Image.open(os.path.join(init, f))
                name = f[:-4] + ".png"
                im.save(os.path.join(out, "Init", name))
                rows.append(("Init", name, im.width, im.height, "", "text baked in"))
    with open(os.path.join(out, "manifest.tsv"), "w", encoding="utf-8", newline="\n") as m:
        m.write("dir\tname\twidth\theight\talpha\tnote\n")
        for r in rows:
            m.write("\t".join(str(x) for x in r) + "\n")
    print(f"{len(rows)} images -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
