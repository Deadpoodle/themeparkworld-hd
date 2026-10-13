# Theme Park World / Sim Theme Park: prior-art survey

Compiled 2026-10-13. Confidence tags: **H** = read directly in the cited source; **M** = inferred (reasoning given); **L** = rumour or unverified.
Nothing was downloaded. Only web pages, READMEs, text files and source code were read, through the GitHub API or a browser.

---

## 0. Executive summary (what matters for our patch)

1. **The community has standardised on one "modern" TP.exe: 3,734,528 bytes, image base 0x400000, no relocations.** This is the de-SafeDisc'd v2.0 exe that ships in the 2015 "SimThemePark-Win8-10-patch", in The Buzzy Project's Generic-Patch-Files and in HyperJeanJean's TPW-TPI-Fixes. TPW Patcher was developed against it. The original retail TP.exe is a SafeDisc loader of about 260 KB, and the real code lives in `TP.ICD` (3,734,573 bytes). Our own globals (height `0x007A1930`) match the HyperJeanJean hex pattern, so we target the same build. (H/M, see §1, §4, §7)
2. **TPW Patcher (MIT, June 2026) makes three code redirects and adds a new `.fix` PE section.** If we absorb it, we should apply the same three fixes as runtime hooks and avoid those five code sites. We should also accept a TP.exe that already contains its `.fix` section. (H, §1)
3. **Resolution:** `data\Resolution.sam` (`Res.RESOLUTION 1..7`) arrived with the official v2.0 patch. HyperJeanJean's "AltRes" exe swaps the hard-coded resolution table entries (pattern `<w32> C7 05 30 19 7A 00 <h32>`) for 4:3 modes 720x540, 960x720, 1440x1080, 1920x1440 and 2880x2160. Text survives because each new width stays in the same font-tier bracket as the original index. That supports our tier-mismatch diagnosis. (H for the pattern, M for the explanation, §2)
4. **The text fixes are not resolution-related.** The usp10→usp11 rename (plus an import-string edit) restores text on ride, gate and sign textures. Adam Hearn's font fix pre-places the game's generated TTFs in `data\tpwfnt`. A read-only `DONOTREMOVE.txt` stops the game from deleting that folder. (H, §3–4)
5. **OpenTPW (MIT)** plus its two documentation repos (no licence: cite only) document WAD/DWFB, SDT, WCT, SAM, BFST/BFUM strings, the TPWS save header, RSE/RSS ride scripts, TGQ/TQI video and a stub of BF4 fonts. MD2 models exist only as C# code. (H, §5)
6. **Other modern-Windows issues:** SafeDisc does not run on Win10 (secdrv removed), so a no-CD exe is effectively mandatory. The game runs too fast above 60 FPS. Older guides recommend single-CPU affinity and RENDER32/TEXTURE32=1, which fixes an invisible cursor. Large parks show white-square textures, fixed by a one-byte multiplier change. HyperJeanJean ships a newer QMixer.dll. DDrawCompat or dgVoodoo is used for Direct3D. (H/M, §6)

---

## 1. TPW Patcher (in-park level music)

- **Where it is published:**
  - GitHub repo `TheAussie/TPW-Patcher`, created 2026-06-13, last push 2026-06-15, **licence MIT** (`SPDX-License-Identifier: MIT` in the script). https://github.com/TheAussie/TPW-Patcher (H)
  - Mirrored as PCGW community file #4052 "Theme Park World - Level Music Patcher 1.0.0", submitted 13 June 2026, 28.8 kB. https://community.pcgamingwiki.com/files/file/4052-theme-park-world-level-music-patcher/ (H)
  - PCGW's "In-park level music does not play" fixbox points to it (edit dated 2026-06-13). https://www.pcgamingwiki.com/wiki/Theme_Park_World (H)
  - The README credits the investigation, testing and packaging to TheAussie, with AI-assisted implementation. (H)
- **Files:** `Patch-TPW-LevelMusic-Combined.py` (the patcher), `Test-TPW-PatchCompat.py` (read-only signature scanner), `docs/TPW-LevelMusic-Investigation.md` (full RE write-up), `release-notes-v1.0.0.md`. (H)
- **How it identifies the exe:** it does **not** use a hash or a file size. It checks that the file is PE32 with ImageBase `0x00400000`. It checks a fingerprint at the entry of `FUN_0051E730` (`A1 44 3A 80 00 8B 0D A0 3A 80 00`) and the original bytes at each redirect site. It refuses the exe if anything does not match. The test environment's TP.exe was 3,734,528 bytes. HyperJeanJean's `TP_AltRes.EXE` is also stated to patch cleanly. Source: script and investigation doc in the repo. (H)
- **Root cause and fix.** Every bug involves the level-music handle global `DAT_00803AA0` (H, investigation doc):
  - **v1 (save-state clobber):** `FUN_0051C400` restores the save or park sound state. It deserialises a 0x54-byte block over `&DAT_00803AA0` immediately after `FUN_0051E730` (level-music start) has created a valid handle, which zeroes the handle.
    - Fix: the call site at **VA 0x00415BA2 (file offset 0x14FA2)** goes from `E8 59 68 10 00` (call 0x51C400) to `E8 59 04 BB 00` (call `.fix+0x00`).
    - The 20-byte stub calls the original `FUN_0051C400` and then calls `FUN_0051E730` again, preserving EAX.
  - **v2 (per-tick selector overwrite):** `FUN_0051E790` (track selector, roughly every 32 ticks) runs `DAT_00803AA0 = FUN_0051BC40(DAT_00803AA0,4,i)` with no check, so a 0 return value overwrites a valid handle.
    - Fix: the function entry at **VA 0x0051E790** goes from `8B 44 24 04 8B` to `E9 rel32` (jmp `.fix+0x80`).
    - The stub keeps the old handle when the result is 0 and the old handle was non-zero.
  - **v3 (first-entry ordering):** on the first visit to a level, `FUN_0051E730` creates the music object before the asset loader has opened `\music\MusicHD.sdt`, so the object fails from the second tick onward.
    - Fix: the `ret`+11×`nop` at **VA 0x0051E764** (12 bytes) is replaced with `jmp .fix+0x50` plus 7 nops.
    - That hook sets a `REPRIME_PENDING` flag.
    - The combined stub at `.fix+0x80` re-runs `FUN_0051E730` once on the next selector tick. A `SUPPRESS_REARM` flag prevents a loop.
- **`.fix` section layout:**
  - The section is 0x1000 bytes, characteristics `0xE0000020`, so RWX.
  - The v1 call-site rel32 resolves to VA 0x00FC6000 (M: computed as 0x415BA7 + 0x00BB0459).
  - Contents: +0x00 v1 stub; +0x14 v2 stub (dead code); +0x40 the two flag bytes; +0x50 the e730 return hook; +0x80 the live e790 stub; the rest is 0xCC padding. (H)
- **Other fixes:** none. It is music-only. Its doc also records a QMixer.dll update and the "memory allocation" hex edit from TPW-TPI-Fixes as already applied, and as having no effect on music. (H)
- **Implications for us (M):**
  - We can either (a) implement v1–v3 as runtime hooks in our DLL, crediting TheAussie under MIT, and skip them when a `.fix` section is detected, or (b) require TPW Patcher.
  - With either option, our hooks must not patch VA 0x415BA2, 0x51E764–0x51E76F or 0x51E790–0x51E794, and must not assume the PE has only its original sections.
  - "Healed" saves: a save written while the fix is active stores valid sound state. (H, investigation doc)

## 2. Widescreen / high resolution

- **Official route:**
  - The v2.0 patch readme (dated 7 March 2000) says "There is a resolution.sam file that enables higher res running" and "The camera zoom can be unlocked with CTRL-SHIFT-ALT Space". Sources: https://github.com/The-Buzzy-Project/Generic-Patch-Files (`2.0Changelog.txt`) and https://www.moddb.com/downloads/theme-park-world-20-patch (H).
  - The patch installs it as `_Resolution.sam`. You rename it to `Resolution.sam` and set `Res.RESOLUTION N`. A 2014 guide recommends 5 (1280x1024) on 1080p monitors and also suggests single-CPU affinity. https://blog.andypotts.com/2014/10/running-theme-park-on-windows-8.html (H)
- **The "hex edit":** HyperJeanJean/TPW-TPI-Fixes (GitHub, created 2025-05-04, last push 2025-06-02, **no licence file**) gives the exact pattern. https://github.com/HyperJeanJean/TPW-TPI-Fixes (H)
  - **TPW:** find every `<width u32> C7 05 30 19 7A 00 <height u32>`, for example `80 02 00 00 C7 05 30 19 7A 00 E0 01 00 00` for 640x480. In other words: an immediate width (stored by the preceding instruction), then `mov dword [0x007A1930], height`.
  - TPI (Game.exe) uses `C7 05 C8 C8 A2 00`.
  - Add the new mode to `ddrawcompat.ini`.
  - 0x007A1930 is our height global, which matches our RE (`re/display-pipeline.md`). (H for the pattern, M for the match)
- **"AltRes" exe:**
  - The same repo ships `TP_AltRes.EXE` with the table changed to 1=512x384, 2=640x480, 3=720x540, 4=960x720, 5=1440x1080, 6=1920x1440, 7=2880x2160. All are 4:3, so there is no widescreen. (H)
  - The repo's default `Resolution.sam` forces 4 (1024x768).
  - The repo warns that higher resolutions reduce stability, mildly in TPW and severely in TPI. (H)
  - **Why text survives (M):** each AltRes width lies in the same font-tier bracket as the original width for that index (720 ≤ 800, 960 > 800, and so on). The tier chosen from the RESOLUTION index therefore matches the tier chosen from the real width. This fits our finding that text disappears when the two disagree. Arbitrary edits such as putting 1920x1080 in slot 2 break that agreement.
- **PCGW claim:** "hex edit … Hor+ … all the texts disappear". The field has no citation or bytes. https://www.pcgamingwiki.com/wiki/Theme_Park_World (H for the claim, L for its accuracy)
- **WSGF / ThirteenAG / Widescreen Fixes Pack:** none found.
  - Searches of wsgf.org and of the WidescreenFixesPack listings found no TPW or Sim Theme Park entry. (M: absence of evidence)
  - A VOGONS thread, "Low Quality Textures (W10)" (https://www.vogons.org/viewtopic.php?f=8&t=51689), reportedly notes that forcing a resolution via dgVoodoo does not make text higher-resolution. The site blocked automated reading, so this comes from a search snippet only. (L)
- **dgVoodoo integer scaling:** PCGW recommends forcing an integer multiple of the in-game resolution (e.g. 800x600 → 1600x1200) and leaving AF/AA alone, to reduce HUD glitches. Refcheck dated 2025-08-16. (H)

## 3. Adam Hearn's font file fix ("loads forever" / crash entering park)

- **Source page:** "Theme Park World/Sim Theme Park Fix for Windows 2000/XP".
  - Original: http://www.adamhearn.co.uk/games/themeparkworld/tpwwin2kfix.html
  - Archived copy: https://web.archive.org/web/20200628063012/https://www.adamhearn.co.uk/games/themeparkworld/tpwwin2kfix.html
  - Found on Christmas 2002 and tested on Win2K SP4 with v2.0. (H)
- **Diagnosis (author):**
  - First-chance exceptions occur in USP10.dll (Uniscribe).
  - The game "generated a host of True Type Fonts (TTF) on demand".
  - After a crash the generated TTFs were invalid, and Font Viewer would not open them until after a reboot.
  - The game worked better after a reboot: it reached player selection, then crashed on entering a park. (H)
- **What the fix is:** no code changes ("I've not reverse engineered anything"). It is an MSI (recommended) or a ZIP that puts pre-built TTFs into the game's `data\tpwfnt` folder, with **all TTFs set read-only**. Install order: clean install, v2.0 patch, reboot, then the fix. (H)
  - PCGW file #566, 663.7 kB, v1.0, describes it as "adding the created fonts to their appropriate file locations before they are created by the game". https://community.pcgamingwiki.com/files/file/566-theme-park-world-font-file-fix/ (H)
- **Why it works (M):** the game regenerates its TTFs in `data\tpwfnt` through Uniscribe/GDI and loads them. If generation fails or leaves a locked or invalid file (Win2K+ font cache or file locking), loading spins or crashes. Read-only, valid, pre-built TTFs mean generation never has to succeed.
- **The pre-built font set** appears in Buzzy Generic-Patch-Files and in HyperJeanJean's TPW/Fonts (identical git blob for `YOUNIA__.TTF`). It contains: `BIGLA___ CLUNA___ EGGITAOE GARGSA__ HAUNTAOE INTRAB__ INTRA___ KRELA___ LINUPA__ TANNA___ YOUNIA__` .TTF. HyperJeanJean adds `BIGLOA__ CLUNAB__ GARGA___ GARGSAI_ KRELAB__ TANNAO__`. (H)
- **HyperJeanJean's 2025 refinement:**
  - Read-only TTFs in tpwfnt actually broke the in-game fonts.
  - The recommended fix is now a read-only dummy `data\tpwfnt\DONOTREMOVE.txt` that stops the game from deleting the folder. Pre-placed TTFs are only a fallback.
  - Run as admin under Program Files, otherwise "minor font issues". (H) https://github.com/HyperJeanJean/TPW-TPI-Fixes (commit 2025-05-27)
  - **Implication (M):** the game deletes and recreates `tpwfnt` at startup. Our DLL could hook that path (or CreateFont/AddFontResource) instead of relying on file attributes.
- **Other items on the same page:**
  - "LpkPresent not found in usp10.dll": rename the game's usp10.dll to `_usp10.dll`.
  - Grant Everyone access to `HKLM\Software\Bullfrog Productions Ltd` for non-admin users.
  - For hangs or clipped announcer audio, Win98 compatibility mode.
  - Online play returns "Authorization Failed".
  - US vs EU differences (see §7). (H)

## 4. Community bundles ("Win8-10 patch", Golden Ticket, TPW-TPI-Fixes)

- **"SimThemePark-Win8-10-patch":**
  - Archived as `simthemeparkwin810patch.7z` (225 MB) in the archive.org item "Theme Park World / Sim Theme Park Patches", added 2025-01-31. https://archive.org/details/ThemeParkWorldSimThemeParkPatches (H, listing only)
  - Contents per the listing: `tp.exe` (3,734,528 B, dated 2015-05-31), `README FIRST.txt` (2015-10-11), `data\Movies`, `data\levels\{fantasy,hallow,jungle,space}\Music`, `data\tpwfnt`, `data\win2kfix`. (H)
  - The README says the .reg file "enables the no CD Executable". The patch is for Gold Edition, or for v2.0 with the patch applied. It warns about VirtualStore leftovers. (H)
  - **Author:** the README's disclaimer ("Buzzy wearing a coconut bra…") is word-for-word the one in ryanguy426's 2014 "ThemeLauncher" thread on Neoseeker, so ryanguy426 is the likely author. https://www.neoseeker.com/forums/2159/t2035630-themelauncher-launcher-for-sim-theme-park-fix-all-your-game-issues/ (M)
  - The same developer then appears to have started The Buzzy Project / Golden Ticket in 2016. (L)
  - Used by Lutris installers 11356 and 33046 (v2.0 patch, then merge, then import registry). https://lutris.net/games/install/11356/view (H)
- **What the music in these bundles is:**
  - Level music (`data\levels\*\Music\MusicHD.sdt` and `cat_music*.map`) is not installed to disk by the retail installer. It stays on the CD, so a no-CD exe leaves parks silent. Bundles and guides therefore copy those folders from the disc.
  - Sources: TPW-TPI-Fixes README ("Copy … Data/levels/*/Music from your disc") (H), and a search snippet of a guide saying music "breaks when you use a no-cd crack" (M).
  - TPW Patcher's doc found the level `MusicHD.sdt` "byte-identical to the original disc / win810 patch copies". So the bundled .sdt files are **not re-encoded**. The "re-encoded .sdt" idea is unsupported. (H for identity, L for any re-encode claim)
- **The usp10 → usp11 rename:**
  - Symptom: no text on ride signs, park gates or big signs. A Neoseeker thread (2011–2014) traced it to the system's newer usp10.dll being used. The game's own copy (v1.208.1890.1 reported working) restores the text.
  - The thread's fix: rename the game's DLL to `USP11.DLL` (or `USP1_.DLL`). Hex-edit the import string `USP10.dll` in TP.EXE (or TP.ICD for SafeDisc installs) to match, keeping 5 characters. https://www.neoseeker.com/forums/2159/t1631124-text-on-rides-entrances-signs-missing/ (H)
  - HyperJeanJean: "usp10.dll was renamed to usp11.dll as Windows seems to prefer using its own non-working version of usp10.dll". (H)
  - The `usp11.dll` in TPW-TPI-Fixes and in Buzzy Generic-Patch-Files is the same file (git blob `29b80d75…`, 291,504 B). (H)
  - **Mechanism (M):** `usp10.dll` is a KnownDLL on modern Windows, so the game folder copy is ignored. Renaming the DLL plus the import bypasses KnownDLLs.
  - **Our patch (M):** do not use a proxy named after a KnownDLL. Either keep the import-string approach or LoadLibrary the old Uniscribe privately.
  - Wine bug 34168 ("Ride Signs … not displayed", 2013, abandoned) is the same symptom. https://bugs.winehq.org/show_bug.cgi?id=34168 (H)
- **Golden Ticket (The Buzzy Project):**
  - **GPL-2.0**, created 2016-10-16, v0.2.1, last push 2022-12-07. https://github.com/The-Buzzy-Project/Golden-Ticket (H)
  - It downloads and copies `Generic-Patch-Files`: `tp.exe` 3,734,528 B, `TP.ICD` 3,734,573 B, `usp11.dll`, `data\tpwfnt\*.TTF`, `PermissionFix.exe`, `clokspl.exe`, `UNINST.dll`, `2.0Changelog.txt`.
  - It also copies `8-10-Configs` (`low/med/high.sam`).
  - `PermissionFix` grants Everyone FullControl on the game dir, `data` and the four level dirs. (H)
  - The org also has `Decompiled_Game_Code`: Snowman decompiles of TP.exe, Setup.exe and QMixer, v1.0, with an EA copyright disclaimer. Avoid it for licensing reasons. (H)
- **HyperJeanJean/TPW-TPI-Fixes:**
  - The most complete modern bundle (2025). (H)
  - Contents: DDrawCompat (`DDraw.dll` and `ddrawcompat.ini`: `FullScreenMode = borderless`, `TextureFilter = af16x`, extended `SupportedResolutions`); a newer `QMixer.dll` (315,392 B) that "seems to solve a few sound issues"; `usp11.dll`; `Resolution.sam`=4; a tuned `high.sam` (all quality options maxed, `RENDER32/TEXTURE32 1`); the `DONOTREMOVE.txt` trick; Gold Edition bonus WADs; patched `TP.EXE` / `TP_AltRes.EXE`; a dgVoodoo.conf (§6); `SFMAN32.DLL` for SoundFont MIDI.
  - **Memory fix:** white textures on large parks happen when 32-bit textures are enabled, because the texture memory estimate is too low. Replace `24 10 C1 E0 10` with `24 10 C1 E0 13` in TP.exe (shl eax,0x10 → 0x13, an 8× estimate). TPI: `24 0C C1 E7 10`→`…13`. (H)
  - **No licence file**, so its binaries and text are all-rights-reserved. Cite the byte patterns only. (H)

## 5. Open-source projects and file-format documentation

| Project | URL | Licence | Status |
|---|---|---|---|
| OpenTPW (C# reimplementation) | https://github.com/OpenTPW/OpenTPW (homepage https://opentpw.org) | **MIT** | Created 2019-06-16; last push 2026-09-07; "very early stage… not yet playable" (H) |
| OpenTPW.FileFormats (Astro/Starlight docs) | https://github.com/OpenTPW/OpenTPW.FileFormats (site https://docs.opentpw.org) | **none** (cite only) | last push 2025-02-21 (H) |
| opentpw-docs (older VuePress docs) | https://github.com/OpenTPW/opentpw-docs (site https://opentpw.gu3.me/formats/) | none | last push 2024-02-06 (H) |
| WADView (BFWD viewer/extractor) | https://github.com/OpenTPW/WADView | **MPL-2.0** | 2020 (H) |
| RSSEQCompiler (+VSCode syntax) | https://github.com/OpenTPW/RSSEQCompiler | none | 2023 (H) |
| ThemeParkWorldRPC (Discord RPC, reads process memory) | https://github.com/xezno/ThemeParkWorldRPC | **MPL-2.0** | 2020 (H) |
| Golden Ticket | see §4 | GPL-2.0 | 2022 (H) |
| TPW Patcher | see §1 | MIT | 2026 (H) |

**OpenTPW README status table (H):** Textures .WCT, Settings .SAM, Sounds .SDT/.MP2 and Strings .BFMU/.BFST/.BFUM are implemented. Models .MD2, Map .MAP, Ride scripts .RSE and Saves .TPWS are partial. Fonts .BF4, Lip sync .LIPS, Materials .MTR and Video .TQI are not implemented.

**What the docs cover (H unless noted; summarised, not copied):**
- **WAD / BFWD, magic "DWFB":**
  - Header: version, 64 pad bytes, file count, file-list offset and length.
  - 40-byte entries: name offset and length, data offset, size, compression (4 = RefPack, 0 = none), decompressed size.
  - A backslash prefix in an entry name switches the current directory for that and later entries.
  - The PS2 version uses an incompatible "FKNL" WAD.
  - Docs: `wad.md` (FileFormats), `bfwd.md` (opentpw-docs). Code: `source/OpenTPW.Files/Formats/Archive/WadArchive.cs` and `Compression/Refpack.cs`.
- **SDT (sound bank):**
  - A count, an offset table, then per-entry header: header size, data size, 16-byte name, sample rate, resolution, type (2 = WAV, 3 = "old WAV", 36 = MP2 64 kbps mono, 37 = MP2 112 kbps stereo), samples.
  - Payload is MPEG-1/2 Layer II.
  - Docs: `sounds.md`. Code: `SDTArchive.cs` and `MP2File.cs`.
- **SAM:** plain text, `key <ws> value`, `#` comments, quoted strings. Docs: `sam.md`. Code: `SAMParser.cs`.
- **Strings:**
  - `BFUM` maps Unicode to and from Bullfrog multibyte (MBtoUNI.dat / UNItoMB.dat).
  - `BFST` .str files hold a count, offsets, then records of `01` + a 24-bit length + BFMU characters.
  - Docs: `strings.md`. Code: `BFSTReader.cs` and `BFMUReader.cs`.
- **WCT textures:**
  - Header: compression byte (0x12 zlib, 0x13 LZSS), bit count, width, height, Y/Cb/Cr/A quantisation scales, two block sizes.
  - Decoding is wavelet-like ("D4 coefficients" in code), with chroma at half size, then YCbCr→RGB. Dimensions are aligned to a power of two of at least 8.
  - Docs: `texture.md`. Code: `Formats/Image/TextureFile.*.cs` and `LZSS.cs`.
- **Saves .TPWS / .INTS / .LAYS:**
  - Magic `F4 01 00 00`, then a copyright block to 0x33B and padding to 0x603.
  - Then type `00 01 22 19`, version byte `0x85`, an online flag, then a "BILZ" chunk header and a zlib stream.
  - Docs: `saves.md`. Code: `SaveReader.cs`.
- **RSE / RSS ride scripts:** an assembly-like VM. The full instruction list is in `vm/instructions.md` (~17 KB). The compiler is RSSEQCompiler.
- **Video (.TGQ / .TQI):**
  - EA chunk container (FourCC + size). Video is EA TQI (`pIQT`) and audio is SCxl (`SCHl/SCCl/SCDl/SCEl…`).
  - Same family as Dungeon Keeper 2. Refers to https://wiki.multimedia.cx/index.php/Electronic_Arts_Formats.
  - Docs: `video.md`. FFmpeg's `eatgq`/`eatqi` decoders likely play these (M).
- **BF4 fonts (F4FB?):**
  - Only a stub: magic, max width and height, offset count, offsets, then per glyph char, size, width, height, x/y offset and advance.
  - Docs: `bf4.md` (opentpw-docs). Not implemented in code.
  - Our RE is ahead here (M).
- **MD2 models (Bullfrog "M3D2", not Quake MD2):**
  - The docs page is a TODO.
  - The partial loader `Formats/Model/ModelFile.cs` reads frame count @0x36, mesh count @0x44, texture-list offset @0x50, frame-list offset @0x54, mesh pointer @0x70. Per mesh: vertex, UV, face and material offsets and counts, plus a transform matrix; 20-byte texture names. (H, code read)
- **MAP, MTR, LIPS:** docs are TODO stubs. (H)
- **Also:** `bullfrog-library.md` lists Bullfrog "Library85" source paths from a Theme Park Inc debug build, which can help name functions such as TbWadFile, TbRefPackFileAccess and LbScreen. (H)
- **ThemeParkWorldRPC (MPL-2.0):** reads cash @0x007CB2DC, level name @0x00871F74, golden tickets @0x00752EC4, golden keys @0x007CC4B8, and more, from its `TpwRpc.cs`. These are useful cross-checks for our symbol names; the exe build is unstated. (H for the addresses, M for the build)

## 6. Modern-Windows problems beyond PCGamingWiki

- **SafeDisc:**
  - SafeDisc 1.35 is on the original retail release and 1.41 on re-releases (PCGW Availability). (H)
  - The Win10 secdrv removal (2015) is general knowledge. (M)
  - In practice every modern guide uses the 3,734,528-byte no-CD exe. TP.ICD crashes ("tp.icd has stopped working") on Win7 and Win8 (andypotts blog, Neoseeker). (H)
  - Wine bug 37802 (2014): the installer fails to install the SafeDisc driver. Bug 46943 (2019, unconfirmed): an access violation after the splash with the SafeDisc exe, which "runs as it should" with the no-CD exe. https://bugs.winehq.org/show_bug.cgi?id=46943 (H)
- **dgVoodoo:**
  - PCGW: copy DDraw.dll and D3DImm.dll from MS\x86; General = Unspecified; do not force AF or AA; force an integer multiple of the in-game resolution. (H)
  - HyperJeanJean's dgVoodoo.conf (v0x286): ScalingMode `stretched_ar`, DirectX VideoCard `geforce_fx_5700_ultra`, VRAM 512, Filtering 16, dgVoodooWatermark off, ExtraEnumeratedResolutions covering the AltRes set, `AppControlledScreenMode true`. HyperJeanJean actually prefers DDrawCompat. (H)
  - A Reddit snippet: GeForce4 Ti 4800 with 128 MB VRAM helped. (L)
  - VOGONS p=610582: dgVoodoo gave a black screen and then a crash; a d3d8 wrapper fixed it. (L, snippet only)
- **Frame rate / speed:** PCGW says the game is unplayable above 60 FPS and recommends capping at 60 (citation needed, 2021). Our `re/frame-timing.md` should own this. (H)
- **Multi-core:** the andypotts guide (2014, still working on Win10 in 2016) sets single-CPU affinity. (H) Root cause unknown (L).
- **Cursor:** the cursor is invisible in 16-bit render mode on Win8+. Set `GraphicalOptions.RENDER32` and `TEXTURE32` to 1 in low/med/high.sam (andypotts). Win10 Technical Preview also had a cursor-missing report (ThemeLauncher thread). (H)
- **Main-menu freeze, can't select park, animals don't flap:** PCGW and Reddit point to permissions or VirtualStore. Golden Ticket's PermissionFix grants Everyone FullControl. HyperJeanJean and the Win8-10 README: delete the VirtualStore copy and run as admin. (H/M)
- **QMixer / QSound:** QSound middleware (PCGW). HyperJeanJean ships a "bit newer" QMixer.dll (315,392 B) that fixes "a few sound issues". Adam Hearn: hangs and clipped announcer audio, fixed via Win98 compatibility mode. (H) Exact QMixer versions are unknown. (L)
- **"EA Online" DLL errors:** nothing specific found. Online play is dead ("Authorization Failed", Adam Hearn). (H for the dead online, L for any DLL error)
- **White textures in big parks:** see the §4 memory fix. (H)
- **Crashes:** TPW-TPI-Fixes lists random crashes, possibly related to graphics settings, as a known issue. (H)
- **Wine / Proton:**
  - AppDB app 7545 could not be read; it is behind an Anubis bot challenge, which we did not attempt.
  - Bugzilla shows sign text missing (34168), installer and SafeDisc failures (37802, 5692, both duplicates), and 46943.
  - Lutris has working Wine installers that use the v2.0 patch, the Win8-10 bundle and registry imports, with no DLL overrides listed. (H)
  - TPW Patcher says it works on Wine prefixes. (H)
  - No ProtonDB entry was found (the game is not on Steam or GOG; PCGW has no store IDs). (H/M)
- **Installer:** the disc autorun install fails about 50% of the time on Win7+. Retry from `autorun.exe`. HyperJeanJean suggests rebooting after the steps. Registry keys the game needs:
  - `HKLM\Software\Bullfrog Productions Ltd\Theme Park World` with Version, Language and BuildTypeCode
  - `App Paths\TP.exe` with `Path`
  - Sources: Lutris script, TPW Patcher doc. (H)

## 7. Versions and editions

- **v1.0** retail (Nov 1999), then **v1.1** (patch; "not recommended" per Adam Hearn), then **v2.0**.
  - "Theme Park World Patch 2" readme is dated 7 March 2000. The file `TPPatchTwoEUROAMER20000324a.exe` is 5,947,043 B; the name suggests a 2000-03-24 build covering the European and American SKUs (M).
  - Upgrades English 1.0 and 1.1. Not needed for **Gold Edition**, which already includes v2.0 plus the online bonus rides on its second disc. (H, ModDB/PCGW #565/Adam Hearn)
- **v2.0 changes (H, 2.0Changelog.txt):**
  - Gameplay and online bug fixes.
  - Coaster wheels animate; coaster and water-coaster sounds fixed.
  - Dual Voodoo3 support.
  - Faster first person; first-person fog distance per quality level.
  - `resolution.sam` for higher resolutions.
  - Zoom unlock with CTRL-SHIFT-ALT-Space.
  - "2.0" shown in the lobby; ALT-V in a park reports the version.
- **Theme Park World vs Sim Theme Park (US):** per Adam Hearn, only the name and logo and an American announcer differ. (H) The same patch serves both (M). No exe-level diff was found (L). Golden Ticket and the Win8-10 bundle target "SimTheme Park" install paths but ship the same 3,734,528-byte tp.exe (H/M).
- **Re-releases:**
  - Sold Out Software, 2003 UK, described by one user as "Windows Vista Compatible" (Neoseeker 2011, L). It uses SafeDisc 1.41 (PCGW, H).
  - An EA Classics edition (UK/FR) is listed at computinghistory.org.uk with no content details. https://www.computinghistory.org.uk/det/60112/ (H for existence, L for contents)
- **Mac:** Mac OS 2000 and OS X 2002 ports (Feral/Zonic), plus the "TPW 2.1.4" Mac patch on PCGW. Not relevant to the PE patch. (H)
- **Localised builds:** the Lutris installer lists EN/DE/FR/SV/DA disc folders. PCGW lists English, French, Hebrew, Japanese and Spanish. Non-English exes may differ, and none of the community byte patterns have been verified against them. (H/M)

---

### Licences we might build on (summary)
- **MIT:** OpenTPW, TPW Patcher. Reuse with attribution.
- **MPL-2.0:** WADView, ThemeParkWorldRPC. File-level copyleft if we copy code.
- **GPL-2.0:** Golden Ticket and some Buzzy repos. Avoid copying code into a non-GPL patch.
- **No licence (all rights reserved):** OpenTPW.FileFormats, opentpw-docs, RSSEQCompiler, TPW-TPI-Fixes, Buzzy Generic-Patch-Files / 8-10-Configs. Cite facts and byte patterns only.
- **Do not use:** the Buzzy `Decompiled_Game_Code` (EA code) or any redistributed EA binaries (tp.exe, TP.ICD, usp11.dll = Microsoft Uniscribe, QMixer = QSound).
