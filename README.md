# Theme Park World HD

A patch that makes **Theme Park World** (Bullfrog, 1999; *Sim Theme Park* in North America) run well on a modern Windows PC: native widescreen resolutions with working text, borderless full screen, and no registry set-up. The game has no released source, so this is a binary patch: a small DLL that the game loads by itself and that changes it in memory. **No game file is modified, and none is included here.**

> **Status: early development (v0.0.1-dev).** The resolution and frame-cap fixes work and are tested. The rest of the plan is below. There's no release build yet.

## Made with New Game+

This project is built with **[New Game+](https://github.com/Deadpoodle/newgame-plus)**, a Claude Code skill for modernising old PC games with or without their source. The skill supplies the process, and this repo is one game run through it:

- **Reconnaissance first:** identify the exact build, read its imports and configs, check prior art (PCGamingWiki, community patches, open-source projects), then map the engine before changing anything. The findings are in [`re/`](re/).
- **Reverse engineering in tiers:** a Ghidra headless project exported to plain, grep-able caches, a capstone disassembly cache ([`tools/tpwre.py`](tools/tpwre.py)), and read-only research agents with narrow questions. Each agent writes a report with confidence marks (H = read from the instructions, M = inferred, L = a guess to test).
- **Getting code in without editing the exe:** a proxy `wsock32.dll` (the game imports it at start-up), import-table hooks for Win32 calls, and code patches that check the original bytes first. The addresses are all in one header, [`src/game.h`](src/game.h).
- **Test everything hidden first:** a headless mode runs the game off-screen, muted, never focused, and with only scripted keys reaching it. Every change is tested that way before a player sees it.
- **The original stays the default** wherever taste is involved. Fixes are options in a commented ini file.

New Game+ was distilled from [WarBreeds HD](https://github.com/Deadpoodle/warbreeds-hd), a patch for a 1998 RTS. This project reuses its patterns (the proxy, the headless test mode and the test scripts), not its addresses.

## What it does so far

| Fix | Setting | What it does |
|---|---|---|
| **Native resolution** | `[Display] Resolution=auto` | Runs at the desktop's resolution, or any `WxH`, **with all text visible**. The engine already has fonts for big screens. The known hex edit loses all text because the font loader and the font selector choose their size tier differently (the loader from the resolution *setting*, the selector from the real width). The patch keeps them in step. |
| **Borderless full screen** | `[Display] Mode=borderless` | The engine's own windowed path in a frameless window covering the screen: no display-mode change, smooth frames, instant Alt+Tab. `fullscreen` (the original exclusive mode) and `window` are options. |
| **Frame cap (option, off)** | `[Display] MaxFPS=0` | The game has no cap. PCGamingWiki says it runs too fast above 60 fps; that isn't confirmed here yet, so the cap is off. A cap of exactly 60 clashed with the game's timing (about 4 picture updates a second). |
| **32-bit full screen** | (with a resolution set) | Current drivers refuse the game's 16-bit full-screen modes at modern sizes. The patch asks for 32 bits per pixel instead. |
| **No registry set-up** | `[Compat] Registry=1`, `Language=auto` | The game quits without a message if a registry value is missing. The patch answers that read itself and **writes nothing** to the registry. It picks the language your copy actually has. Using the US value on a European copy also makes the game quit without a message. |
| **Software renderer fix** | always | The software renderer's 32-bit clear wrote every other pixel black (a 64-bit fill built from one 32-bit colour). |
| **Alt+F4** | `[Input] AltF4=1` | Quits the game, which the original ignored. |
| **Fixes for modern Windows** | always | The intro movie no longer hangs when the game loses focus, and a full-screen present flag that dgVoodoo2 rejected is corrected. The working directory is set to the game folder, so shortcuts work. |
| **Single core (option, off)** | `[Compat] SingleCPU=0` | Pins the game to one CPU core. Not needed, and with a frame cap it starved the animation. |
| **Windowed** | `[Display] Windowed=1` | The engine's own windowed mode, which the game never exposed (not yet scaled or borderless). |

### Known limits

- **Wider than 2048 pixels needs dgVoodoo2.** Without a wrapper, the game's own Direct3D (the DirectX 6 HAL device) won't start on a render target wider than 2048, and the game falls back to its slower software renderer. So without dgVoodoo2, `auto` picks the largest display mode up to 2048 wide in your screen's shape (1920x1080 on a 2560x1440 screen), and the screen scales it up. With dgVoodoo2 installed (below), `auto` uses your full desktop resolution.
- **The interface isn't scaled yet.** The 2D menu art stretches to the screen width, and the text stays at its 1024x768 size.
- **Tested on one PC** (AMD RX 9070 XT, Windows 10). Without dgVoodoo2: full screen at 1920x1080, and windowed from 1280x720 to 2560x1440. With dgVoodoo2: windowed at 2560x1440. Full screen with dgVoodoo2 is not tested yet.

## What you need

- Your own copy of Theme Park World, updated to **v2.0**, with a `TP.exe` **without SafeDisc** (SafeDisc can't run on Windows Vista and later). Every current community fix targets that same v2.0 exe (3,734,528 bytes). The patch checks the exe's bytes and does nothing to a version it doesn't recognise.
- Windows 10 or 11. Linux/Proton is untested.
- **Recommended: [dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2/releases)** by Dege (tested with v2.87.5). It runs the game's DirectX 6 on Direct3D 11, which lifts the 2048-pixel limit.
  1. Copy `MS\x86\DDraw.dll`, `MS\x86\D3DImm.dll` and `dgVoodoo.conf` from its zip into the game folder.
  2. Set `dgVoodooWatermark = false` in `dgVoodoo.conf`.

  The patch recognises dgVoodoo2 by its version information and logs it. Its readme allows a game mod to ship unmodified dgVoodoo files, so future release packages may include it.

## Building

Needs a 32-bit MinGW-w64 toolchain (MSYS2 `mingw32`, or `i686-w64-mingw32-g++` on Linux):

```sh
./build.sh
```

This writes `build/wsock32.dll`. Copy it into the game folder next to `TP.exe`. The first run writes a commented `ThemeParkWorldHD.ini` and a `ThemeParkWorldHD.log` there. Remove the DLL to get the stock game back.

## Testing

`tools/autotest.sh [seconds] [screenshot-every] [script]` builds the DLL, installs it into a hidden test copy of the game (`testrun/`, never the copy you play), and runs it headless. It reports whether any game window was ever visible or in the foreground, and prints the log. Scripts are timed steps such as `6:esc,12:esc,21:shot` (keys, `click X Y`, `type TEXT`, and screenshots made with the game's own screenshot code). Environment variables select `RES=`, `MAXFPS=`, `ARGS=`, `PATCH=` (verified byte patches for experiments) and `DDTRACE=1` (a DirectDraw/Direct3D call trace).

## Planned

1. **Core:** done apart from big-park textures (a known community fix).
2. **Interface and rendering:** an interface that scales and stays readable at 1440p and above; Direct3D beyond 2048 wide; borderless full screen.
3. **Extras:** the in-park music fix (re-implementing the community TPW Patcher's fix, with credit), mouse-wheel zoom, an FPS counter.
4. **Comfort:** camera speed tied to time rather than frames, an in-game settings page, saves.

## Credits and prior art

- **[PCGamingWiki: Theme Park World](https://www.pcgamingwiki.com/wiki/Theme_Park_World)**, for what is known to be broken and which fixes exist.
- **[TPW Patcher](https://github.com/TheAussie/TPW-Patcher)** by TheAussie (MIT), the in-park music fix. Its patch sites were checked against this exe, so the two can coexist.
- **[OpenTPW](https://github.com/OpenTPW/OpenTPW)** (MIT), an open-source reimplementation that documents the game's file formats.
- The community fix bundles and **Adam Hearn's font fix**, for the noCD v2.0 base and the font and music notes.
- **[dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2)** by Dege, the DirectX wrapper that lets the game's Direct3D run at any resolution.
- **[WarBreeds HD](https://github.com/Deadpoodle/warbreeds-hd)**, for the proxy DLL, the headless test mode and the scripts this project started from.

Theme Park World is © Electronic Arts. This project isn't affiliated with EA or Bullfrog, and includes no game code, data or art.

## Licence

Released into the public domain under [The Unlicense](LICENSE).
