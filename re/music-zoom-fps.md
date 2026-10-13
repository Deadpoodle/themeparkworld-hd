# In-park music fix, mouse-wheel zoom, FPS counter

Target `testrun\TP.exe` (3,734,528 bytes, base 0x400000, 11 sections, SizeOfImage 0xBC6000). All bytes below were read with `tools\tpwre.py bytes/dis` or a raw scan of the file. **H** = read in the bytes, **M** = decompiler output that makes sense but was not traced instruction by instruction, **L** = inference.
Nothing was run and nothing was downloaded. TPW Patcher was read as source through the GitHub API (repo head 9667df4, 2026-06-15).

---

## 1. In-park level music (TPW Patcher)

Sources (MIT, "Copyright (c) 2026 TheAussie"):
- Patcher: https://github.com/TheAussie/TPW-Patcher/blob/main/Patch-TPW-LevelMusic-Combined.py
- Write-up: https://github.com/TheAussie/TPW-Patcher/blob/main/docs/TPW-LevelMusic-Investigation.md
- Read-only signature checker: https://github.com/TheAussie/TPW-Patcher/blob/main/Test-TPW-PatchCompat.py
- Licence: https://github.com/TheAussie/TPW-Patcher/blob/main/LICENSE

### 1.1 The code involved (our exe)

| Address | Name | What it does | Conf |
|---|---|---|---|
| 0x00803AA0 | g_LevelMusic | handle of the level-music request; first dword of a 0x54-byte sound-state block that the savegame stores and restores | H |
| 0x00803A44 | g_CatMusic | `cat_music` category handle | M (TPW doc) |
| 0x0051E730 | Music_StartLevel | `g_LevelMusic = Sound_CreateRequest(g_LevelMusic, g_CatMusic, 2, 0, 0, 0)` (0x0051BFC0), then `g_LevelMusic = Sound_Command(g_LevelMusic, 4, 0)` (0x0051BC40). Ends in `ret` + 11 nops at 0x0051E764. **Only caller: 0x0054EC9F** in Game_MainStateMachine, at park start, after Sound_InitCategories 0x0051EC50 and 0x0051BD70 and just before the park load 0x00457A90 | H |
| 0x0051C400 | Sound_LoadState | savegame "Sound" module loader. Reads two dwords and a size, then reads `size` bytes **straight over 0x00803AA0** (push 0x803AA0 at 0x0051C45F, read call 0x005F5E20 at 0x0051C466), then posts restore commands. **Only caller: 0x00415BA2** in SaveGame_LoadModules 0x00415270 (cdecl, one argument, caller pops) | H |
| 0x0051C350 | Sound_SaveState | the writer: 0x802BD4, 0x802BC8, size 0x54, then 0x54 bytes from 0x00803AA0 | H |
| 0x0051E790 | Music_SelectTrack | `g_LevelMusic = Sound_Command(g_LevelMusic, 4, track)`, 28 bytes plus 4 nops. **Only caller: 0x0054F870**, in the sim-tick loop when `(g_SimTickCount & 0x1F) == 0` (about once a second), with track = min(0x004C81E0(), 0x59), or 0 when `*(0x007CF83C + 0x1DA738) == 4` | H |
| 0x0051E770 | Music_StopLevel | if g_LevelMusic, 0x0051C300(g_LevelMusic) (posts cmd vtable 0x00700BAC with 0x3C); called on park exit 0x0055002B | M |

A raw scan for `E8/E9 rel32` and absolute dwords finds no other reference to 0x0051E730, 0x0051E790 or 0x0051C400. **H**

### 1.2 The three bugs and TPW Patcher's fix, as behaviour

The order at park start is: Music_StartLevel creates a valid handle, then the park load restores the saved sound block over it.

- **v1 (restore clobbers the handle).** Sound_LoadState overwrites the new handle with the stale value from the save (usually 0). **Fix:** the call at 0x00415BA2 goes to a stub that calls Sound_LoadState with the same argument, then calls Music_StartLevel again, and returns Sound_LoadState's EAX.
- **v2 (selector writes 0 over a good handle).** Music_SelectTrack stores Sound_Command's result even when it is 0. **Fix:** a replacement selector computes the same call, and if the result is 0 while the old handle was non-zero, it keeps the old handle. Otherwise it stores the result.
- **v3 (first visit to a level).** On the first visit, Music_StartLevel runs before the level's `\music\MusicHD.sdt` has been opened. The object then answers the first "prime" call but returns 0 on every later one. **Fix ("delayed re-prime"):** two flag bytes, REPRIME_PENDING and SUPPRESS_REARM.
  - Each natural return of Music_StartLevel sets PENDING. If SUPPRESS is set, it clears SUPPRESS instead.
  - On the next Music_SelectTrack tick, if PENDING is set, the selector clears it, sets SUPPRESS, and calls Music_StartLevel once more. By that tick the .sdt is always open.
  - The selector then runs the v2 body.
  - The v1 re-call is a natural call, so it also arms PENDING. Each park load therefore makes three objects: create, v1 re-create, tick-1 re-create. TPW's doc reports that the extra re-create on re-entry is harmless.

**Injected `.fix` section** (0x1000 bytes, characteristics 0xE0000020 RWX, appended after the last section, so VA 0x00FC6000 on our exe):
- +0x00 the v1 stub (20 bytes)
- +0x14 the v2 stub (40 bytes, dead code in the final state, kept so the output matches the sequential v1→v2→v3 scripts)
- +0x40 PENDING, +0x41 SUPPRESS
- +0x50 the return hook of Music_StartLevel (25 bytes)
- +0x80 the live selector (68 bytes)
- the rest is 0xCC

**Sites it rewrites (original bytes confirmed in our exe, H):**

| VA (file off) | Original | TPW Patcher writes (our exe, .fix at 0xFC6000) |
|---|---|---|
| 0x00415BA2 (0x14FA2), 5 bytes | `E8 59 68 10 00` call 0x51C400 | `E8 59 04 BB 00` call .fix+0x00 |
| 0x0051E764, 12 bytes | `C3` + 11×`90` | `E9 E7 78 AA 00` jmp .fix+0x50, + 7×`90` |
| 0x0051E790, 5 bytes | `8B 44 24 04 8B` | `E9 EB 78 AA 00` jmp .fix+0x80 (a v2-only exe has `E9 7F 78 AA 00`, to .fix+0x14) |

**Checks it makes:**
- PE32 with ImageBase 0x00400000.
- The fingerprint `A1 44 3A 80 00 8B 0D A0 3A 80 00` at 0x0051E730.
- Each site must be either original or exactly its own patched form. The 0x0051E790 site may also be in the v2 form.
- With no `.fix` section, every site must be original.
- An existing `.fix` must be at least 0x1000 bytes, and its first 20 bytes must equal the v1 stub.
- A section header must fit before SizeOfHeaders. Ours ends at 0x3A0, below 0x400, so it fits.
- It is idempotent: if everything matches, it does nothing.
- It makes a timestamped backup, then reads the file back and verifies every changed byte.
- It does not check the file size or a hash.

### 1.3 Design for our DLL (load time, no exe edit, no new section)

Use three `hook::Redirect` call-site rewrites to C++ functions. Each callee has exactly one caller, so nothing inside the functions needs patching. **H** for sites and bytes, **M** for behaviour (it mirrors TPW's runtime behaviour, which TPW's author verified by ear and with Frida, not us).

| Site | Original | Redirect to | Our function |
|---|---|---|---|
| 0x00415BA2 | `E8 59 68 10 00` (→0x0051C400) | `MusicFix_LoadState` | cdecl int(int stream): `r = Sound_LoadState(stream); Music_StartLevel(); g_pending = true; return r;` |
| 0x0054EC9F | `E8 8C FA FC FF` (→0x0051E730) | `MusicFix_StartLevel` | cdecl void(): `Music_StartLevel(); g_pending = true;` |
| 0x0054F870 | `E8 1B EF FC FF` (→0x0051E790) | `MusicFix_SelectTrack` | cdecl void(int track): if g_pending, clear it and call `Music_StartLevel()` (original, not re-arming); then `old = *(int*)0x803AA0; r = Sound_Command(old, 4, track)` (0x0051BC40, cdecl, 3 args, returns the handle); `*(int*)0x803AA0 = (r == 0 && old != 0) ? old : r;` |

- Our own re-prime call goes straight to 0x0051E730, so no SUPPRESS flag is needed. The pending flag is a plain bool, because everything runs on the game thread.
- This is equivalent to TPW's state machine: the natural call and the v1 re-call arm the flag, and the tick-1 re-call does not.
- Setting: `[Audio] LevelMusicFix=1` (default on).

**Detection, so we never stack on TPW Patcher:**
1. Walk the in-memory section table (`IMAGE_FIRST_SECTION` of `GetModuleHandle(NULL)`). If a section is named `.fix`, log "TPW Patcher present" and skip all three redirects.
2. Otherwise also require 0x0051E764 = `C3 90…` (12 bytes) and 0x0051E790 = `8B 44 24 04 8B`. TPW changes these, and so might a future tool.
3. `hook::Redirect` already refuses a site whose bytes are not the expected call, which covers 0x00415BA2.
4. Do not hard-code TPW's patched bytes: the .fix VA moves if another tool appended a section first.

Apply all three redirects or none: check all of them first, then patch.

**Credit / licence:** this is a re-implementation of TPW Patcher's fix, not a copy of its code. MIT still asks for attribution, so:
- credit "in-park music fix after TPW Patcher by TheAussie (MIT)" with the repo link in the README;
- add the MIT copyright line and permission notice to a third-party notices section or file;
- reference it in the source comment of the new module.
TPW's "healed saves" note applies to us too: a save written while the fix is active stores a valid sound block.

---

## 2. Mouse-wheel zoom in parks

### 2.1 The stock game already zooms with the wheel (H)

Path:
1. **WM_MOUSEWHEEL** (0x20A, compared at 0x0046B845, handled at 0x0046BB95) and the registered "MSWHEEL_ROLLMSG" (`[0x007A1C5C]`, 0x0046BBF9) both compute `(short)HIWORD(wParam) / 120` (signed, truncating; 0x88888889 multiply).
2. They take lParam as client coordinates (vx = x·2048/W, vy = y·1536/H) and call `UI_MouseWheel` 0x00658BCB(notches, vx, vy, 0).
3. This path is **not** behind the DirectInput-cursor gate at 0x0046B625. That gate's jump table only covers 0x200–0x208.
4. UI_MouseWheel (gated by g_UIInputEnabled) posts **0x1000D**(a = notches, b = vx<<16|vy) to the hovered widget, or to the capture widget if there is one.
5. In a park, the full-screen child of the root (0..0x7FF × 0..0x5FF), **g_ParkViewWidget 0x007CB2B0**, is created in 0x0048A410 / 0x00489F50 with handler **ParkView_WidgetProc 0x00488A00**. It handles keys itself and passes every message on to **Camera_OnWidgetMsg 0x0042A760**(msg, a, b) and then Widget_DefProc.
6. In 0x0042A760 (0x0042A91D): `if (a < 0) g_CameraZoom += 10 else g_CameraZoom -= 10`. The 10.0 comes from the float operands at 0x0042A929 (→0x006FDD5C = −10.0) and 0x0042A93A (→0x006FDD7C = +10.0).
7. Two other widget procs also forward to it (0x004A2840, 0x004BD4A0), so the wheel probably zooms in those views too. **L**

DirectInput **DIMOFS_Z** becomes an axis event (0x00634900), but CursorTracker_OnAxis 0x00634360 only uses offsets 0 and 4. Nothing turns Z into a wheel message. In practice the DirectInput mouse only supplies the cursor position (the hover), and the wheel comes from WM_MOUSEWHEEL. **H**

### 2.2 The zoom variable (H)

- **g_CameraZoom 0x007909EC** is a float camera distance. The default is 110.0 (static init 0x0042A135; reset by Home/Num5 through bit 0x100).
- Camera_Update 0x0042B1C0 clamps it when g_CameraInput bit 0x80 is set (camera init sets it):
  - normally **[70, 130]** (0x0042C626…; constants 0x006FDE08/0x006FDE0C);
  - with bit 0x200, **[40, 300]** (0x0074C9D8/0x0074C9DC). Bit 0x200 is toggled by Ctrl+Shift+Alt+Space (0x0042A520, camera group id 0x11), a debug wide zoom.
- Pitch follows zoom: f = (zoom−70)/60, pitch = 45°·(1−f) + 65°·f, capped at 84.3°. The eye is at distance `zoom` along that pitch.
- The value is used directly each frame. A notch is therefore an **instant 10-unit jump = 1/6 of the normal range**: 6 notches go from fully in to fully out.

### 2.3 Frame-rate dependence (H)

- **Wheel:** one fixed step per message. It does not depend on frame rate.
- **Keyboard zoom** (camera group ids 0/0x15 = Up/Num1 → bit 0x20, zoom in; ids 1/0x16 = Down/Num3 → bit 0x40, zoom out):
  - It runs at `dt × 0.05` units per frame, where dt = RealTime + 1 − last (ms), clamped to 1…100 (values over 200 become 100).
  - So it is time-based, with a +1 ms-per-frame bias: about 6% faster at 60 fps and 14% at 144 fps.
- **Rotation** (ids 6/0x12 = Left/Num9, 7/0x13 = Right/Num7) works the same way at 0.002 rad/ms, unless g_Config+0x3A is set (90° snaps).
- **Correction to startup-and-switches.md §8:** Num−/Num+/Num* (0x0040C3B0/C0/D0) are **game speed** on g_GameTimer 0x00785970, not zoom.
  - Num+ multiplies speed +0x20 by 1.25 (0x004032F0).
  - Num− multiplies it by 0.8 (0x00403340).
  - The speed is clamped to [0.25, 2.0] (0x006FD648/0x006FD650).
  - Num* sets it to 1.0 (0x004032D0).

### 2.4 Defects and the smallest design

The stock wheel has three defects:
- **(a)** A delta under 120 (smooth or precision wheels, touchpads) truncates to 0, and **0 counts as zoom in**. Scrolling down on a touchpad therefore zooms in.
- **(b)** A coalesced 240 still gives one step.
- **(c)** lParam of WM_MOUSEWHEEL is in screen coordinates, but the game treats it as client coordinates. This is harmless in borderless at 0,0, wrong in `Mode=window`, and the camera ignores it anyway.

Design, in our existing window subclass (src/input.cpp):
1. On WM_MOUSEWHEEL, add GET_WHEEL_DELTA_WPARAM to a per-window accumulator.
2. For each whole ±120 in it, call the original WndProc with wParam's high word = ±120 and lParam converted with ScreenToClient. Keep the remainder, and never forward a delta of 0.
3. This fixes (a), (b) and (c) and keeps the game's hover routing, so HUD lists still scroll and the 3D view zooms.
4. Optional finer step: `[Input] WheelZoomStep=10` (stock value).
   - Point the two operands at 0x0042A929 (expect 0x006FDD5C) and 0x0042A93A (expect 0x006FDD7C) at two DLL floats (−step, +step) with `hook::PatchU32`.
   - Do not change the shared constants: each is referenced 9 times.
   - Something like 5 per notch (12 notches over 70–130) feels closer to modern games. The game's own clamp still applies.
5. Smooth zoom (lerping g_CameraZoom toward a target each frame in the present hook) is possible later. It would have to respect bit 0x100 resets and the ride-camera modes (g_CameraFlags & 0x16, where pitch is not derived from zoom). It is not needed for the fix.
6. Do not call the Num+/Num− handlers: they change game speed.

---

## 3. FPS counter

### 3.1 The built-in one cannot be shown (H)

- `g_ShowFps` 0x007B05D0 is read only at 0x0054E395, at the top of Game_MainStateMachine 0x0054E360. That runs on every call, in every state (front end and park).
- When it is non-zero, the function calls `Debug_RenderFps` 0x0046C0D0 (thiscall, this = 0x007B05A8) with 1000/(gameMs − g_FpsLastTime).
- Debug_RenderFps sprintf's "FPS: %.4g" (0x0074D85C) into this+0x10 = 0x007B05B8. It then calls `Engine_DrawDebugText` 0x0057CB10 on object 0x008BBA50 with (x 0, y 0, text).
- **That function only stores the three values** at 0x008BBA60/64/68. A raw scan of the whole file finds 0x008BBA50…0x008BBA70 referenced only by its static initialiser 0x0057CAC0 and by Debug_RenderFps. Nothing ever reads the slot.
- The renderer was compiled out, like Debug_Trace. **Writing 1 to 0x007B05D0 therefore produces a string at 0x007B05B8 and draws nothing.** There is no position or font to report, apart from the unused (0,0).
- The value is also wrong for a frame counter. It uses game time (Timer_GetGameTimeMs), so it shows "inf" while paused (dt 0) and is scaled by the Num+/Num− speed.

### 3.2 Design: our own overlay

- **Measure:** count our own frames with QueryPerformanceCounter and refresh the text every 500 ms. src/framecap.cpp already measures this for its log.
- **Draw:** on the back buffer just before the flip, so it works for both renderers. `DirectDraw_FlipBuffers` 0x00564210 is thiscall with ecx = g_DDrawContext (`*(0x00879A24)`), takes no stack arguments, returns with a plain `ret`, and has two render callers:
  - D3D: `jmp` at **0x00573896**, `E9 75 09 FF FF`.
  - Software: `call` at **0x00573E17**, `E8 F4 03 FF FF`. Software copies its pixels into the back buffer inside Present, so drawing before Present would be overwritten. That is why we hook here.
  - Redirect both to a `__fastcall(ctx)` function that draws, then calls 0x00564210 with ecx = ctx. Leave the movie-frame callers (0x0051B20F/0x0051B248) alone.
- Back buffer: `*(IDirectDrawSurface4**)(ctx + 0x14)`. Draw with GetDC (vtable slot 17), SetBkMode TRANSPARENT, a black-shadowed white TextOut at about (8,8) using a fixed GDI font scaled to the height, then ReleaseDC (slot 26). **M** (GetDC on the D3D render target after EndScene is standard; not yet tried here).
- The engine already has a pre-flip callback, `[0x008BD510]` (called first thing in FlipBuffers). UI_InitColourCursor 0x0047E7D0 sets it to the colour-cursor draw 0x0047E9B0, with `[0x008BD514]` = 0x0047E9C0. So the cursor is drawn after our text, on top of it, which is fine. Chaining that pointer is an alternative, but it is written after we load. The call-site redirect is simpler.
- **Toggle key:**
  - **F2** (VK 0x71) is in none of the six keybind tables. The tables were dumped from the binary; F-keys reach them as raw VK codes because MapVirtualKey(…, 2) returns 0.
  - Keys in use: F1 (help, game group and widget 0x00488BA0), F3, F8 / Ctrl+Shift+Alt+F8, Ctrl+F5, Alt+F4 (ours).
  - F11 is also free in the game, but many players expect it to mean full screen. Avoid F10 (WM_SYSKEYDOWN, menu) and F12 (debugger break).
  - Handle the first WM_KEYDOWN of VK_F2 (lParam bit 30 clear) in the subclass and still pass the message on (no handler matches it).
  - Settings: `[Display] ShowFps=0` plus `FpsKey=F2`.

---

## New names

Sorted by address. The three Key_Zoom* entries replace the existing names at 0x0040c3b0/c0/d0, and Debug_RenderFps / Engine_DrawDebugText replace their [frame-timing] notes.

```
0x004032d0	Timer_SetSpeed	H: thiscall(double): g_GameTimer +0x20 = speed (Num* passes 1.0)
0x004032f0	Timer_SpeedUp	H: speed *= 1.25, clamped to [0.25, 2.0]
0x00403340	Timer_SlowDown	H: speed *= 0.8, clamped to [0.25, 2.0]
0x0040c3b0	Key_GameSpeedDown	H: Num- -> Timer_SlowDown (was Key_ZoomOut: it is game speed, not zoom)
0x0040c3c0	Key_GameSpeedUp	H: Num+ -> Timer_SpeedUp (was Key_ZoomIn)
0x0040c3d0	Key_GameSpeedReset	H: Num* -> Timer_SetSpeed(1.0) (was Key_ZoomReset)
0x0040ca20	Keybind_IsHeld	M: thiscall(group, id) -> nonzero while the bound key is held; Camera_Update polls the camera group 0x00787c14
0x0042a500	Key_CameraReset	H: Home / Num5: g_CameraInput |= 0x100 (zoom 110, yaw 0)
0x0042a520	Key_CameraToggleWideZoom	H: Ctrl+Shift+Alt+Space toggles g_CameraInput 0x200 (zoom clamp 40..300 instead of 70..130)
0x0042a760	Camera_OnWidgetMsg	H: cdecl(msg,a,b) from ParkView_WidgetProc: buttons -> g_CameraInput bits 1/2/4, cursor from 0x10001/0x10003, 0x1000d wheel: g_CameraZoom -= 10 (a>=0) / += 10 (a<0)
0x0042a990	Camera_Init	M: g_CameraZoom 110, yaw 0, eye 475/0/175, g_CameraInput = 0x80 | (old & 0x200)
0x0046c0d0	Debug_RenderFps	H: thiscall(this 0x007b05a8, float fps): sprintf "FPS: %.4g" into 0x007b05b8, Engine_DrawDebugText(0,0,text); nothing is drawn (slot never read)
0x0047e9b0	Cursor_PreFlipDraw	M: colour-cursor draw installed as g_PreFlipCallback
0x0047e9c0	Cursor_PostFlip	M: installed into 0x008bd514
0x00488a00	ParkView_WidgetProc	H: handler of the full-screen park view widget: keybind dispatch for keys, then Camera_OnWidgetMsg and Widget_DefProc for every message
0x0048a410	ParkUI_Create	M: creates g_ParkViewWidget (0..0x7ff x 0..0x5ff) with ParkView_WidgetProc
0x0051bc40	Sound_Command	M: cdecl(handle, cmd, arg) -> handle or 0; music uses cmd 4 (prime/select track)
0x0051bfc0	Sound_CreateRequest	M: cdecl(oldHandle, category, 2, 0, 0, 0) -> new request handle
0x0051c300	Sound_StopRequest	L: posts cmd 0x00700bac (handle, 0x3c) when sound is up
0x0051c350	Sound_SaveState	H: savegame Sound module writer: 0x802bd4, 0x802bc8, size 0x54, 0x54 bytes from 0x00803aa0
0x0051c400	Sound_LoadState	H: savegame Sound module reader; reads the saved block over g_LevelMusic 0x00803aa0 (the music bug, TPW Patcher v1). Only caller 0x00415ba2
0x0051e730	Music_StartLevel	H: g_LevelMusic = Sound_CreateRequest(g_LevelMusic, g_CatMusic, 2,0,0,0); g_LevelMusic = Sound_Command(.,4,0). Only caller 0x0054ec9f (park start)
0x0051e770	Music_StopLevel	M: Sound_StopRequest(g_LevelMusic) on park exit (0x0055002b)
0x0051e790	Music_SelectTrack	H: cdecl(track): g_LevelMusic = Sound_Command(g_LevelMusic, 4, track); stores 0 over a good handle (TPW Patcher v2). Only caller 0x0054f870, every 32 sim ticks
0x0051ec50	Sound_InitCategories	M: registers cat_ambient/cat_speech/cat_music..., sets g_CatMusic (TPW Patcher doc)
0x00564210	DirectDraw_FlipBuffers	H: thiscall(ctx = g_DDrawContext), no stack args; calls [0x008bd510] first; render callers 0x00573896 (jmp, D3D) and 0x00573e17 (call, software)
0x0057cb10	Engine_DrawDebugText	H: thiscall(x, y, text) only stores into 0x008bba60/64/68; never drawn
0x00787c14	g_KeybindCameraGroup	M: camera keybind group object (table 0x00748158)
0x007909ec	g_CameraZoom	H: float camera distance, default 110; clamp [70,130], or [40,300] with g_CameraInput 0x200; wheel +-10 per message, keys 0.05/ms
0x00790a38	g_CameraYaw	M: camera yaw (rad); keys 0.002/ms or 90-degree snaps
0x00790aac	g_CameraInput	H: camera input bits: 1/2/4 mouse buttons, 8/0x10 rotate, 0x20 zoom in, 0x40 zoom out, 0x80 clamp zoom, 0x100 reset, 0x200 wide zoom
0x007b05a8	g_FpsOverlay	M: this of Debug_RenderFps; text buffer at +0x10 (0x007b05b8)
0x007cb2b0	g_ParkViewWidget	H: full-screen park view widget (gets 0x1000d wheel when hovered)
0x00803a44	g_CatMusic	M: cat_music category handle
0x00803aa0	g_LevelMusic	H: level-music request handle; first dword of the 0x54-byte saved sound block
0x008bba50	g_DebugTextSlot	H: {.., text +0x10, x +0x14, y +0x18}; written by Engine_DrawDebugText, read by nothing
0x008bd510	g_PreFlipCallback	H: called at the start of DirectDraw_FlipBuffers when non-zero; colour cursor sets 0x0047e9b0
0x008bd514	g_PostFlipCallback	M: colour cursor sets 0x0047e9c0
```
