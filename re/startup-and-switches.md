# Theme Park World: start-up, switches, registry, config files and debug features

Target: `testgame\TP.exe` (noCD v2.0). Sources: Ghidra export caches, with key findings checked against the real instructions (disassembled from TP.exe with gdb in static mode; capstone is not installed). Confidence: **H** = checked in the disassembly, **M** = decompiler output that makes sense but was not checked instruction by instruction, **L** = inference.

## 0. Start-up order (WinMain 0x0045a960)

1. `0x0045f640` Startup_RegistryAndMutexCheck: registry "Language" check, then the `TPWMutex` mutex (sections 3 and 5). If it fails, WinMain skips straight to shutdown. **H**
2. Base directory = the current working directory (`_getcwd` at 0x0067b9f0, then `Path_SetBaseDir` 0x005f5690 into 0x00fa6fd0). `Paths_Init` 0x005c0c10 builds the data, levels and save paths from it. `Lang_SelectFromRegistry` 0x005c0d00 runs next. **M** (A harness must start the game with its working directory set to the game folder.)
3. `0x00449df0`: `DAT_007a1a8c = 0xc0e15` (display flags), then tries `dialog.tcf` (section 1). **H**
4. `Config_LoadTcf` 0x00424930 reads `save\config.tcf` (this = 0x0078d8d8). **H**
5. Message loop. On the first idle tick (`DAT_007a1a14 & 4` clear): `Lang_LoadTextTables` 0x00419710 (Data\Language\<lang>\, sets `[0x0078b530]=1`), CD check 0x0045f7a0, resource mounts 0x00408470, `Video_InitResolution` 0x00449ec0, window and DirectDraw 0x0044e080, game init 0x0054dcf0 (which parses the command line). Each failure sets `DAT_007a1a14 |= 2` (quit). **H**

## 1. Windowed vs fullscreen

- The flag passed to DirectDraw_InitDisplay is **bit 0 of `DAT_007a1a8c`**. At 0x0044e1f0–0x0044e216: `push edx` where `edx = flags & 1`. The chain is 0x0044df20 param_3, then 0x005632b0 param_6, then 0x00563460 param_6 (1 = fullscreen). The last argument of 0x00563460 (param_7) is `DAT_008bd524 == 1` (renderer is D3D), not the windowed flag. **H**
- The same bit selects the window style in `0x0044e080` (0x0044e0f7 `test bl,al`):
  - fullscreen: `CreateWindowExA` style `0x90000000` (WS_POPUP|WS_VISIBLE), size W×H;
  - windowed: style `0x00CF0000` (WS_OVERLAPPEDWINDOW, so it can be resized and maximised). `AdjustWindowRect` sizes it so the client area is W×H (`DAT_007a1968`×`DAT_007a1930`), at CW_USEDEFAULT. Class and title are both "Theme Park World" (0x0074c350). **H**
  - Then in 0x00563460 (windowed branch): SetCooperativeLevel(0x808 = NORMAL|NOWINDOWCHANGES). `MoveWindow` resizes again so the client area equals `DAT_008bcbd0`×`DAT_008bcbd4`. The primary surface is created with a clipper, plus an offscreen back buffer (caps 0x6040) **in the desktop's pixel format** (GetDisplayMode result into `DAT_008bcbe4`). Then `SetWindowPos`. **M** Risk: the software renderer and the TGQ movie blitter may assume 16-bpp. On a 32-bpp desktop that is untested (**L**).
- **Who sets bit 0:** only the default `mov dword [0x007a1a8c],0x000c0e15` at 0x00449e1c (bit 0 = 1, fullscreen) and the copy from `dialog.tcf` (0x00449e73). Every other writer (0x0044d8f0, 0x0044dd50, 0x0044de20, 0x0044e32f, 0x00457a00/40, 0x00550ca0/d80) masks or sets other bits only. **H**
- **No command-line switch, .sam key, config.tcf field or registry value selects windowed mode.** **H**
- **Hidden developer path, `dialog.tcf`** (in the working directory; string 0x0074ce84). Loaded by `DialogTcf_Load` 0x00424a60 into 0x0078d950.. (this = 0x0078d8d8 + 0x78). If the load succeeds, `DAT_0078d968 = 1` (the only writer, 0x00449e5b) and:
  - `DAT_007a1a8c` = field 1 (whole display-flag word, including bit 0 and the resolution bits). `DAT_0079d86c` = field 2. Field 4 goes to `FUN_00409790` (theme). Field 6 goes to `DAT_0079d9c8` (texture cache MB, `<<20` into `DAT_008c1620`). **H**
  - `Video_InitResolution` then ignores RESOLUTION and the options-screen value and picks the size from flag bits: `0x20000000` 160×120 (only if windowed), `0x40000000` 320×240, `0x80000000` 400×300, `0x8` 512×384, `0x10` 640×480, `0x20` 800×600, `0x40` 1024×768, `0x80` 1280×1024, `0x100` 1600×1200. If no bit is set, 640×480. **H** (decompiled 0x00449ec0, bit tests confirmed)
  - The window code uses the debug renderer selector 0x0044dd50 instead of 0x0044de20. `DAT_0079d86c & 0x40` gives software (4), otherwise D3D (1). Unless `DAT_0079d86c & 0x4000`, it first loads the low/med/high detail .sam picked by `0x800/0x1000/0x2000` (0x0044dc60). Other bits: `0x8000` sets `DAT_008bcbc8|1` (fullscreen back-buffer count 2), `0x80000` sets `DAT_008bd550|2`, `0x2` clear sets `DAT_008bd508|0x40`. **M**
  - Layout (28 bytes, little-endian dwords): `version(>=2)`, `displayFlags→0x007a1a8c`, `debugFlags→0x0079d86c`, `unused→0x0078d958`, `CURRENTTheme`, `DEBUGGuardUInts`, `TextureCacheSize(MB)`. **H** (0x00424b3b `cmp eax,2; jae`; field reads at 0x004246ed–0x004247e3)
  - Other bits in `DAT_007a1a8c`: `0x1000` = DirectDraw device index 1 (second card, 0x0044e208), `0x200000` = 32-bpp, `0x400000` = flag passed to D3D init, `0x800` = colour hardware cursor / DirectInput mouse, `0x2000/0x4000` = in-game 3D toggle, `0x800000/0x1000000/0x2000000` = game type. **M**
- **Simplest windowed patch:** change the immediate at VA 0x00449e22 (file offset 0x49222) from `0x15` to `0x14`. The normal settings path then runs with bit 0 clear. Resolution still comes from RESOLUTION or the options value. **H** for the logic, untested in practice.

## 2. Command-line switches

- `CmdLine_Split` 0x00405630 (a static constructor, object 0x008787a0) copies `GetCommandLineA` and splits it **only at " /"** (a space followed by a slash). argv[0] is the exe part. Switches must therefore be written as ` /name`; `-name` is accepted by the parser but is never split off as its own argument. Up to 128 arguments. **H**
- `CmdLine_ParseSwitches` 0x0040f000 is called from game init 0x0054dd65. It compares case-insensitively (`_stricmp` 0x0067df40) after skipping the leading `/` or `-`. **H**

| Switch | Effect | Conf |
|---|---|---|
| `/v`, `/version` (0x0074874c, 0x00748744) | returns false, so 0x0054dcf0 aborts game init (ErrorReporter has nothing pending, so no message). In this build it is effectively a "do nothing / broken start". | H (effect after the return: M) |
| `/quickload`, `/nodebug`, `/noload`, `/SAVEDEBUG` (0x00748738, 0x00748730, 0x00748728, 0x0074871c) | **recognised but no-ops**: they jump straight to the next argument (0x0040f1bd). The debug code was compiled out of the release build. | H |
| `/flmouse` (0x00748714) | `DAT_0078826c = 1`: forces the cursor mode that is normally used on 3dfx (vendor 0x121a, device < 5), in 0x0047e7d0 | H (meaning M) |
| `/bwcursor` (0x00748708) | `DAT_00788270 = 1`: no colour cursor. Skips DirectInput mouse set-up and uses the mono cursor set 0x007b9e88 (0x00489ca0) | H |
| any token ending in `.TPWS` (0x007479d0) | copied into `DAT_00788164`, `DAT_00788160 = 1`. Quick-loaded at level start (0x00407e00, "QuickSave Loaded"). The token keeps its leading `/`. | H (path handling M) |
| anything else | `ErrorReporter_Set(2, 0, arg)` and init aborts. A MessageBoxW then shows (language is loaded by then). | H |

- `lpCmdLine` (WinMain param_3) is only stored in 0x007a1614 and never parsed. The second `GetCommandLineA` user (0x005f5950) only extracts the exe directory (fallback base path). **M**
- `"cheat"` 0x007486e8 is **not a switch**. It is the name of a keybind group (section 8).

## 3. Registry

All calls go through 0x005f8ae0..0x005f8d40. The only imports are `RegCreateKeyExA`, `RegOpenKeyA`, `RegQueryValueExA` and `RegCloseKey`. **H**

- `Reg_OpenBullfrogKey` 0x005f8ae0: `RegCreateKeyExA(HKLM, "software\Bullfrog Productions Ltd", KEY_ALL_ACCESS 0xF003F)`, then a subkey through the same create call. HKCU is used if the last argument is non-zero, and both callers pass 0. Because it uses *Create*, missing keys are **created**, not reported. **H**
- On 64-bit Windows the process is 32-bit, so this is `HKLM\Software\WOW6432Node\Bullfrog Productions Ltd\Theme Park World`. The exe has no manifest (resources: icon, dialog 101, group icon only), so UAC registry virtualisation applies when it runs unelevated. A `tp.reg` imported with 64-bit regedit lands in the wrong view. **H** (no manifest) / standard Windows behaviour.
- Call sites:
  1. `Startup_CheckRegistryLanguage` 0x0045f6b0 (from 0x0045f640): key `...\Theme Park World` (static string 0x007a2b00 = "Theme Park World"), DWORD value **"Language"** (0x007a2350, "Language" at 0x0074c364). The value itself is thrown away; only its presence matters. Accepted types: 4/5 used as-is, 6 byte-swapped (0x005f8c50). **If the key or value is missing, `ErrorReporter_Set(0)` runs and the game exits silently**: the error box needs the language tables, which are not loaded yet. **H**
  2. `Lang_SelectFromRegistry` 0x005c0d00 (WinMain, early): same key and value. If missing, the default is **0x809** (UK). The LCID maps to a language bit (0x409 gives 0x8000, 0x809 gives 1, and so on), and the language folder name goes into 0x00f7f230, which 0x00419710 uses for `Data\Language\<name>\`. **M**
- `Version`, `BuildTypeCode` and the `App Paths\TP.exe` entry are **never read** by the exe (none of these strings exist). Only `Language` matters. **H**

## 4. CD checks (noCD state)

- `CD_FindDiscByLabel` 0x005aa500 (from 0x0045f7a0 on the first idle tick): enumerates drives (`GetLogicalDriveStringsA`/`GetDriveTypeA` == DRIVE_CDROM, mask 8, 0x005f87b0), then looks for the volume label **"TPWorld"** (0x007477e8) with `GetVolumeInformationA` (0x005f8940/0x005f89f0). **H**
- **Crack patch at 0x005aa5ad: `EB 1B` (jmp short to the success path, 0x005aa5ca)** right after `test al,al`. The result is ignored and the function always returns 1. A retry/cancel box (MessageBoxW 0x005aa5c1, type 0x15) only appears if `GetLogicalDriveStringsA` itself fails. **H**
- `CD_SetCDRootPath` 0x005aa650 builds `<letter> + ":" + "\"` into the path manager's CD slot (+0x14). With no disc the letter is 0, so the CD root is an empty string. **M**
- The CD root is only used as a **fallback search path** after the local folder: `FUN_0041cc20` = local (0x0041cc40) then CD (0x0041cd50). The only mount that uses it is `data\movies` (0x007866a0) in 0x00408470, plus one theme loop entry in 0x004081e0. Movies are read locally first, and `testgame\data\Movies` exists. Nothing reads music or data only from the CD. **No remaining CD check can block start-up or file loading.** **H** (mount logic M)
- The other `GetDriveTypeA` callers (0x0067bb50, 0x0067e900) are in the CRT. SafeDisc DLLs (clcd16/32, drvmgt) sit in the folder but are not imported. **M**

## 5. Single instance

- `CreateMutexA(NULL, TRUE, "TPWMutex")` (name 0x007477f0, handle 0x007a267c) at 0x0045f666. If `GetLastError() == 183` (ERROR_ALREADY_EXISTS), `ErrorReporter_Set(1)` runs, the handle is closed, and the game **exits silently** (the language is not loaded yet, so no box). **H**
- There is no `FindWindow` (not imported). The window class "Theme Park World" could be used by a harness to find the window. **H**

## 6. save\config.tcf (32 bytes)

Object at **0x0078d8d8**. `Config_LoadTcf` 0x00424930 first calls `Config_SetDefaults` 0x00423690, reads a 4-byte version (0 = keep defaults), then `Config_SerializeTcf` 0x004242c0 (mode 0 = read). `Config_SaveTcf` 0x00424820 writes version 1 at shutdown (0x005c8650). A missing file silently keeps the defaults. **H**

| File off | Global | Name / meaning | Default | safemode.tcf | Conf |
|---|---|---|---|---|---|
| 0x00 | (none) | version; must be ≥ 1 | (written 1) | 1 | H |
| 0x04 | 0x0078d8d8 | 3D acceleration (0 = software renderer 4, 1 = D3D 1; 0x0044de20) | 1 | 0 | H |
| 0x08 | 0x0078d8dc | options resolution (0 = 512×384, 1 = 640×480, 2 = 800×600; only used when RESOLUTION ≤ 0) | 1 | 1 | H |
| 0x0C | 0x0078d8e0 | detail level 0/1/2 (picks low/med/high .sam via 0x0044dc60 flag 0x800/0x1000/0x2000; level 2 without 3D falls back to med) | 1 | 0 | H/M |
| 0x10 | 0x0078d8e4 | "VideoCard": DirectDraw device index (1 sets `DAT_007a1a8c |= 0x1000`; reset if fewer than 2 devices) | 0 | 0 | H |
| 0x14 | 0x0078d900 (byte) + 0x0078d904 (dword) | "MovieVolume": enable byte (+3 pad), volume % (`*1023/100` into 0x0051b3c0) | from 0x00785924/… | 1, 100 | H |
| 0x1C | 0x0078d908 | labelled "SoundQuality" by the loader. Actually feeds options slider 0x1d4e0 and `FUN_0051c700` (piecewise-linear quality/distance curve) | 0x0078591c | 0x20 | M |

The sound/music/speech enable and volume fields (0x0078d8e8–0x0078d8fc) and the field at +0x36 (0x0078d90e, written by the Ctrl+H handler) are **not** stored in config.tcf. **H**

## 7. Message boxes

| Site | API | Caption / type | Trigger | Conf |
|---|---|---|---|---|
| 0x00405acd in `ErrorReporter_Show` 0x00405750 | MessageBoxW | L"Theme Park World" (0x007477ac), 0x10010 (MB_ICONHAND\|MB_SETFOREGROUND, **OK only**) | Pending error in object 0x00785de8, shown at shutdown (0x0045ad2b) or after a bad command line (0x0054dd76). **Shown only if `[0x0078b530] != 0` (language loaded).** Codes: 0 registry (silent), 1 already running (silent), 2 bad argument, 3 file/text-table/font missing (0x00401650, 0x00419710), 4 RegisterClass/CreateWindow failed, 8 data error (0x00401700, 0x00402720), 0xC display/renderer init failed (engine error mapped through 0x00405b90). Text is localised (string table 0x0078ba88). | H |
| 0x005aa5c1 | MessageBoxW | localised, 0x15 (RETRYCANCEL\|ICONHAND) | only if GetLogicalDriveStringsA fails | H |
| 0x0046f10e | MessageBoxA | "M3D2 Exporter - Data Error", 0x31 (OKCANCEL) | model data errors (0x00457e70, 0x00458400) | M |
| 0x005d2a10 | MessageBoxA | "IP Error", 0 | Winsock or online errors | M |

For a headless harness at start-up, expect only the first box (caption "Theme Park World", one OK button). The usual cause is 0xC (DirectDraw/D3D failure) or 3 (missing data). Registry, mutex and language failures end the process **without** any window. **H**

## 8. Developer and debug features

- **The FPS counter cannot be switched on.** `DAT_007b05d0` is read only at 0x0054e395. Its only writer is `0x0046c0b0`, which is a **CRT static initialiser** (`[0x007b05cc]=1`, `[0x007b05d0]=0`) and not a hotkey handler. `0x00729f00` is part of the **`__xc_a` static-constructor table** (entries 0x00466720…), not a keybind table. A raw byte scan finds the address 0x007b05d0 only at 0x0046c0bb and 0x0054e397. This corrects frame-timing.md §9.2. Enabling it needs a patch (for example, set `[0x007b05d0]=1`). **H**
- **The profiler line "E: %d, W: %d, R: %d, P: %d, S: %d (ms)"** (0x007650b4) is formatted into 0x00877eb8 in 0x0054e360, then passed only to `Debug_Trace` 0x005da3c0, **which is a bare `ret`**. Every debug trace in the exe (including "Arg %d: %s" and the "Couldn't open file" messages) is a no-op. **H**
- **Log files:** `enginedebug.txt` (0x00564650; always on for DirectX errors through the error mapper, plus verbose init when `DAT_008bd508 & 0x1000000`); `fallback.txt` (0x005645e0, when `DAT_008bd508 & 0x2000000`); `c:\tcache.log` (0x0058e010/0x0058e110, when `DAT_008bd550 & 0x80`). All are written in the working directory except tcache. **M**
- **Keybind system:** `Keybind_InitGroups` 0x0040cb80 creates six groups. Each group is {entry table, count, enabled, name}, and each entry is 0x14 bytes {id, key, mods, pressed, name*, handler*}. A key is matched exactly on (key, mods) in `Keybind_DispatchKeyDown` 0x0040c900. **Key** = `MapVirtualKeyA(vk, MAPVK_VK_TO_CHAR)` (so letters are upper-case ASCII), or `vk<<8` for extended keys and numpad + - * / . (WndProc 0x0046b600). **Mods**: Shift = 3, Ctrl = 0xC, Alt = 0x30, so 0x3F = Ctrl+Shift+Alt. WndProc dispatches only the "system" group. The other groups are dispatched from the in-park handler around 0x00488700 and are enabled per game state (0x0040cf60 enables all, 0x0040cb50 disables one). **H** (tables read from the binary)

| Group (table) | Key | Handler | Function | Conf |
|---|---|---|---|---|
| system (0x00747f38) | P | 0x0040bf70 | pause toggle (0x00409350) | M |
| | Ctrl+H | 0x0040c5d0 | toggles a UI flag into 0x0078d90e (HUD hide?) | L |
| | **F8** | 0x0040c470 | **screenshot** `Scr%05ld.tga` (0x00550460) | H |
| | **Ctrl+Shift+Alt+F8** | 0x0040c480 | toggles `DAT_00878128` = **dump a TGA every frame** (0x00550510, used in 0x0054e360) | H |
| game (0x00748028) | Esc / `,` / `.` | 0x0040c180 / 0x0040c120 / 0x0040c150 | menu / rotate camera +90° / +270° | M |
| | Delete, F3, Backspace, F1 | 0x0040c5e0, 0x0040c4b0, 0x0040bda0, 0x0040c800 | UI panels | L |
| | Num- / Num+ / Num* | 0x0040c3b0 / 0x0040c3c0 / 0x0040c3d0 | zoom out / in / reset (object 0x00785970) | M |
| | Ctrl+C, Ctrl+O | 0x0040c410, 0x0040c3f0 | 0x00519ef0(0/1) | L |
| | **Alt+S / Alt+L** | 0x0040bf90 / 0x0040c040 | **QuickSave / QuickLoad** ("QuickSave OK", "QuickSave Loaded") | H |
| | Alt+V | 0x0040c820 | 0x00481b70 | L |
| camera (0x00748158) | arrows, Home, numpad codes 0x61–0x69, Ctrl+Shift+Alt+Space | 0x0042a500/0x0042a520 | camera movement | M |
| | Ctrl+F5 | 0x0040c830 | cycles `DAT_008bcbc8` bits 2–3 (render mode, possibly wireframe) | L |
| cheat (0x007484d0) | Alt+R, Alt+1, Alt+G, Alt+[keys 0x61–0x69] | 0x0040c430, 0x0040c450 (uses 10000, probably money), 0x0040c6c0…0x0040c7e0 | cheats | M |
| coaster (0x00748350) | Esc, Backspace | 0x0040c3a0, 0x0040bda0 | coaster editor | M |
| shortcuts (0x00748378) | Esc B H I A S V F L T G R Space Ctrl+S Ctrl+V Ctrl+P C | 0x0040c4c0…0x0040c5c0 | UI shortcuts; keys 1–4 are re-read from the localised KEYBOARD table | M |

- **Cheat unlock:** the cheat group is dispatched only when `Cheat_IsEnabled` 0x00405560 (byte `[0x00fb352a]`) is true. That byte is set by `Cheat_SetEnabled(1)` 0x004053c0, called from dialog proc 0x004993e0 when its two text fields, XOR-ed character by character with their index, equal the stored strings. The stored strings decode to **"Kevin the"** and **"Cosmic Cow"** (0x00751a54, 0x00751a3c). The flag is saved in the savegame (SAD_CHEAT module 0x00415270/0x004164c0). **H** for the comparison, **M** for which dialog it is (probably player/profile name entry).

## 9. Input APIs (for scripted input)

- **Keyboard:** Win32 messages in `WndProc_HandleInput` 0x0046b600 (WndProc 0x0046bcb0): WM_KEYDOWN/UP, WM_SYSKEYDOWN/UP, WM_CHAR, and custom 0x410 (char). Modifiers come from `GetKeyState` (Shift/Ctrl/Alt). `GetAsyncKeyState` polls Shift/Ctrl/Alt (0x00486aa0, 0x00486d90, 0x004879d0), VK_LBUTTON/VK_RBUTTON each frame (0x0054e3d0), and **Esc/Space to skip intro movies** (0x0054e360 states 6/8). **H**
- **Mouse:** WM_MOUSEMOVE, L/R/M button down/up, WM_MOUSEWHEEL, and the registered "MSWHEEL_ROLLMSG". Coordinates are scaled to a 2048×1536 virtual space by `DAT_00faa5c4`/`DAT_00faa5c0`. **DirectInput 5** (`DirectInputCreateA` version 0x500 at 0x00633bf0): **GUID_SysMouse** (referenced at 0x006348e1) and GUID_SysKeyboard (0x006350d1) device classes, buffered (DIPROP_BUFFERSIZE 32) with event notification (0x0063e8c0). The mouse device is created in 0x00489ca0 only when `DAT_007a1a8c & 0x800` (on by default) and not `/bwcursor`. The system cursor is hidden (`ShowCursor(0)`) and a game-drawn cursor is used. There is no `GetCursorPos` or `ClipCursor` import. **H** (imports and GUIDs) / **M** (which device drives the cursor)
- Harness implication: `PostMessage` is enough for keys handled through WM_*. Movie skipping and mouse-button state need real input (`SendInput`), because `GetAsyncKeyState` and DirectInput do not see posted messages. `/bwcursor` removes DirectInput mouse use. **M**

## New names

```
0045f640	Startup_RegistryAndMutexCheck	Language registry check then CreateMutexA "TPWMutex"; silent exit on failure
0045f6b0	Startup_CheckRegistryLanguage	HKLM\software\Bullfrog Productions Ltd\Theme Park World, DWORD "Language" must exist
0045f7a0	Startup_CDCheck	wrapper calling CD_FindDiscByLabel
005aa500	CD_FindDiscByLabel	scans CD-ROM drives for label "TPWorld"; noCD jmp at 0x005aa5ad always succeeds
005aa650	CD_SetCDRootPath	stores "<letter>:\" as path-manager CD root (+0x14)
005f87b0	Drive_EnumerateByType	GetLogicalDriveStringsA + GetDriveTypeA filter mask (8 = CDROM)
005f8940	Drive_FindByVolumeLabel	returns drive letter whose volume label matches
005f89d0	Drive_LetterFromRoot	returns letter if root is "X:"
005f89f0	Drive_GetVolumeLabel	GetVolumeInformationA wrapper
005f8ae0	Reg_OpenBullfrogKey	RegCreateKeyExA KEY_ALL_ACCESS on HKLM (or HKCU) software\Bullfrog Productions Ltd\<sub>
005f8b80	Reg_CloseKey	RegCloseKey wrapper
005f8bc0	Reg_CreateKey	RegCreateKeyExA wrapper, returns HKEY or 0
005f8c00	Reg_QueryValueRaw	RegQueryValueExA wrapper
005f8c50	Reg_QueryDword	reads DWORD (types 4/5, 6 byteswapped)
005f8cf0	Reg_QueryDwordDefault	Reg_QueryDword with default on failure
005f8d40	Reg_GetValueType	returns value type, -2 if subkey, -1 if missing
005c0d00	Lang_SelectFromRegistry	reads Language LCID (default 0x809), sets language folder 0x00f7f230
005c0c10	Paths_Init	builds game/data/levels/save paths from base dir
005f5690	Path_SetBaseDir	stores base dir (cwd) with trailing slash into 0x00fa6fd0
005f5950	Path_GetExeDirFromCmdLine	extracts exe directory from GetCommandLineA (fallback base dir)
00419710	Lang_LoadTextTables	loads Data\Language\<lang> string tables and fonts; sets [0x0078b530]=1
00405630	CmdLine_Split	static ctor (obj 0x008787a0): splits GetCommandLineA at " /"
0040f000	CmdLine_ParseSwitches	/v /version /quickload /nodebug /noload /SAVEDEBUG /flmouse /bwcursor, *.TPWS
00405710	ErrorReporter_Set	this=0x00785de8: pending error code + optional text
00405750	ErrorReporter_Show	MessageBoxW "Theme Park World" if language loaded
00405b90	ErrorReporter_MapEngineError	engine HRESULT (0xb0754xxx) to error-string id
00423690	Config_SetDefaults	this=0x0078d8d8 config defaults (3D=1, res=1, detail=1)
00423dd0	Config_ApplyVolumes	applies sound/music/speech/movie enables and volumes
004242c0	Config_SerializeTcf	config.tcf body read(0)/write(1): 3D, res, detail, VideoCard, MovieVolume, SoundQuality
00424930	Config_LoadTcf	loads save\config.tcf (version dword must be >=1)
00424820	Config_SaveTcf	writes save\config.tcf with version 1
00424560	DialogTcf_Serialize	dialog.tcf body: displayFlags, debugFlags, unused, CURRENTTheme, DEBUGGuardUInts, TextureCacheSize
00424a60	DialogTcf_Load	loads dialog.tcf (version >=2) into 0x0078d950..; returns 1 on success
00449df0	Config_InitDisplayFlags	DAT_007a1a8c=0xc0e15 (bit0 fullscreen); dialog.tcf overrides and sets DAT_0078d968
0044d8f0	Display_ApplyDetailSamOptions	applies GameOptions .sam values to render flags
0044dc60	Display_LoadDetailSam	loads low/med/high .sam by flag 0x800/0x1000/0x2000
0046bcb0	WndProc	window procedure; WM_DESTROY posts quit, else WndProc_HandleInput
0046b600	WndProc_HandleInput	mouse/keyboard/activate message handling, keybind dispatch
0040cb80	Keybind_InitGroups	creates system/game/camera/cheat/coaster/shortcuts keybind groups
0040c900	Keybind_DispatchKeyDown	matches (key,mods) in a group and calls handler
0040c990	Keybind_DispatchKeyUp	key-up counterpart
0040cb50	Keybind_DisableGroup	clears group enabled and entry pressed flags
0040cf60	Keybind_EnableAllGroups	enables all keybind groups
0040cfa0	Keybind_DisableAllGroups	disables all keybind groups
00550460	Debug_SaveScreenshotTGA	F8: writes next free Scr%05ld.tga
00550510	Debug_SetFrameCapture	sets DAT_00878128 (TGA every frame), Ctrl+Shift+Alt+F8
0040bf90	Debug_QuickSave	Alt+S quick save
0040c040	Debug_QuickLoad	Alt+L quick load
00405560	Cheat_IsEnabled	returns byte [this+0xa] (this=0x00fb3520)
004053c0	Cheat_SetEnabled	sets cheat flag [0x00fb352a]
004993e0	UI_NameDialog_CheckCheatCode	unlocks cheats when fields are "Kevin the" / "Cosmic Cow"
00633bf0	DInput_CreateDevice	DirectInputCreateA(0x500) + CreateDevice + SetDataFormat
0063e8c0	DInputDevice_InitBuffered	device init with event notification and 32-entry buffer
00489ca0	UI_InitCursorAndInput	cursor set-up; creates DirectInput mouse when colour cursor enabled
0046c0b0	StaticInit_FpsVars	CRT static initialiser: [0x007b05cc]=1, FPS flag [0x007b05d0]=0 (not a hotkey)
005da3c0	Debug_Trace	empty stub (ret); all debug traces compiled out
005645e0	FallbackLog_Write	appends to fallback.txt
```
