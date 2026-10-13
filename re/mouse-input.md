# Theme Park World: mouse and text input, and how to drive it headless

Target `testrun\TP.exe`. **H** = checked in the disassembly (tools\tpwre.py), **M** = decompiler output that makes sense but was not traced instruction by instruction, **L** = inference.

## 1. Two input modes, chosen once at UI init

`UI_InitCursorAndInput` 0x00489ca0 (called from game init at 0x0054deb3, after the switch parse at 0x0054dd65, so `/bwcursor` does take effect) picks the mode:

- **Colour cursor (default)**: `DAT_007a1a8c & 0x800` and not `/bwcursor` (`DAT_00788270`). It creates the DirectInput keyboard and mouse (0x005f55c0 / 0x005f5600), installs the button callback 0x00486c70 in the struct at 0x007b9fe0, then `UI_InitColourCursor` 0x0047e7d0 sets **`DAT_007b4b88 = 1`** (the only writer, 0x0047e951; it is set only if the cursor sprite 0x007b4b84 was created). **H**
- **Mono cursor (`/bwcursor`)**: no DirectInput devices, `DAT_007b4b88` stays 0. **H**

`UI_IsDInputCursor` 0x0047eaa0 returns `DAT_007b4b88`. It has exactly three callers (0x0046b625, 0x0046b70f, 0x0048a5e4). **H**

## 2. Path from mouse to UI

### 2.1 Window messages (WndProc_HandleInput 0x0046b600)

- On the first message, `UI_SetScreenSize` 0x006590d0 copies `DAT_007a1968`/`DAT_007a1930` (game width/height) into the int16s **`DAT_00faa5c4` (W)** and **`DAT_00faa5c0` (H)**. This happens only once. **H**
- **0x0046b625–0x0046b634: if `UI_IsDInputCursor()` is true, all WM_MOUSEMOVE and button messages are skipped.** In the default mode, posted mouse messages therefore do nothing. That explains the failure without `/bwcursor`. **H**
- Otherwise lParam is scaled to virtual coordinates: `vx = (x << 11) / W`, `vy = (y * 0x600) / H`, using signed integer division. The jump table at 0x0046bc78 then dispatches: 0x200 → `UI_MouseMove` 0x006588ef(vx,vy); 0x201/0x204/0x207 → `UI_MouseButtonDown` 0x00658af1(btn 0/1/2, vx, vy); 0x202/0x205/0x208 → `UI_MouseButtonUp` 0x00658b5b(btn, vx, vy). WM_MOUSEWHEEL and "MSWHEEL_ROLLMSG" go to `UI_MouseWheel` 0x00658bcb(delta/120, vx, vy, 0). **H**
- wParam (MK_*) is ignored. The button comes only from the message ID. **H**

### 2.2 DirectInput (default mode)

- The input manager object is at **0x00fa6e40**. Wrappers: 0x005f5640 gets the cursor position, 0x005f5650 sets the clip rect, 0x005f5590 polls. **H**
- Mouse device class: outer vtable 0x00703cf8, sub-object (+0x10) vtable 0x00703ce8. GUID_SysMouse is at 0x00707c40 (returned by 0x006348e0). The data format is **c_dfDIMouse at 0x00679440** (DIMOUSESTATE, 16 bytes, DIDF_RELAXIS, 7 objects). The keyboard class uses 0x00703d38/0x00703d28, GUID_SysKeyboard 0x00707c50 and format 0x0067a460. **H**
- Set-up: 0x00633bf0 calls `DirectInputCreateA`(0x500), then CreateDevice, then SetDataFormat (+0x2C). 0x0063e8c0 calls SetEventNotification (+0x30), allocates a 0x200-byte buffer at +0x30, and calls SetProperty (+0x18) with DIPROP_BUFFERSIZE = 32. **H**
- Cooperative level: 0x00633aa0 calls Unacquire (+0x20) and then SetCooperativeLevel (+0x34) on HWND `DAT_00fa7610` (the game window). 0x00633b90 calls Acquire (+0x1C). Default flags are 5 (0x00633a00). In `InputMgr_OnEngineMsg` 0x00618610, message 7 (sent by 0x0047e7d0 with field 5 = 0) sets **mouse = DISCL_EXCLUSIVE|DISCL_FOREGROUND (5)** and keyboard = NONEXCLUSIVE|FOREGROUND (6). With field 5 ≠ 0 it would use BACKGROUND|NONEXCLUSIVE (0xA) for both. **H**
  - Consequence: our window is never the real foreground window, so **Acquire always fails** (DIERR_OTHERAPPHASPRIO). No DirectInput mouse or keyboard data is ever produced. This is also why a test copy cannot grab the player's mouse. Keep it that way. **H** for the flags, standard DirectInput behaviour for the failure.
- Reading: an input thread (0x006324e0, `WaitForMultipleObjects` on the notification events) calls outer slot 3 (`DIMouse_Poll` 0x0063e970). That calls **GetDeviceData (+0x28)** with cbObjectData 16, buffer +0x30, count 32, flags 0, and re-acquires on DIERR_INPUTLOST. Slot 5, `DIMouse_ProcessBuffered` 0x00634900, then handles each record: dwOfs 0/4/8 (X/Y/Z) become axis events, and 0x0C–0x0F (BUTTON0–3) become button events 0xF0–0xF3 with pressed = `dwData & 0x80`. GetDeviceState (+0x24, 16 bytes) is used only when a listener is attached (0x00633691). **H**
- **Cursor position = accumulated relative deltas** in the cursor tracker `*(0x00fa6e40+0x64)` (vtable 0x00703cc8, constructor 0x00634220). Its axis handler 0x00634360 applies Windows-style acceleration (SPI_GETMOUSE thresholds at +0x50/+0x54/+0x58) and sensitivity (+0x30), adds to the floats **+0x20 (x) / +0x24 (y)**, clamps them to the rect at +0x34..+0x40 (0,0,W-1,H-1, set at 0x0047e867), and writes the ints **+0x10/+0x14** (client pixels). It also moves the colour-cursor sprite (+0x0C). Its button handler 0x00634420 swaps buttons if needed, flags double-clicks (GetDoubleClickTime), and copies the position to event +0x14/+0x18. **H**
- Main thread, `UI_Update` 0x0048a580 (each frame; front-end state 2 calls it via 0x0054e360): in DirectInput mode it reads the tracker position. If it changed since the last frame (0x007b9d00/04), it calls `UI_MouseMove((x<<11)/W, (y*0x600)/H)`. It then dispatches queued button events through 0x005f5590 to `UI_DInputButtonCallback` 0x00486c70(code, pressed, ?, x, y). That callback converts to virtual coordinates and calls UI_MouseButtonDown/Up (0xF0 → 0, 0xF1 → 1, 0xF2 → 2, debounced by 0x007c24dc/e0/e4). **H**
- The tracker constructor 0x00634220 calls `SwapMouseButton(FALSE)` and, if the buttons were swapped, `SwapMouseButton(TRUE)` again. This briefly changes a **system-wide** setting (IAT 0x006fd330). **H**

### 2.3 Button state from GetAsyncKeyState

`GetAsyncKeyState(VK_LBUTTON/VK_RBUTTON)` at 0x0054e3e1/0x0054e3fc only counts held frames into `0x008785dc`/`0x008786dc`. **Nothing reads those counters.** The click path never uses them. **H** (xrefs: write-only)

### 2.4 Widget layer (common to both modes)

- `UI_MouseMove` 0x006588ef: if `g_UIInputEnabled` **`DAT_0077c488`** == 0, it returns. It stores the virtual position in 0x00faa540/0x00faa544. If the root `DAT_00faa5d8` is set and nothing has captured the mouse (`DAT_00faa5e8` = 0), it hit-tests (0x0065db25) from the root down. If the hovered widget `DAT_00faa5e0` changed, it sends 0x10002 (leave) to the old widget and **0x10001 (enter)** to the new one. If the widget is the same, it sends **0x10003 (move)**. **H**
- ButtonDown sends 0x10005(btn, vx<<16|vy) to `DAT_00faa5e0` and remembers that widget in 0x00faa588[btn]. ButtonUp sends 0x10004 to the remembered widget. Both are gated on `DAT_0077c488`. **H**
- Messages are queued (0x00658ccc → queue `DAT_00faa5f0`) and dispatched next frame by 0x00658e42 in UI_Update. There are no focus, visibility or activation checks in the dispatch (0x006698e6), apart from the target's "dying" byte +4. **H**
- **The 3D front-end menu** (object `DAT_00f82884`, handler 0x005d5dd0) records the cursor position at +0x20/+0x24 **only from 0x10003** (not from 0x10001). It records button bits at +0x1C from 0x10005/0x10004. Each frame, 0x005dc860 casts a ray from (+0x20/1024−1, (1536−(+0x24))/768−1) and stores the hovered item at +0x70. On 0x10005 with button 0, menu items (for example 0x005dd530, 0x005dd840) select the hovered +0x70 item. **H**
  So a click needs, in order: a move that makes the front end the hovered widget, **a second move to the same widget (0x10003)**, at least one rendered frame for the pick, then down and up.

## 3. Gates that matter in our setup

| Gate | Effect | Headless status | Conf |
|---|---|---|---|
| `DAT_007b4b88` (DirectInput cursor) | WndProc drops every WM_MOUSE* | **1 by default**, which is the cause without `/bwcursor` | H |
| DirectInput mouse EXCLUSIVE\|FOREGROUND | Acquire fails unless our window is the real foreground window, so no DirectInput data | always fails headless (good: never grabs the player's mouse) | H |
| `DAT_0077c488` (UI input enabled) | move, button, wheel, key and char all dropped when 0 | **starts at 1** (initialised data). Cleared only by WM_ACTIVATEAPP(FALSE) (0x006593c5) and set by WM_ACTIVATEAPP(TRUE) (0x00659333) | H |
| `DAT_007a1a14 & 8` (app active) | set or cleared by WM_ACTIVATEAPP. Read only at 0x0054f8e0 (skips park frames when inactive **and** fullscreen) and 0x0042b5bb (inside the camera update 0x0042b1c0, probably edge scroll) | 0 headless; harmless for menus | H / L (meaning) |
| `DAT_00faa5e8` (mouse capture) | when set, moves go only to the capturing widget | unknown, check at run time | M |
| GetFocus == `DAT_008bd518` | present and render (0x00583730, 0x00576ec0), not input | already hooked | H |

Not present: **GetCursorPos, SetCursorPos, ClientToScreen, ClipCursor, SetCapture, TrackMouseEvent, IsIconic, IsWindowVisible, GetActiveWindow, SetFocus** are not imported (pefile import table). `ScreenToClient` (IAT 0x006fd318) is used only to place intro movies (0x0054e005, 0x0054e229). The off-screen position (-32000) therefore never reaches input. **H**

User32 IATs relevant to input: GetAsyncKeyState 0x006fd320, GetKeyState 0x006fd324, GetFocus 0x006fd328, ShowCursor 0x006fd30c, SwapMouseButton 0x006fd330, GetDoubleClickTime 0x006fd32c, SystemParametersInfoA 0x006fd374, MapVirtualKeyA 0x006fd344. DirectInputCreateA 0x006fd020. **H**

**Why `/bwcursor` + posted messages still failed is not explained by the static code.** In that mode the WM path reaches UI_MouseMove with nothing in between. Read these at run time, after the second move and after the press: `DAT_007b4b88` (must be 0), `DAT_0077c488` (must be 1), `DAT_00faa540/544` (should be about 1328,115 for 830,54 at 1280×720), `DAT_00faa5e0` (hovered widget), `DAT_00faa5e8` (capture, should be 0), `*(DAT_00f82884+0x20/+0x24)`, and the menu object's +0x70. One check is enough to tell whether the messages are not arriving, `DAT_0077c488` is 0, or the hit test lands on another widget. **L**

## 4. Coordinate mapping

- Virtual UI space is 2048×1536 (root widget 0..0x7FF × 0..0x5FF, created at 0x006584df; the pick at 0x005dc860 uses /1024 and /768). **H**
- `vx = cx * 2048 / W`, `vy = cy * 1536 / H` (integer division). Client pixel (cx,cy) is in the game's own render size. W and H are the int16s at 0x00faa5c4/0x00faa5c0, equal to `DAT_007a1968`/`DAT_007a1930`. The F8 screenshots are W×H, so a point measured on a screenshot is already the client pixel. At non-4:3 sizes x and y scale differently. Drawing and hit-testing use the same mapping. **H** for the formula, **M** for drawing.

## 5. Text entry (name dialog)

- WM_CHAR (0x102): `MultiByteToWideChar(CP_ACP)` of wParam. **Characters < 0x20 are dropped.** Hebrew (language 0x40D, 0x0041a850) remaps keys through 0x0046b460. It then calls `UI_Char` 0x00658c96(wch, flags), which sends 0x1000C. **H**
- **Custom 0x410**: `UI_Char(wParam, 0)` directly (no conversion, no < 0x20 filter). This is the simplest way to inject UTF-16 characters. **H**
- WM_KEYDOWN/UP and WM_SYSKEYDOWN/UP: key = `MapVirtualKeyA(vk, 2)` (Enter 0x0D, Backspace 0x08, letters upper-case), else the raw vk. Numpad + − * / . and keys with the **extended bit (lParam bit 24)** become `vk<<8` (arrows 0x2500–0x2800, which the front end uses). `UI_KeyDown` 0x00658c38 sends 0x1000A and `UI_KeyUp` 0x00658c67 sends 0x1000B, both gated on `DAT_0077c488`. The keybind dispatch 0x0040c900 is not gated. Enter and Backspace in a text field therefore come from WM_KEYDOWN, not WM_CHAR. **H**
- Key and char messages go to the focus widget (0x00faa578 / 0x00faa5ec / 0x00faa5dc, flag bit 1 at +0x48) in 0x006698e6. **M**
- The DirectInput keyboard is never acquired headless and is not needed. The name dialog 0x004993e0 is a game widget, not a Win32 dialog (DialogBox is not imported). **H** / **M**

## 6. Recipe for the headless autopilot

1. **Do not feed DirectInput.** Leave its Acquire failing. Optional: hook SwapMouseButton (0x006fd330) to return FALSE without calling the real API, so the test copy never touches the player's button setting.
2. **Inject at the widget layer on the game thread**, which works in both cursor modes. Subclass the game window (or wrap WndProc 0x0046bcb0) and handle a private `WM_APP+n` posted by the worker. In the handler, convert client px to virtual (`vx = cx*2048/W`, `vy = cy*1536/H`, W/H from int16 0x00faa5c4/0x00faa5c0) and call the cdecl functions:
   - `UI_MouseMove` 0x006588ef(short vx, short vy), **twice, at least one frame apart** (enter, then the 0x10003 the front end needs);
   - wait for at least one or two frames (the per-frame pick);
   - `UI_MouseButtonDown` 0x00658af1(0, vx, vy), then one or more frames later `UI_MouseButtonUp` 0x00658b5b(0, vx, vy).
   Alternative: write byte `DAT_007b4b88 = 0` once after UI init, and the existing posted WM_MOUSEMOVE/WM_LBUTTONDOWN/UP path works unchanged. In that case the colour cursor sprite stays where it is (cosmetic). To move it as well, write the tracker floats +0x20/+0x24 and ints +0x10/+0x14 at `*(0x00fa6e40+0x64)`.
3. **Activation is not needed.** `DAT_0077c488` starts at 1. Never send WM_ACTIVATEAPP(FALSE). It disables all UI input and pauses the game.
4. **Text:** post 0x410 with wParam = UTF-16 char (or WM_CHAR for printable ASCII). Use WM_KEYDOWN/UP for Enter (VK_RETURN) and Backspace. Set lParam bit 24 for arrow keys.
5. If it still does nothing, read the globals listed at the end of section 3.

## New names

```
0047eaa0	UI_IsDInputCursor	returns DAT_007b4b88; true = WndProc ignores WM_MOUSE*, UI_Update polls DirectInput
0047e7d0	UI_InitColourCursor	creates colour cursor sprite, clip rect, sends engine msgs 6/7, sets DAT_007b4b88=1
0047e9d0	Input_PostActivate	engine msg 4 {active} from WM_ACTIVATEAPP (ShowCursor + re-Acquire)
00486c70	UI_DInputButtonCallback	(code 0xf0..f2, pressed, ?, x, y) px -> virtual, calls UI_MouseButtonDown/Up
0048a580	UI_Update	per-frame UI: queue dispatch; in DirectInput mode polls cursor tracker and button events
006590d0	UI_SetScreenSize	stores W/H int16 into 0x00faa5c4/0x00faa5c0 (first WndProc message)
006588ef	UI_MouseMove	cdecl(vx,vy): sets 0x00faa540/544, hit-test, sends 0x10001/0x10002/0x10003
00658af1	UI_MouseButtonDown	cdecl(btn,vx,vy): 0x10005 to hovered widget 0x00faa5e0
00658b5b	UI_MouseButtonUp	cdecl(btn,vx,vy): 0x10004 to widget that got the press
00658bcb	UI_MouseWheel	cdecl(delta/120,vx,vy,0): 0x1000d
00658c38	UI_KeyDown	cdecl(key,flags): 0x1000a
00658c67	UI_KeyUp	cdecl(key,flags): 0x1000b
00658c96	UI_Char	cdecl(wchar,flags): 0x1000c
00658ccc	UI_PostWidgetMsg	queues (widget,msg,a,b) into 0x00faa5f0 / 0x00faa5f4
00658e42	UI_DispatchQueue	pops and dispatches up to n queued widget messages, observer 0x00faa5fc
0065db25	UI_HitTest	deepest visible widget containing (vx,vy)
00659333	UI_EnableInput	DAT_0077c488=1, sends 0x21 to widget tree (WM_ACTIVATEAPP TRUE)
006593c5	UI_DisableInput	DAT_0077c488=0, clears button bits (WM_ACTIVATEAPP FALSE)
006584df	UI_CreateRoot	root widget 0..0x7ff x 0..0x5ff (2048x1536), queues
00485780	UI_GlobalMsgObserver	DAT_00faa5fc: sees every dispatched widget message
005d5dd0	FrontEnd_HandleMsg	stores cursor from 0x10003 at +0x20/+0x24, button bits +0x1c, forwards to children
005dc860	FrontEnd_PickMenuItem	per-frame ray pick from front-end cursor, hovered item at +0x70
00618610	InputMgr_OnEngineMsg	msg 4 activate (Acquire), 6 suspend, 7 set coop levels (mouse 5, kbd 6 or both 0xA)
005f5640	InputMgr_GetCursorPos	returns &tracker->x (client px), manager 0x00fa6e40
005f5650	InputMgr_SetCursorClip	tracker clip rect +0x34..+0x40
005f5590	InputMgr_Poll	pumps messages, then dispatches queued input events to a callback struct
006324e0	Input_ThreadLoop	WaitForMultipleObjects on DirectInput notification events, calls device Poll
00633a00	DIDevice_BaseCtor	coop flags default 5, want-acquire 1
00633aa0	DIDevice_ApplyCoopLevel	Unacquire + SetCooperativeLevel(hwnd 0x00fa7610, flags)
00633b90	DIDevice_Acquire	apply coop level then Acquire (+0x1c)
00633d10	DIDevice_SetCoopFlags	sets EXCLUSIVE/NONEXCLUSIVE/FOREGROUND/BACKGROUND bits and re-acquires
0063e8c0	DIDevice_InitBuffered	SetEventNotification, 32-record buffer, DIPROP_BUFFERSIZE
0063e970	DIDevice_PollBuffered	GetDeviceData(16, buf, 32, 0), re-acquire on INPUTLOST, then ProcessBuffered
00634900	DIMouse_ProcessBuffered	DIMOFS_X/Y/Z -> axis events, BUTTON0..3 -> 0xf0..0xf3
006345d0	DIMouse_Ctor	vtables 0x00703cf8 / 0x00703ce8, GUID_SysMouse, c_dfDIMouse 0x00679440
00634ee0	DIKeyboard_Ctor	vtables 0x00703d38 / 0x00703d28, GUID_SysKeyboard
00634220	CursorTracker_Ctor	vtable 0x00703cc8; SPI_GETMOUSE, GetDoubleClickTime, SwapMouseButton probe
00634360	CursorTracker_OnAxis	accelerated relative delta into float pos +0x20/+0x24
00634420	CursorTracker_OnButton	button swap, double-click flag, attaches pos to event
006344f0	CursorTracker_Clamp	clamps float pos to rect, writes int pos +0x10/+0x14
0054df20	Movie_PlayIntro1	plays bf.tgq, positions with ScreenToClient when windowed
0054e070	Movie_PlayIntro2	plays a random ride movie, same positioning
00679440	c_dfDIMouse	DIDATAFORMAT for DIMOUSESTATE (data)
0077c488	g_UIInputEnabled	init 1; 0 drops all UI mouse/key input (data)
007b4b88	g_DInputCursor	1 = colour cursor and DirectInput mouse mode (data)
00fa6e40	g_InputManager	engine input manager; +0x58 kbd, +0x5c mouse, +0x64 cursor tracker (data)
```
