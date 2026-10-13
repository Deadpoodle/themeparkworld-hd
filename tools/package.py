"""Build the release zip: release/ThemeParkWorldHD-<version>.zip

  python -I tools/package.py [dgVoodoo2 zip]

Contents (extract into the game folder, next to TP.exe):
  wsock32.dll               the patch (build/wsock32.dll; run build.sh first)
  DDraw.dll, D3DImm.dll     dgVoodoo2 by Dege, copied byte for byte from its
                            official zip (MS/x86); its terms allow a game mod
                            to ship unmodified individual dgVoodoo files
  dgVoodoo.conf             our own short settings file (not a copy of
                            dgVoodoo's): no watermark, smooth 2D stretching
  ThemeParkWorldHD-README.txt, LICENSE.txt, THIRD-PARTY.txt

No game files go in. The dgVoodoo2 zip defaults to
third_party/dgVoodoo2/2.87.5/dgVoodoo2_87_5.zip.
"""
import hashlib
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DGV_VERSION = "2.87.5"

DGV_CONF = """\
; dgVoodoo2 settings for Theme Park World HD (written by Theme Park World HD,
; not dgVoodoo's own file: anything not listed here keeps dgVoodoo's default).
; dgVoodooCpl.exe from the dgVoodoo2 zip can write a full file instead.
Version = 0x287

[DirectX]
; no "dgVoodoo" logo in the corner
dgVoodooWatermark = false
; smooth (bilinear) stretching of the movies and the splash screens
Bilinear2DOperations = true
; edge smoothing and texture filtering: Theme Park World HD sets these from
; its own settings (HD Settings page, or Antialiasing/Anisotropic in
; ThemeParkWorldHD.ini) each time the game starts
Antialiasing = 4x
Filtering = 16
"""

README = """\
Theme Park World HD {version}
==============================

A patch for Theme Park World (1999) on Windows 10 and 11: it runs at your
screen's resolution with all text visible, in a borderless window, with a
fixed game clock, park music, smooth edges (anti-aliasing), 32-bit textures
without colour banding, sharper menu art and movies, a limit on the
middle-button camera turn, and an HD Settings page on the main menu. It
changes no game files: remove wsock32.dll to get the original game back.

NEEDS
- Your own copy of Theme Park World, updated to v2.0, with a TP.exe without
  SafeDisc (3,734,528 bytes; the one every community fix uses). The patch
  checks TP.exe and does nothing to a version it doesn't recognise.

INSTALL
1. Extract everything in this zip into the game folder, next to TP.exe.
   If you already use dgVoodoo2 with your own dgVoodoo.conf, keep yours
   (but set Bilinear2DOperations = true and dgVoodooWatermark = false in it).
2. Start the game as usual (TP.exe).
3. The first start writes ThemeParkWorldHD.ini (every setting, explained)
   and ThemeParkWorldHD.log next to TP.exe. The HD Settings sign under
   Quit Game on the main menu changes the common settings; changes apply
   the next time the game starts.

DGVOODOO2 (included)
DDraw.dll and D3DImm.dll are dgVoodoo2 {dgv} by Dege, unmodified. They run
the game's DirectX 6 on Direct3D 11, which lets it use more than 2048 pixels
of width. Without them (delete both) the patch still works, but on screens
wider than 2048 it picks the largest display mode up to 2048 wide.

KEYS
- F2: frame counter on/off
- Middle mouse button: turn the camera (faster further from the centre,
  up to the limit set on the HD Settings page)
- Alt+F4: quit
- Alt+Tab: a paused park carries on when you come back (setting)

UNINSTALL
Delete wsock32.dll, DDraw.dll, D3DImm.dll, dgVoodoo.conf,
ThemeParkWorldHD.ini and ThemeParkWorldHD.log.

Source, issues and the full feature list:
https://github.com/Deadpoodle/themeparkworld-hd
"""

THIRD_PARTY = """\
Third-party software and credits
================================

dgVoodoo2 {dgv} (DDraw.dll, D3DImm.dll)
  by Dege, http://dege.fw.hu/dgVoodoo2/
  Included unmodified, as dgVoodoo2's readme allows for games and game mods
  ("Redistribution rights"). dgVoodoo.conf in this zip is Theme Park World
  HD's own settings file. dgVoodoo2's full package and readme:
  https://github.com/dege-diosg/dgVoodoo2/releases
  DDraw.dll  sha256 {ddraw}
  D3DImm.dll sha256 {d3dimm}

TPW Patcher (the park music fix is re-implemented after it)
  https://github.com/TheAussie/TPW-Patcher

{tpw_licence}

Also thanks to OpenTPW (https://github.com/OpenTPW/OpenTPW) for the file
format documentation, PCGamingWiki, the community fix bundles and Adam
Hearn's font fix.
"""

TPW_LICENCE = """\
  MIT License

  Copyright (c) 2026 TheAussie

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE."""


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def main():
    dgv_zip = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "third_party", "dgVoodoo2", DGV_VERSION, "dgVoodoo2_87_5.zip")
    version = re.search(r'TPWHD_VERSION "([^"]+)"', open(os.path.join(ROOT, "src", "version.h")).read()).group(1)
    dll = os.path.join(ROOT, "build", "wsock32.dll")
    if not os.path.isfile(dll):
        sys.exit("build/wsock32.dll missing: run build.sh first")
    with zipfile.ZipFile(dgv_zip) as z:
        ddraw = z.read("MS/x86/DDraw.dll")
        d3dimm = z.read("MS/x86/D3DImm.dll")
    sha = lambda b: hashlib.sha256(b).hexdigest()
    os.makedirs(os.path.join(ROOT, "release"), exist_ok=True)
    out = os.path.join(ROOT, "release", f"ThemeParkWorldHD-{version}.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dll, "wsock32.dll")
        z.writestr("DDraw.dll", ddraw)
        z.writestr("D3DImm.dll", d3dimm)
        z.writestr("dgVoodoo.conf", crlf(DGV_CONF))
        z.writestr("ThemeParkWorldHD-README.txt", crlf(README.format(version=version, dgv=DGV_VERSION)))
        z.writestr("LICENSE.txt", crlf(open(os.path.join(ROOT, "LICENSE"), encoding="utf-8").read()))
        z.writestr("THIRD-PARTY.txt", crlf(THIRD_PARTY.format(dgv=DGV_VERSION, ddraw=sha(ddraw), d3dimm=sha(d3dimm),
                                                              tpw_licence=TPW_LICENCE)))
    with zipfile.ZipFile(out) as z:
        for i in z.infolist():
            print(f"{i.file_size:>9}  {i.filename}")
    print(f"-> {out} ({os.path.getsize(out):,} bytes)")


if __name__ == "__main__":
    main()
