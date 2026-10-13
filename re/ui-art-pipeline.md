# UI art, text and movie pipeline (for an "HD UI" pass)

Static analysis of `TP.exe` (v2.0 noCD, base 0x400000), plus the game data in `testrun\data`. Addresses
are VAs. Confidence: **H** = read in the exe with `tpwre.py dis`, **M** = clear from the decompiler or
inferred from the mechanism, **L** = guess. Names in section 8 are new; the rest come from `re/names.tsv`.
Helper: `tools/uiextract.py`. It lists WADs, unpacks RefPack, lists sprites with their UI hash, textures and
sizes, and converts TGAs to PNG.

## 0. Summary

- **The 2D UI is not 2D.** Every widget image is a small **3D mesh** (Bullfrog M3D2 `.MD2`) from `ui.wad`,
  textured with **WCT** (wavelet-compressed) textures. The 3D engine draws it as a textured DrawIndexedPrimitive
  mesh in normalised device coordinates. Positions come from the 2048x1536 virtual layout. **H**
- **The source art is tiny.** The largest UI texture is **128x128**. Buttons are 64x64 or 32x32, and icons
  32x32. The main-menu "Quit"/player-slot sign (`b_login`) uses two 128x128 textures stretched to about
  1455x138 px at 2560 wide. **H** (data)
- **Textures are quantised to ARGB4444.** All engine textures share one texture-cache format: 4444 unless the
  detail preset's TEXTURE32 is 1 (then 8888). Every stock preset says 0, and the HD patch's forced TEXTURE32 is
  known to corrupt textures (src/compat.h). **H**
- **Filtering is bilinear for magnification and point for minification**, by the engine's state word. **H** for the
  translation, **M** for the UI's exact bits. The blur is a 64→~400 px bilinear stretch of 4-bit-per-channel art.
- **Text** is drawn by the CPU, 1:1, from the tier-3 (1024x768-sized) `.bf4` fonts. It is sharp but small. **H**
- **The splash, legal and loading screens** use the **640** Init tier at 2560x1440, because any width other than
  400/512/640/800/1024 falls back to `"640\"`. They are drawn with a nearest-neighbour CPU stretch. **H**
- **Movies** are 320x352 anamorphic. They are decoded in software to the display format, then stretched by
  `IDirectDrawSurface2::Blt` to full width x H·352/480. **H**

## 1. UI art pipeline

### 1.1 Containers and formats (H unless noted)

- **`ui.wad` (DWFB).**
  - Header layout:
    - "DWFB", version 2, 64 pad bytes.
    - count at 0x48, name-table offset and length at 0x4c/0x50.
    - 40-byte entries from 0x54: `0, id, nameOff, nameLen, dataOff, packedSize, packing (4 = RefPack, 0 = stored), size, 0, 0`.
  - A name containing `dir\` sets the directory for that entry and the ones after it (prior-art.md §5, confirmed on the file).
  - Contents: 1202 entries.
    - 278 `.MD2` sprites at the root.
    - `stexture\` (415 WCT).
    - `textures\` (412 WCT).
    - 3 `qickload.txt`.
  - Mounted by `Resources_MountAll` 0x00408470 (`WadMount`, `WadMount_ReadDirectory` 0x0061a250).
- **Texture tier = TEXTUREQUALITY, not resolution.**
  - `UI_SelectTextureDirs` 0x004688b0 (from `Game_Init` 0x0054dd32) picks between two directory pairs:
    - `stexture`/`ssharete` when the detail preset's TEXTUREQUALITY (0x00785874) is 0 or 1, or dialog.tcf debug flag 0x800.
    - Otherwise `textures`/`sharetex`.
  - The high preset (3) uses `textures`.
  - `stexture` is the same art at 0.8x (64→51, 32→26) or identical.
  - **There is no art above `textures`.** Sizes there: 128x128 (61 textures), 64x64 (204), 32x32 (326), 16x16 (12).
- **WCT.**
  - 0x1c-byte header (read by `WCT_LoadHeader` 0x00564c00):
    - codec byte 0x12 (zlib) / 0x13 (LZSS).
    - alpha flag.
    - bits 24/32.
    - block code.
    - width and height (u16).
    - four u16 quantisers.
    - colour-stream size, alpha-stream size, 4 unknown bytes.
  - Then a "BILZ"-wrapped compressed colour stream and, when bits = 32, an alpha stream.
  - Dimensions in ui.wad are 8..128 and mostly powers of two (51 and 26 in `stexture`).
  - The pixel codec is class `WaveletCodec` (vtable 0x007026d8, ctor 0x005916c0, global 0x00879a64 made by `WaveletCodec_Create` 0x00564b20). It is about 19 KB of code. **M**
  - OpenTPW's `texture.md` / `TextureFile.*.cs` describes the format (cite; MIT code).
- **MD2 sprite (Bullfrog M3D2, not Quake).** Example `b_login.MD2`:
  - Texture count u16 at 0x36.
  - Texture table at [0x50]: 8 bytes per texture, followed by 20-byte names (`button_text1.tga`, `button_text2.tga`).
  - Mesh at [0x78]: its name pointer is at mesh+0x54 (`b_login`), and that name is what the UI hash is taken of.
  - 12 vertices: a 3-part, horizontally stretchable button.
  - **UVs are floats in 0..1** (e.g. 0.996, 0.645). **H**
- **Sprite banks.**
  - `UI_LoadSpriteBanks` 0x004891d0 walks `data\ui` (with `here2000.txt`/`qickload.txt`). For each `.md2` it tries three loaders:
    - `UI_LoadCheckButtonSprite` 0x00488d20 (`_cbut`).
    - `UI_LoadSliderSprite` 0x00488ee0 (`_slider`).
    - `UI_LoadImageSprite` 0x00489060 (skips names starting with `!`, and `w_small_shadow.md2`).
  - Each loader calls `Model_Load` 0x0046dcf0 and `Model_BindTextures` 0x0046ead0. That binder only checks the `.wct`, then the `.tga`, exists in the texture dir.
  - It then wraps the model in a sprite renderer (classes 0x006fee38 / 0x006fee60 / 0x006fee10) and calls `UI_RegisterSpriteBank` 0x00659a58(renderer, name). That links the sprite into `g_SpriteBankList` 0x00faa610 (+0x10 hash, +0x14 next).
  - `UI_FindImageByName` 0x00659bc9 hashes with `UI_HashName` 0x006588b6. The hash is h = 0, then h = (h ^ (signed char)c) * 0x2f, case-sensitive. "b_login" = 0xee32be94 and "lolight" = 0x0f898e57, which agrees with frontend-menus.md. **H**

### 1.2 From file to D3D texture

| Step | Function | What it does | Conf. |
|---|---|---|---|
| model textures | `Model_LoadTextures` 0x00575e10 (from model load 0x00579e00 → 0x00575c60) | for each 20-byte texture name: `TexMgr_FindOrLoad(dir, name)`; material bits 0x10/0x20 → texture flags 0x10000/0x20000 (wrap per axis) | H |
| cache by name | `TexMgr_FindOrLoad` 0x00575870 | 4096-entry table at `g_TexMgr` 0x0087a268 (dir, name, texture, refcount); path = data root 0x00786de4 + dir + `\` + name | H |
| create | `Texture_CreateFromFile` 0x005755f0 | allocates 0x60-byte engine texture(s) (`Texture_SetSize` 0x00590eb0), flags from the load result `g_TexLoadFlags` 0x0087a24c: 0x10000000 = wavelet source, 0x100 / 0x200 from alpha (1-bit / full) | H |
| **load pixels** | **`Texture_LoadImageFile` 0x00574bd0** (path,&w,&h,flags,lut,bpp) | **the one file→pixels function for every engine texture and the splash screens**. Replaces the extension with `WCT` and tries `WCT_LoadHeader`; on success returns the WCT object (still compressed) and its size. Otherwise reads an uncompressed TGA (18-byte header) and converts it with `Image_ConvertPixelsLUT` 0x00575160 to `bpp` = `g_TexBytesPerPixel`[format] (0x007016f8: 2,2,2,4) | H |
| slot pool | `TCache_Init` 0x0058da40 / `TCache_CreateSlots` 0x0058d670 / `TCache_CreateSlotsOfSize` 0x0058d1d0 | pre-creates **square** D3D textures in sizes **256,128,64,32,16,8** (0x007022e8). `Texture_SetSize` puts each texture in class 0..5 by max(w,h), so **anything over 128 goes to a 256 slot and 256 is the maximum** | H |
| D3D surface | `D3DTex_Create` 0x0058b4f0 (texture class vtable 0x007021f0) | CreateSurface: DDSD 0x21007, caps 0x10405008 (TEXTURE, VIDEOMEMORY, MIPMAP, COMPLEX, ALLOCONLOAD) in the pixel format from `D3D_GetTextureFormat` 0x00573820 | H |
| format | 0x0044e1eb in `Display_CreateGameWindowAndInit` | format = ((g_DisplayFlags & 0x400000) \| 0x800000) >> 22, i.e. **2 = ARGB4444, or 3 = ARGB8888 with TEXTURE32**; `g_TexFormat` 0x008bd554 (software renderer: 3). Formats 0..3 = 565/1555/4444/8888, matched by `D3D_EnumTextureFormatCallback` 0x00572f70 (table 0x007015fc, descriptors 0x008bd558). 565/1555 are never used by the cache | H |
| upload | `TCache_UploadTexture` 0x0058f190 (per frame via 0x0058f940 / 0x0058cf40) | picks a slot (smaller textures can share a bigger slot at x,y), `D3DTex_LockStaging` 0x0058b810 (system-memory staging surface from 0x0058b0b0, caps 0x1800), `TCache_WriteTexture` 0x00590350, `D3DTex_UnlockAndUpload` 0x0058baf0 (box-filter mips `D3DTex_BuildMip` 0x0058b890 for 4444/8888, then Blt DDBLT_WAIT into the video-memory texture) | H |
| UV fix-up | `TCache_WriteTexture` 0x00590350 | stores the per-texture UV scale (+0x20/+0x24 = w/slotSize) and offset (+0x28/+0x2c = slot position + half-texel inset 0x008c1628), so **mesh UVs stay 0..1 for any texture size** | H |
| pixel write | **`D3DTex_WriteTexels` 0x0058bd10** | writes into the locked staging surface: a wavelet source calls the source's vtable +4 (`WCT_DecodeInto` 0x00564ef0, `WaveletCodec` vf04 0x00597100, bpp = 0x007011a0[format]); a raw source is a nearest-neighbour copy from w x h to the slot size (+0x14/+0x16) in 2 or 4 bytes per pixel | H |
| LOD squeeze | `TCache_Rebalance` 0x0058e390 / 0x0058f190 | under memory pressure halves a texture's +0x14/+0x16 (nearest-neighbour downscale on the next upload). This would also hit upscaled UI art | M |

### 1.3 How sprites are drawn (H unless noted)

- **Sprite renderer.**
  - The ctor 0x00476a90 ORs **0xc8010** into every material of the model. At **exactly 640 wide** it ORs **0x80c8010**.
  - `UISprite_SetRect` (class 0x006fee10 vf0c 0x00476f10) builds the matrix with `UISprite_BuildMatrix` 0x00476c50. The matrix maps the widget rect (in 2048x1536 virtual units, y flipped) through scale 2/2048 and 2/1536 (0x0074f888/0x0074f88c) into **NDC −1..1**. Constants 4/3 and 3/4 are used for rotation.
  - `UISprite_Show` (vf14 0x00476bf0) adds the model to the render list (0x0045bd30).
  - The viewport is the whole render target. So **the UI is stretched to the full screen, non-uniformly at 16:9** (x 1.25, y 0.9375 at 2560x1440). No code computes a separate scale: the stretch falls out of the NDC mapping. **H** for the mapping, **M** for the viewport.
- **Draw.**
  - Meshes go World_RenderView → 0x00582170 → 0x00581db0 → `Mesh_DrawMaterial` 0x00581880 → `Render_BeginPrim`. The state word comes from `Mesh_MaterialStateFlags` 0x00581790:
    - material 0x8000000 → state 0x20000.
    - texture flags 0x30000 → 0x400000.
    - texture flag 0x100000 → 0x40000.
  - Then `D3D_DrawPrim` 0x005680b0: `D3D_ApplyStateFlags` 0x00567620 and `D3D_SetTexture` 0x005675c0 (IDirect3DDevice3::SetTexture at 0x005675d9), then DrawIndexedPrimitive. **M** for the UI taking this path.
- **Sampler translation.** In 0x00567620 / `D3D_SetStageStateIfChanged` 0x00567a70:

  | State bit | Texture-stage setting |
  |---|---|
  | 0x20000 | MAGFILTER **point** if set, else **linear** |
  | 0x400000 | MINFILTER linear if set, else **point**; ADDRESS wrap if set, else clamp |
  | 0x40000 | MIPFILTER none if set, else [dev-0x20] |
  | 0x8000 | Z off |

  **H**
- **Result: bilinear magnification on UI sprites at every width except 640, where they get point sampling (the
  1:1 case).** **M** dgVoodoo2 may add its own forced filtering.

### 1.4 Which art at 2560x1440

| Art | Source used | How it reaches the screen | Conf. |
|---|---|---|---|
| UI sprites (buttons, panels, HUD) | `ui.wad\textures\` (high preset), ≤128 px | textured mesh, 4444, bilinear, stretched 2048x1536 → 2560x1440 | H/M |
| UI text | tier 3 `.bf4` (`data\Language\<lang>\*.bf4`, the 1024x768 sizes) | CPU blit 1:1 into the locked back buffer | H |
| Splash / Legal / Welcome | `data\Init\640\*.tga` (fallback string 0x00768dd8 `"640\"`; only exact widths 400/512/640/800/1024 match) | `Screen_BlitFullscreenImage` 0x00587130: CPU nearest-neighbour stretch to the full screen, aspect ignored | H |
| Loading bar | no art: 3 solid rects in 640x480 units (`UI_RenderLoadingBar` 0x00587a70); the bar frame is baked into `Welcome.tga` | renderer fill | H |
| Cursors | `data\ui\cursors\*.tga` 128x32 | colour cursor sprite (mouse-input.md) | M |
| Movies | 320x352 TQI | DirectDraw Blt stretch | H |

## 2. Hook points for an upscaler

1. **`Texture_LoadImageFile` 0x00574bd0 (best: one choke point, before the cache).**
   - Every engine texture (UI, sky, park) and the splash images load through it.
   - **Upscaled replacement.** Hook it (cdecl, 6 args, returns a data pointer; sets *w/*h and `g_TexLoadFlags` 0x0087a24c). For a path under `ui.wad`'s texture dirs:
     1. Look for `data\hd\ui\<name>.png` (the future AI art).
     2. Failing that, decode the WCT and upscale it: 4x for ≤64 and 2x for 128, capped at **256**. Use any non-AI scaler (xBRZ/ScaleFX-style edge-directed for the flat-shaded art, or Lanczos plus unsharp mask) and alpha-aware filtering (premultiply, then unpremultiply).
     3. Return a **raw** buffer in the cache format (`g_TexBytesPerPixel`[g_TexFormat]), with flags `0x300 | 2` (raw, full alpha). Leave bit 0x04, the wavelet-source flag, clear.
   - The engine then sizes the texture from the new w/h, places it in a ≤256 slot, and fixes the UVs (§1.2 UV fix-up). So **a bigger texture is a drop-in**. **H** for the mechanism, **M** for the exact flag set (copy what the TGA path returns).
   - **Decoding the WCT for the upscale.** Either:
     - call the game's codec: `WCT_LoadHeader` object → vtable +8 = `WCT_DecodeLinear` 0x00564e20 → `WaveletCodec` vf08 0x00595920(codec, w, h, bpp, alpha, colour, dst, quantisers), into a linear buffer, with `g_TexFormat` temporarily 3 (needs the 8888 descriptor at 0x008bd558+0x60 to exist); or
     - port a decoder. **M**
   - **Limits.**
     - **256** is the cache maximum. Raising it means extending the size table (0x007022e8), the slot arrays (6 per kind in the 0x1928-byte tcache object), `TCache_SizeToSlot` 0x005848b0 and `Texture_SetSize` 0x00590eb0. That is invasive (L).
     - Raise the cache budget (the existing TextureMemoryX8 patch) so `TCache_Rebalance` doesn't halve UI art again.
2. **`D3DTex_WriteTexels` 0x0058bd10 (alternative: at upload).**
   - This is the last point that sees both the engine texture (with its name, via the TexMgr table) and the locked surface.
   - Useful for dithering 8-bit art down to 4444 (instead of truncating) when TEXTURE32 stays off. Not useful for changing size, since the slot is already chosen.
3. **Texture format (8888).**
   - The single cache format is chosen at 0x0044e1eb. Forcing it to 3 (TEXTURE32) is the real fix for the 4444 banding.
   - The HD patch's attempt (`Texture32Read` 0x0044db6a) "corrupts every texture" (src/compat.h). The cause is **not found** here.
   - All the paths read in this pass handle 4 bytes per pixel correctly: `g_TexBytesPerPixel`, 0x007011a0, the slot bpp (0x007021c0), the mip builder and `D3DTex_WriteTexels`.
   - Suspects (M/L):
     - the codec's output stage, which reads the format's alpha mask from 0x008bd574 + 32·format (vf00 at 0x00595680).
     - `Sky_SampleColours`, the ride-sign and procedural-texture writers that read `g_TexFormat` (xrefs to 0x008bd554).
     - the 16-bit display mode (565 back buffer) mixed with 8888 textures under dgVoodoo.
   - **Next experiment:** TEXTURE32 with RENDER32, one texture dumped from the staging surface. UI-only 8888 is not possible inside the cache, which has one format.
4. **Sampler state.**
   - To force bilinear on UI magnification and minification, patch `D3D_ApplyStateFlags` 0x00567620: the MINFILTER push at 0x00567910–0x0056792a and MAGFILTER at 0x0056788e.
   - Or clear the 640-only 0x8000000 in the sprite ctor 0x00476a90.
   - The present mapping is already bilinear for magnification, so this is a minor gain.
   - With upscaled textures, minification matters (256 → ~180 px). Enable linear MIN for UI: the material → state 0x400000 path, or a patch of the 0x00567910 test.

## 3. Text

- **Confirmed.** `Font_BlitGlyph4BitAlpha` 0x006b15f0 is slot 0x18 of the glyph blitter class vtable 0x0070a188 (data ref 0x0070a1a0).
  - It reads 4-bit alpha nibbles from the glyph and blends `dst + (src - dst)·a/15` per channel.
  - It writes 32-bit pixels (`x*4`) **1:1 into the locked back buffer**. There is no scaling.
  - Sibling classes 0x0070a1a8 / 0x0070a1c8 / 0x0070a1e8 have the same 8-slot layout; they are probably the other pixel formats (M).
- **So text is sharp but at its 1024x768 size.** The tier-3 fonts are chosen by `Font_SelectResolutionTier` 0x0041a5a0. The README already notes "text stays at its 1024x768 size". **H**
- **Fonts are loose files**: `data\Language\<lang>\*.bf4` (F4FB: GAME6..12, GAME*AA, GAMEBOLD*, CASH*, DATE*, CONSOLE6, COURIER8). Loader: `Font_LoadF4FBResource` 0x006b0680. **H** (files)
- **Sharper and larger text options:**
  - (a) **Generate new F4FB fonts at W/1024 times the size from a TTF** (FreeType/GDI grayscale AA quantised to 4 bits) and load them as tier 3 when the width exceeds 1024. Load time is the only hook: redirect the tier-3 file names, or write the files to a folder the loader reads first. The widget rects already scale with the screen, so larger glyphs fit. This is the best fit.
  - (b) Hook the glyph blit (slot 0x18) to draw scaled glyphs. Simple, but scaling 4-bit glyphs blurs them.
  - (c) Draw all text ourselves after the frame (needs the string, rect and colour from the widget's TextRenderer). This is a big job.

## 4. Movies (TGQ)

Findings below come from a delegated pass. I re-checked the Blt and the header myself.

- **Decoded frame.** Every `pIQT` chunk starts `40 01 60 01 …`, i.e. **320x352** (anamorphic, shown 2x wide), at about 30 fps. Audio is EA-XA. **H**
- **Decode thread.** `TGQ_StartPlayerThread` 0x0066efd0 (names.tsv calls it `QSound_StartMixerThread`; that name is wrong) → `TGQ_PlayerThreadProc`.
  - `TGQ_DecodeFrame` 0x00670c20 locks the movie surface (0x00670d77), decodes the TQI macroblocks straight to RGB, then unlocks (0x00670fc1).
  - The RGB decoders are `TQI_DecodeFrameRGB16` 0x00671800 and `TQI_DecodeFrameRGB32` 0x006718a0, with 4:2:0 IDCT blocks (0x006747d0).
  - Row converters: 0x00675d90 (16-bit) and 0x006758c0 (32-bit).
  - **M/H**
- **Surfaces.**
  - `TGQ_CreateMovieSurfaces` 0x006719e0 makes two 320x352 system-memory offscreen surfaces in the display format.
  - The YUY2 overlay paths (0x00671cb0, 0x00670ad0, 0x00671ea0) are never used: both callers pass 0 for flag 0x200 to `TGQ_SelectOutputMode` 0x00670660.
  - **H**
- **To screen.**
  - `TGQ_PresentFrame` 0x0066f600 calls **IDirectDrawSurface2::Blt at 0x0066f9f9** (flags DDBLT_WAIT, src {0,0,320,352}), then Flip at 0x0066fa4d in full screen.
  - The dest rect comes from `TGQ_ComputeDestRect` 0x006711a0: x 0..W, y H·64/480 .. +H·352/480. That is full width, so 16:9 is stretched.
  - **The scaling is DirectDraw's Blt stretch**, so the filtering is whatever dgVoodoo does. **H**
- **Hook.**
  - Replace the Blt at 0x0066f9e2–0x0066f9fb with a call: esi = player, edi = dest surface, [params+0x1c] = the 320x352 source.
  - Lock the source, scale it on the CPU to the aspect-correct rect: 2x horizontal anamorphic, so the true picture is 640x352 → 4:3 → H·4/3 wide. Use bilinear or Lanczos plus sharpen. Lock the dest, write, unlock.
  - At 32 bpp the source is already 8 bits per channel. At 16 bpp capture YCbCr in the row converters instead. This runs on the decode thread.

## 5. Clean-art extraction

- **What the extractor needs.**
  - WAD and RefPack: done in `tools/uiextract.py`.
  - The MD2 texture list and sprite name, for the hash and the name: done (`sprites` lists 609 sprite/texture rows).
  - WCT header: done.
  - **WCT pixels: not done.** They need the wavelet decoder.
  - TGA → PNG: done. It handles type 1 and 2, 8/24/32 bpp, and either origin. Verified on `Init\640\Legal_English.tga` and `Init\512\Splash_English.tga`.
- **Baked text versus runtime text.**
  - **Runtime text (art is text-free):**
    - every widget button (`b_login` = `button_text1/2.tga`, blank button plates; labels come from `Widget_SetText` / `UI_SetTitleText` and the fonts).
    - Esc menus (TextMenu on `lolight`).
    - HUD numbers (CASH/DATE fonts).
    - The `b_*` icon textures are pictograms. The `*d` variants are the disabled or down state; `*X` variants also exist.
  - **Baked (H, visual check):**
    - `Init\*\Splash_<lang>.tga` (Bullfrog logo and the line "Bullfrog is an Electronic Arts Inc. company").
    - `Legal_<lang>.tga` (copyright and QSound strip).
    - `Welcome_<lang>.tga` (localised strip composited at 47% height).
    - `Welcome.tga` (the TPW logo and the loading-bar frame are painted in).
  - **Probably baked (L, check after the dump):** `tpw_logo.wct`, `voted4.wct`, `postcard\legal.tga`, `postcard\tpwlogo.tga`, and any `I_*` / `ti_*` thought icons with glyphs.
- **Init tiers.** 512/640/800/1024 hold real images; 400 has 6-byte "Hack !" stubs. 1024 sizes: Splash and Welcome 1024x768, Legal 1024x116, Welcome_<lang> 461x65. Use 1024 as the clean master.

## 6. Recommended "HD UI" design

1. **Quick wins (byte patches).**
   - (a) String 0x00768dd8 `"640\"` → `"1024\"`, so the splash, legal and loading screens use the 1024-tier art. H
   - (b) Replace `Screen_BlitFullscreenImage` 0x00587130 (call sites 0x005877d3, 0x00587d21) with a bilinear or Lanczos scaler, aspect-correct with letterboxing. Move the loading bar to match (`UI_RenderLoadingBar` scales from 640x480). Optional: a `data\hd\Init\<name>.png` override.
2. **UI textures.**
   - Hook `Texture_LoadImageFile` 0x00574bd0. For names in the UI texture dirs:
     - if `data\hd\ui\<name>.png` exists, use it (the player's art);
     - otherwise decode the WCT, upscale (non-AI, edge-directed or Lanczos plus sharpen) to ≤256, and cache the result on disk.
   - Return raw pixels in the cache format; the UVs fix themselves.
   - Pair this with the texture budget x8 and with fixing TEXTURE32. While TEXTURE32 stays off, do ordered dithering to 4444 in the hook.
3. **Sampler.**
   - Make UI minification linear (patch at 0x00567910 or the material state), because 256-px art is now shown smaller than its size.
   - Keep magnification linear. Drop the 640-only point flag.
4. **Text.** Generate scaled F4FB fonts (tier 3 replacements sized ×W/1024) from a TTF at start-up or offline.
5. **Movies.** Hook the Blt at 0x0066f9f9: CPU bilinear or Lanczos plus mild sharpen from 320x352 to the 4:3 rect H·4/3 x H·352/480, centred, on the decode thread.

## 7. Recommended extractor design

`tools/uiextract.py` (in place) plus a WCT decode step:

1. `raw` / `sprites` give every sprite: name, UI hash, textures (both tiers), sizes, alpha.
2. **WCT pixels.**
   - Preferred: a **dump mode in the HD DLL** that, after engine init, calls `Texture_LoadImageFile` and `WCT_DecodeLinear` (§2.1) for every `textures\*.wct` with `g_TexFormat` = 3, and writes PNG 8888. This uses the game's own decoder, so it is bit-exact.
   - Alternative: a Python port of the wavelet decoder (OpenTPW documents it; MIT code, cite).
3. Write `out\ui\<texture>.png` plus `manifest.tsv` (sprite → textures, hash, W×H, tier). Note which have text by the list in §5.
4. Convert `Init\1024\*.tga`, `postcard\*.tga`, `2dmap\*.tga`, `ui\cursors\*.tga` to PNG with `tga2png`.
5. The same names and paths are what the replacement loader looks for (`data\hd\ui\<texture>.png`). The player's AI art drops in under the same name, at any size ≤256 (power-of-two not required).

## 8. New names (names.tsv format)

```
0x004688b0	UI_SelectTextureDirs	H: (lowQuality) -> "stexture"/"ssharete" else "textures"/"sharetex" into 0x007af650/0x007af720; from Game_Init 0x0054dd32 (TEXTUREQUALITY 0x00785874 <=1 or dialog flag 0x800) [ui-art-pipeline]
0x00488d20	UI_LoadCheckButtonSprite	M: "_cbut" md2 -> sprite class 0x006fee38, UI_RegisterSpriteBank [ui-art-pipeline]
0x00488ee0	UI_LoadSliderSprite	M: "_slider" md2 -> class 0x006fee60 [ui-art-pipeline]
0x00489060	UI_LoadImageSprite	H: md2 (not '!' or w_small_shadow) -> class 0x006fee10, registered under the mesh name ([[0x78]+0x54]) [ui-art-pipeline]
0x004891d0	UI_LoadSpriteBanks	M: walks data\ui md2s (here2000.txt/qickload) through the three loaders [ui-art-pipeline]
0x0046dcf0	Model_Load	M: loads an md2 model; 0x0046ead0 binds textures [ui-art-pipeline]
0x0046ead0	Model_BindTextures	H: per texture: tries dir\name.wct then .tga (existence only), "Could not load texture" [ui-art-pipeline]
0x00476a90	UISprite_Ctor	H: class 0x006fee10; ORs 0xc8010 into each material, 0x80c8010 when g_ScreenWidth==640 [ui-art-pipeline]
0x00476c50	UISprite_BuildMatrix	H: widget rect (2048x1536 virtual) -> NDC matrix (2/2048, 2/1536), optional rotation [ui-art-pipeline]
0x00476f10	UISprite_SetRect	M: Class_006fee10 vf0c; computes bounds and calls UISprite_BuildMatrix [ui-art-pipeline]
0x00476bf0	UISprite_Show	M: Class_006fee10 vf14; adds/removes the model from the render list [ui-art-pipeline]
0x006fee10	UISprite::vftable	H: image sprite renderer (Widget_SetImage target) [ui-art-pipeline]
0x00564b20	WaveletCodec_Create	H: creates the WCT codec 0x00879a64 (class 0x007026d8) [ui-art-pipeline]
0x00564c00	WCT_LoadHeader	H: reads the 0x1c-byte WCT header (codec 0x12/0x13), colour and alpha streams [ui-art-pipeline]
0x00564e20	WCT_DecodeLinear	M: source vtable +8: codec vf08(codec,w,h,bpp[g_TexFormat],alpha,colour,dst,q0..q3) [ui-art-pipeline]
0x00564ef0	WCT_DecodeInto	M: source vtable +4: codec vf04 into a pitch rect (codec 0x13 only, else 0) [ui-art-pipeline]
0x007026d8	WaveletCodec::vftable	M: vf00 0x00593ab0, vf04 0x00597100 rect decode, vf08 0x00595920 linear decode [ui-art-pipeline]
0x00574bd0	Texture_LoadImageFile	H: (path,&w,&h,flags,lut,bpp): tries <path>.WCT (compressed object) else raw TGA converted to bpp; sets g_TexLoadFlags; the choke point for an HD replacement loader [ui-art-pipeline] (also: Image_LoadTGA)
0x00575160	Image_ConvertPixelsLUT	H: 24/32-bit TGA -> target format via LUT, flips bottom-up [ui-art-pipeline]
0x005755f0	Texture_CreateFromFile	H: allocates 0x60-byte engine texture(s), flags from g_TexLoadFlags (0x10000000 wavelet source) [ui-art-pipeline]
0x00575870	TexMgr_FindOrLoad	H: (dir,name) cached by name in g_TexMgr (4096 entries), path = root+dir+"\"+name [ui-art-pipeline]
0x00575e10	Model_LoadTextures	H: per model texture: TexMgr_FindOrLoad; material 0x10/0x20 -> texture 0x10000/0x20000 [ui-art-pipeline]
0x00574a20	TexMgr_Init	H: creates g_TexMgr, loads Data\Generic\defaulttexture\notfound.tga [ui-art-pipeline]
0x0087a268	g_TexMgr	H: texture name table (dir,name,texture,refs) x 4096, count +0x10000 [ui-art-pipeline]
0x0087a24c	g_TexLoadFlags	H: result flags of Texture_LoadImageFile (4 wavelet, 2/1 alpha, 0x300 raw) [ui-art-pipeline]
0x008bd554	g_TexFormat	H: texture-cache format 0 565, 1 1555, 2 4444, 3 8888 (from (g_DisplayFlags&0x400000) at 0x0044e1eb) [ui-art-pipeline]
0x008bd558	g_TexFormatDescs	H: 4 x 32-byte DDPIXELFORMAT from EnumTextureFormats [ui-art-pipeline]
0x007016f8	g_TexBytesPerPixel	H: 2,2,2,4 by format [ui-art-pipeline]
0x007022e8	g_TCacheSlotSizes	H: 256,128,64,32,16,8 (square slots; 256 is the max texture) [ui-art-pipeline]
0x00572f70	D3D_EnumTextureFormatCallback	H: matches ARGB bit counts (table 0x007015fc) to formats 0..3 [ui-art-pipeline]
0x00573820	D3D_GetTextureFormat	H: (&desc, format) -> 0x008bd558 + 32*format [ui-art-pipeline]
0x0058cff0	TCache_Create	H: new 0x1928 tcache (0x008c4b44), TCache_Init(format) [ui-art-pipeline]
0x0058da40	TCache_Init	H: "Texture Cache Initialization", staging pool, budget, slots [ui-art-pipeline]
0x0058d400	TCache_ComputeBudget	H: texture memory budget (shl 16 at 0x0058d46d, x8 patch) [ui-art-pipeline]
0x0058d670	TCache_CreateSlots	H: creates slots of each size from g_TCacheSlotSizes [ui-art-pipeline]
0x0058d1d0	TCache_CreateSlotsOfSize	H: (size,&n,format,mips) [ui-art-pipeline]
0x005848b0	TCache_SizeToSlot	H: 256..8 -> 0..5 (1,2,4 -> 8,7,6) [ui-art-pipeline]
0x00590eb0	Texture_SetSize	H: w/h at +0x10/+0x12 (+0x14/+0x16 target), size class by max(w,h) into flags low bits [ui-art-pipeline]
0x0058c9f0	Texture_Touch	M: marks a texture used this frame and queues it for upload [ui-art-pipeline]
0x0058f190	TCache_UploadTexture	H: pick slot, lock staging, TCache_WriteTexture, unlock/upload [ui-art-pipeline]
0x0058f940	TCache_UploadPending	M: uploads the queued list (0x18c4) [ui-art-pipeline]
0x0058e390	TCache_Rebalance	M: halves texture sizes under memory pressure [ui-art-pipeline]
0x00590350	TCache_WriteTexture	H: calls slot WriteTexels, stores UV scale/offset (+0x20..+0x2c) [ui-art-pipeline]
0x0058b4f0	D3DTex_Create	H: CreateSurface DDSD 0x21007 caps 0x10405008 in the cache format [ui-art-pipeline]
0x0058b810	D3DTex_LockStaging	H: Lock the system-memory staging surface (0x0058b1f0 pool) [ui-art-pipeline]
0x0058baf0	D3DTex_UnlockAndUpload	H: build mips (0x0058b890), Blt DDBLT_WAIT staging -> texture [ui-art-pipeline]
0x0058b890	D3DTex_BuildMip	H: 2x2 box filter for 4444 and 8888 [ui-art-pipeline]
0x0058b0b0	D3DTex_CreateStaging	H: 2 sysmem surfaces per size (caps 0x1800) [ui-art-pipeline]
0x0058bd10	D3DTex_WriteTexels	H: wavelet decode or nearest-neighbour copy into the locked staging surface (2/4 bpp) [ui-art-pipeline]
0x007021f0	D3DTex::vftable	H: [0] Create 0x0058b4f0, [1] Release 0x0058b790, [4] Lock 0x0058b810, [5] Unlock 0x0058baf0, [7] WriteTexels 0x0058bd10 [ui-art-pipeline]
0x005675c0	D3D_SetTexture	H: thiscall(stage, IDirect3DTexture2*); SetTexture at 0x005675d9 [ui-art-pipeline]
0x00567a70	D3D_SetStageStateIfChanged	H: (flags, stage, mask, tss, ifSet, ifClear) [ui-art-pipeline]
0x00581790	Mesh_MaterialStateFlags	M: material/texture flags -> engine state word (0x8000000 -> 0x20000 point mag, tex 0x30000 -> 0x400000) [ui-art-pipeline]
0x00581880	Mesh_DrawMaterial	M: Render_BeginPrim with Mesh_MaterialStateFlags [ui-art-pipeline]
0x00586e40	Splash_LoadImage	H: loads an Init TGA in the display format via Texture_LoadImageFile [ui-art-pipeline]
0x00586f40	Splash_CompositeOverlay	H: copies a Legal/Welcome strip into the Welcome image (y from image width) [ui-art-pipeline]
0x008c4748	g_LoadingScreen	H: +0x10 welcome image, +0x14 legal image, +0x2c blits done [ui-art-pipeline]
0x00768dd8	s_InitTierFallback	H: "640\" used for any width not in {400,512,640,800,1024}; 8 bytes, patch to "1024\" [ui-art-pipeline]
0x0070a188	GlyphBlit32::vftable	M: 8-slot glyph blitter, slot 0x18 = Font_BlitGlyph4BitAlpha (32-bpp); siblings 0x0070a1a8/0x0070a1c8/0x0070a1e8 [ui-art-pipeline]
0x0066efd0	TGQ_StartPlayerThread	H: _beginthread(0x0066f050) movie player thread, waits state 3 [ui-art-pipeline] (was: QSound_StartMixerThread)
0x0066f600	TGQ_PresentFrame	H: Blt movie surface -> primary/back (0x0066f9f9), Flip 0x0066fa4d [ui-art-pipeline]
0x0066f9f9	TGQ_PresentFrame_BltCall	H: Surface2::Blt(dest, src+0x1c, {0,0,320,352}, DDBLT_WAIT); scaler hook site [ui-art-pipeline]
0x00670660	TGQ_SelectOutputMode	H: 1 RGB Blt (always), 2/4 YUY2 need flag 0x200 [ui-art-pipeline]
0x00670c20	TGQ_DecodeFrame	H: Lock 0x00670d77 / TQI decode / Unlock 0x00670fc1, decode thread [ui-art-pipeline]
0x006711a0	TGQ_ComputeDestRect	H: x 0..W, y H*64/480, h H*352/480 [ui-art-pipeline]
0x00671800	TQI_DecodeFrameRGB16	H [ui-art-pipeline]
0x006718a0	TQI_DecodeFrameRGB32	H [ui-art-pipeline]
0x006719e0	TGQ_CreateMovieSurfaces	H: 2 x 320x352 sysmem offscreen surfaces in the display format [ui-art-pipeline]
0x00675d90	TQI_RowYCbCrToRGB16	M [ui-art-pipeline]
0x006758c0	TQI_RowYCbCrToRGB32	M [ui-art-pipeline]
```

Corrections to existing entries:
- `Video_PlayTGQFrame` 0x0051b010: as names.tsv already says, it starts a movie.
- `Screen_BlitFullscreenImage` 0x00587130: a nearest-neighbour integer-divide full-screen stretch, aspect ignored, 16/32 bpp.
- `UI_RenderLoadingBar` 0x00587a70: 3 solid rects, no art.
