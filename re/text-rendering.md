# Theme Park World: Text Rendering Engine Analysis & Resolution Bug Investigation

**Date**: 2026-10-13  
**Target Binary**: `TP.exe` (Theme Park World / SimCoaster, 1999, Bullfrog / EA)  
**Image Base**: `0x00400000`  

---

## 1. Executive Summary & Root Cause of the Disappearing Text Bug

### Why All Text Disappears Above 640x480
When the game resolution is changed above 640x480 via hex edit, **all text in the game completely disappears**. The investigation revealed that this is caused by a **fatal desynchronization between two font-handling subsystems**:

1. **Resolution Tiering System**: The game divides UI fonts into **4 resolution tiers**:
   - **Tier 0**: Low resolution (< 513 px wide, e.g. 512x384)
   - **Tier 1**: Standard resolution (513–640 px wide, e.g. 640x480)
   - **Tier 2**: High resolution (641–800 px wide, e.g. 800x600)
   - **Tier 3**: Ultra-high resolution (> 800 px wide, e.g. 1024x768 and up)
   Each tier contains an array of **13 distinct font styles/sizes** (24 bytes per font struct, 312 bytes per tier).

2. **Font Loading (`Language_LoadResourcesAndFonts`, `0x00419710`)**:
   During startup (in `WinMain`, `0x0045a960` at line `0x0045ab6f`), the game reads `DAT_007858ec` (derived from `Resolution.sam`). In standard installations, `DAT_007858ec` evaluates to tier 1. Therefore, `0x00419710` **only loads the 13 fonts for Tier 1** into memory at offset `+0x5a` (`0x0078b698`). The slots for **Tier 0, Tier 2, and Tier 3 are left completely empty (all NULL / zero)**.

3. **Resolution Initialization (`Video_InitResolution`, `0x00449ec0`)**:
   Later during startup (at line `0x0045abc0`), the video mode initializes. The hex edit patches the hardcoded resolution dimensions here (normally `0x280` = 640 and `0x1e0` = 480) to values like 800x600, 1024x768, 1280x720, or 1920x1080 into `DAT_007a1968` (width) and `DAT_007a1930` (height).

4. **Font Tier Selection (`Font_SelectResolutionTier`, `0x0041a5a0`)**:
   Immediately following `Video_InitResolution`, `0x0041a5a0` is called with the newly initialized screen width `DAT_007a1968`. It executes the following logic:
   - If width < 513: `DAT_0078b534 = 0`
   - If width < 641: `DAT_0078b534 = 1`
   - If width < 801: `DAT_0078b534 = 2`
   - Else (width >= 801): `DAT_0078b534 = 3`
   Because the screen width was hex-edited above 640, `DAT_0078b534` is set to **2** or **3**.

5. **The Disappearance (`Font_GetFromActiveTier`, `0x00485a70` and `UI_SetWidgetTextAndFont`, `0x00485d20`)**:
   Every time the game renders UI text (tooltips, buttons, ride names, visitor thoughts, dialogs, HUD), it requests font structures using `Font_GetFromActiveTier(font_id)` or indexes `&DAT_0078b5c4 + DAT_0078b534 * 0x138`. Because `DAT_0078b534` is 2 or 3, it indexes into **Tier 2 or Tier 3**—**which were never loaded and contain only NULLs**.
   The software glyph rasterizer (`Font_BlitGlyph4BitAlpha`, `0x006b15f0`) receives NULL font / glyph pointers and aborts drawing.
   **Result: Not a single glyph is drawn. All UI text completely vanishes.**

### Proposed Fix / Mod Solution

> **Corrected 2026-10-13 (lead agent, checked against the disassembly).** The original proposal here quoted the wrong original bytes and ended its patch with `ret`. `0x0041a5a0` is `__thiscall` with one stack argument and returns with `ret 4` (`c2 04 00`), so a plain `ret` would unbalance the stack. Forcing tier 1 would also put the 640x480 fonts on a 1080p screen.

What the code actually does (H, read from the instructions):
- The loader `0x00419710` maps the `RESOLUTION` setting to a tier: 1→0, 2→1, 3→2, 4..7→3 (setting 0 uses the Options value). It loads that tier only.
- `Video_InitResolution` `0x00449ec0` maps the same setting to 512x384 / 640x480 / 800x600 / 1024x768 / 1280x1024 / 1600x1200 / 2048x1536.
- `Font_SelectResolutionTier` `0x0041a5a0` picks the tier from the real width (<=512, <=640, <=800, else 3).
- **These agree for every stock `RESOLUTION` value**, so `data\Resolution.sam` with `RESOLUTION 4..7` should keep text (to be confirmed in play). They disagree only when the width table itself is hex-edited (the PCGamingWiki method), because the loader still loads the tier the setting names.
- Original bytes at `0x0041a5a0`: `8b 44 24 04 3d 00 02 00 00 7f 0a c7 41 04 00 00 00 00 c2 04 00 ...`

Fix for arbitrary sizes (e.g. 1920x1080): when the patch sets the size, make the loader's tier and the selector's tier the same one (tier 3 above 800 wide), e.g. set `RESOLUTION` to 4..7 so the loader takes tier 3, then override the width and height after `0x00449ec0` returns and before `0x0041a5a0` is called (the calls at `0x0045abbb` and `0x0045abcc` in WinMain). Whether tier-3 fonts and the `data\Init\1024` art suit 1080p+ is a layout question for later.

---

## 2. Architecture Overview: The Two Text Rendering Subsystems

Theme Park World has **two distinct, completely separate text rendering architectures**:

| Feature | System 1: In-Game UI / HUD / Menus | System 2: 3D Ride Signs |
| :--- | :--- | :--- |
| **Technology** | Bullfrog proprietary 4-bit antialiased rasterizer | Microsoft Uniscribe (`USP10.dll`) + Win32 GDI |
| **Font Format** | Proprietary `'F4FB'` binary glyph tables (4-bit alpha) | TrueType fonts (`.ttf`) extracted from `fonts.wad` |
| **Target Surface** | DirectDraw backbuffer surface memory (`lpSurface`) | Memory `CreateDIBSection` (8-bit grayscale), downsampled |
| **Composition** | Software alpha-blended direct blit (`0x006b15f0`) | Dynamic 128x128 texture applied to 3D sign polygon |
| **Resolution Impact** | **Disappears above 640x480 due to unpopulated font tier** | Independent (renders to fixed 128x128 texture) |

---

## 3. Investigation Findings

### 1. The Uniscribe Call Chain
**Confidence**: **H** (Confirmed directly from call xrefs and disassembled instructions)

In the entire executable, Uniscribe (`USP10.dll`) is imported and called strictly by a single function: `Uniscribe_RenderTextLineToDIB` (`0x005eab70`). No other function in `TP.exe` calls Uniscribe APIs.

#### Full Call Chain:
```
WinMain / Game Event Loop (0x0045a960 / 0x0054e360)
  └─► Park Initialization / Ride Placement (0x00404f70)
  │     └─► RideSign_CreateForRide (0x004682b0)
  └─► Ride Renaming via User Interface (0x005d8470)
        └─► RideSign_UpdateRenamedRide (0x00468420)
              │
              └─► RideSign_GenerateTexture (0x00467a80)
                    │   Calls GetDC(hWnd) (HWND from DAT_007a130c)
                    │
                    └─► SignTexture_RenderDualLineText (0x005ecb40)
                          │   Calls twice: line 1 (offset +0x384), line 2 (offset +0x3f0)
                          │
                          └─► Uniscribe_RenderTextLineToDIB (0x005eab70)
                                │
                                ├─► CreateDIBSection (0x005eac47)
                                ├─► CreateCompatibleDC (0x005eac73)
                                ├─► CreateFontIndirectA (0x005eaca3)
                                ├─► ScriptStringAnalyse (0x005eace0)
                                ├─► ScriptStringGetLogicalWidths (0x005eacef)
                                ├─► ScriptStringFree (0x005ead37)
                                ├─► ScriptString_pSize (0x005ead70)
                                ├─► [Dynamic Font Size Binary Search Loop]
                                │     ├─► CreateFontIndirectA (0x005eadc6)
                                │     ├─► ScriptStringAnalyse (0x005eae03)
                                │     ├─► ScriptStringGetLogicalWidths (0x005eae12)
                                │     ├─► ScriptStringFree (0x005eae5a)
                                │     ├─► ScriptStringAnalyse (0x005eae89)
                                │     └─► ScriptString_pSize (0x005eae93)
                                ├─► ScriptStringOut (0x005eaf5e)
                                └─► ScriptStringFree (0x005eaf68)
```

---

### 2. The Device Context (DC)
**Confidence**: **H** (Confirmed from disassembly of `0x00467a80` and `0x005eab70`)

- In `RideSign_GenerateTexture` (`0x00467a80` line 3544):
  ```c
  hDC = GetDC(DAT_007a130c); // DAT_007a130c is the main game window HWND
  SignTexture_RenderDualLineText(hDC, ...);
  ReleaseDC(DAT_007a130c, hDC);
  ```
- In `Uniscribe_RenderTextLineToDIB` (`0x005eab70` lines 6318–6325):
  - The window DC is passed as `param_2`.
  - A memory DIBSection is created using `CreateDIBSection(param_2, lpbmi, 0, &local_8, 0, 0)`.
  - A compatible memory DC is created: `hdc = CreateCompatibleDC(param_2)`.
  - The DIBSection is selected into `hdc` via `SelectObject(hdc, h)`.
  - GDI mapping mode is set: `SetMapMode(hdc, MM_TEXT)`.
  - `ScriptStringOut` draws directly into this memory DC `hdc`.
  - The DIBSection is created with an **8-bit grayscale palette** (256 shades of gray, RGB 0,0,0 to 255,255,255 populated in `bmiColors`).
  - Dimensions: `biWidth = width * 2`, `biHeight = -height * 2` (top-down 2x supersampled bitmap).

---

### 3. Font Creation & Dynamic Binary Search
**Confidence**: **H** (Confirmed from instructions and decompiler in `0x005eab70`)

- `CreateFontIndirectA` is called exclusively in `0x005eab70` at `0x005eaca3` and `0x005eadc6`.
- **`LOGFONT` Parameters**:
  - The base `LOGFONTA` structure is stored in the sign parameter block at `param_1 + 0x10`, initially populated from the ride's `.sgn` configuration file.
  - The font face name is specified by the `.sgn` file corresponding to the ride.
  - Initial `lfHeight` is read from `param_1 + 0x14`.
- **Dynamic Binary Search Font Sizing**:
  - Font sizes are **not hardcoded**; they are dynamically scaled to ensure the text fits the sign.
  - After calling `ScriptStringAnalyse` and `ScriptString_pSize`, if the text pixel width exceeds the sign width (`*param_4 * 2 <= text_width`), the engine initiates a **binary search loop** (`0x005ead90`–`0x005eaed0`).
  - It brackets `min_height` (initially 1) and `max_height` (initial `lfHeight`).
  - On each iteration:
    - Calculates `mid_height = min + (max - min) / 2`.
    - Updates `LOGFONT.lfHeight`.
    - Re-creates the font via `CreateFontIndirectA`.
    - Measures text width using `ScriptStringAnalyse` and `ScriptString_pSize`.
    - Adjusts `min_height` or `max_height` until `max - min < 3` and the text fits within `sign_width * 2`.
  - Background is set to transparent/black (`SetBkColor(hdc, 0)`), text is white (`SetTextColor(hdc, 0x00FFFFFF)`), and the string is rendered centered via `ScriptStringOut` (`x = 256 - text_width / 2`).

---

### 4. Coordinate Systems
**Confidence**: **H** (Confirmed from mathematical expressions in `0x005f18d0` and `0x00485722`)

#### A. 3D Ride Signs (Uniscribe):
- Rendered in texture-local coordinates `[0, 256]` (supersampled space).
- Centered horizontally at `x = 256 - (width / 2)`.
- Downsampled to `[0, 128]` texture coordinates before being mapped onto the 3D sign polygon.

#### B. UI / HUD / Menus:
- Virtual coordinate system based on a canonical base grid of **1024x768** and **640x480**:
  - Horizontal scaling: `(x * DAT_007a1968) * 0.00048828125` (where `0.00048828125 = 1 / 2048`) or normalized by `1 / 1024` (`0.0009765625`).
  - Vertical scaling: `(y * DAT_007a1930) * 0.0006510417` (where `0.0006510417 = 1 / 1536`) normalized to canonical base `480.0`.
- Position offsets on widgets are computed in virtual pixels and scaled to the current display resolution before passing to the glyph blitter.

---

### 5. The Text Surface & DirectDraw Backbuffer Blitting
**Confidence**: **H** (Confirmed from `0x006b15f0` and DirectDraw structure analysis)

#### 3D Ride Signs:
- Renders to a 256x256 memory DIBSection.
- Downsamples 2x2 pixels into a 128x128 8-bit texture buffer (`param_4[3]`).
- Composited into `sign1.tga` / `sign2.tga` and bound to the 3D ride model as a Direct3D texture.

#### In-Game UI Text:
- **No intermediate text texture is used**.
- Glyphs are blitted **directly into the locked DirectDraw backbuffer surface**:
  - `0x006b15f0` (`Font_BlitGlyph4BitAlpha`) takes:
    - `lpSurface = *(int *)(surface_desc + 0x18)`
    - `lPitch = *(int *)(surface_desc + 0x24)`
    - Pixel offset = `y * lPitch + x * 4 + lpSurface`
  - Reads 4-bit alpha nibbles from the `'F4FB'` glyph table.
  - Blends color channels:
    ```c
    dest_channel = ((src_color - dest_color) * alpha) / 15 + dest_color;
    ```
  - Directly writes final 32-bit (or 16-bit) ARGB pixels into the screen buffer.

---

### 6. Connection to Resolution & Why Text Disappears
**Confidence**: **H** (Confirmed by cross-referencing disassembled instructions across `0x00419710`, `0x0041a5a0`, `0x00449ec0`, and `0x00485a70`)

#### Font Memory Layout at `0x0078b530` (Language Manager):
- Base address: `0x0078b530`
- Active tier index (`DAT_0078b534`): offset `+0x04`
- Font array starting address: `0x0078b560` (offset `+0x30` / `+0x0c` dwords)
- **13 fonts per tier**, 24 bytes (`0x18`) per font = **312 bytes (`0x138`) per tier**:
  - **Tier 0** (512x384): `0x0078b560` (offset `+0x30`)
  - **Tier 1** (640x480): `0x0078b698` (offset `+0x168` / `+0x5a` dwords)
  - **Tier 2** (800x600): `0x0078b7d0` (offset `+0x2a0` / `+0xa8` dwords)
  - **Tier 3** (>800): `0x0078b908` (offset `+0x3d8` / `+0xf6` dwords)

#### The Loading vs. Selection Disconnect:
1. `Language_LoadResourcesAndFonts` (`0x00419710`) runs first. It calculates which tier to load based on `Resolution.sam` (`DAT_007858ec`). In default configs, it selects Tier 1 and loads resource IDs `0x416`, `0x411`, `0x412`, etc., into Tier 1 (`0x0078b698`). Tiers 0, 2, and 3 are zero-filled.
2. `Video_InitResolution` (`0x00449ec0`) runs next. The hex edit sets `DAT_007a1968` to e.g. 1024 or 1920.
3. `Font_SelectResolutionTier` (`0x0041a5a0`) runs immediately after:
   ```c
   if (width < 641) DAT_0078b534 = 1;
   else if (width < 801) DAT_0078b534 = 2;
   else DAT_0078b534 = 3;
   ```
   Because width > 640, `DAT_0078b534` is set to **2** or **3**.
4. During rendering, `Font_GetFromActiveTier` (`0x00485a70`) computes:
   ```x86asm
   mov  eax, [DAT_0078b534]     ; tier (2 or 3)
   lea  edx, [eax + eax*2]      ; tier * 3
   lea  eax, [ecx + edx*4]      ; font_id + tier * 13
   lea  eax, [eax + eax*2]      ; 3 * (font_id + tier * 13)
   lea  eax, [eax*8 + 0078b54c] ; 24 * (font_id + tier * 13) + base
   ```
   It returns a pointer inside Tier 2 or Tier 3.
5. Because Tier 2 and Tier 3 were **never populated**, the structure contains all zeroes (`font_ptr = NULL`).
6. Every UI widget checks `if (font != NULL) { render(); }`. Finding NULL, it renders nothing.

---

### 7. AddFontResourceA & TrueType Font Storage
**Confidence**: **H** (Confirmed from binary inspection of `data\fonts.wad` and disassembly of `0x005c5010`)

- **Physical Storage**:
  - TrueType fonts are stored inside the Bullfrog WAD archive: `testgame\data\fonts.wad` (header magic: `DWFB` = Bullfrog WAD).
  - Contains full TTF font files with standard tables: `cmap`, `cvt `, `fpgm`, `glyf`, `hdmx`, `head`, `hmtx`, `kern`, `loca`, `maxp`, `name`, `post`, `prep`.
- **Runtime Extraction & Registration**:
  1. Ride sign configurations (`.sgn`) reference font filenames located in `Data\Fonts`.
  2. `WAD_ExtractFileToDisk` (`0x005c5010`) reads the TTF file data from `data\fonts.wad` into memory.
  3. Writes the TTF out to disk in the `Data\Fonts` or `tpwfnt` directory.
  4. Calls `AddFontResourceA(path)` (`0x005ec680` at `0x005ec762` and `0x005ec894`) to register the font with Windows GDI.
  5. The font is tracked in a `std::vector<std::string>` list of active font resources (`DAT_00789c1c`).
  6. When the ride is removed or park unloaded, `RemoveFontResourceA` is invoked (found in `0x00413140`, `0x00415140`, `0x0048d640`, `0x004993e0`, `0x005e2010`, `0x005e27b0`), and the temporary file is unlinked.

---

## 4. Established Function Names (`names.tsv` format)

| Address | Name | Note |
| :--- | :--- | :--- |
| `0x00419710` | `Language_LoadResourcesAndFonts` | Loads string tables and 13 UI bitmap fonts for configured resolution tier |
| `0x0041a5a0` | `Font_SelectResolutionTier` | Sets active font resolution tier DAT_0078b534 based on screen width |
| `0x0041a5f0` | `Language_UnloadFonts` | Releases loaded font resources across all 4 resolution tiers |
| `0x00449ec0` | `Video_InitResolution` | Sets DAT_007a1968 / DAT_007a1930 and initializes video mode |
| `0x00467a80` | `RideSign_GenerateTexture` | Generates composite sign texture with sign1.tga/sign2.tga and rendered text |
| `0x004682b0` | `RideSign_CreateForRide` | Sets up sign generation for a ride from Data\Fonts |
| `0x00468420` | `RideSign_UpdateRenamedRide` | Updates sign texture when a ride is renamed |
| `0x00485a70` | `Font_GetFromActiveTier` | Returns font struct for font ID (0..12) from active resolution tier |
| `0x00485d20` | `UI_SetWidgetTextAndFont` | Sets widget text and selects appropriate font tier |
| `0x005c5010` | `WAD_ExtractFileToDisk` | Extracts a file from WAD archive to temporary disk file |
| `0x005eab70` | `Uniscribe_RenderTextLineToDIB` | Renders line to 2x supersampled DIB using Uniscribe and downsamples to texture |
| `0x005ec220` | `Sign_CleanupTextureGen` | Cleans up temporary resources for sign texture generation |
| `0x005ec3a0` | `Sign_LoadConfiguration` | Parses .sgn sign configuration file |
| `0x005ec680` | `Font_RegisterTTFResource` | Extracts TTF from fonts.wad and calls AddFontResourceA, tracks in vector |
| `0x005ecb40` | `SignTexture_RenderDualLineText` | Renders line 1 and line 2 to sign texture buffer |
| `0x0065c388` | `UI_SetCurrentFont` | Sets the current font on a UI element |
| `0x006b0680` | `Font_LoadF4FBResource` | Resource loader for Bullfrog F4FB font format |
| `0x006b15f0` | `Font_BlitGlyph4BitAlpha` | Software 4-bit alpha font glyph rasterizer onto DirectDraw surface |
