# Theme Park World: front-end menus, Options screen, and how to add an HD Settings page

Target `testrun\TP.exe`. **H** = checked in the disassembly (tools\tpwre.py), **M** = decompiler output that makes sense but was not traced instruction by instruction, **L** = inference. Builds on mouse-input.md (widget input layer), startup-and-switches.md (config.tcf) and text-rendering.md (fonts).

## 0. Summary

- Every 2D screen is a **widget tree built at run time from a small bytecode layout compiled into the exe's .data**. Nothing comes from `data\ui` (it only holds cursors) or from a script file. Art is looked up by **name hash** in sprites loaded from `ui.wad`. **H**
- The main menu ("Create New Player" x4 + "Quit Game") is the **player-select panel**, layout 0x00753c68, built by `FE_PlayerSelect_Build` 0x004a6580. Its buttons are plain type-1 widgets with a text renderer, an orange colour and a cdecl click handler. **H**
- The game's **Options screen** (`Options_Open` 0x004a3a30, layout 0x00752f30) is reached only from the two **Esc text menus**: in the front end once a player has been chosen (item id 0xb) and in the park (item id 5). It is not on the player-select panel. **H**
- Those Esc menus use a reusable **TextMenu** class (0x00492e40..0x004931e7): a full-screen dimmed backdrop plus a column of centred text buttons, where each item text is an **arbitrary `wchar_t*`**, and one menu callback that receives `0x100001(itemId)`. This is the engine's own text-button list, the TPW equivalent of what WarBreeds HD reused. **H**
- Smallest hooks: (a) one more sign on the player-select panel (wrap the two `call 0x4a6580` sites), and/or (b) one more item in the Esc menus (wrap `call 0x48c600` / `call 0x48c150` in 0x0048c830). Build the page itself as a TextMenu with our own callback. **H** for the hook sites.

## 1. Front-end structure

### 1.1 Game states (Game_MainStateMachine 0x0054e360, state `DAT_0087906c`)

- **State 1**: front-end init. Allocates the front-end manager (0x38 bytes, ctor 0x005d5770, vtable 0x007029f0) into **`g_FrontEnd` 0x00f82884**, calls `FrontEnd_Init` 0x005d5970, then sets state 2. **H**
- **State 2**: while `[g_FrontEnd+0x14] == 1`, each frame runs UI_Update 0x0048a580, `FrontEnd_Update` 0x005d5c50 and `FrontEnd_Render` 0x005d5cc0. **H**
- **State 3**: `FrontEnd_Shutdown` 0x005d5cf0, then the manager is freed. Result 2 goes to state 9, anything else to 0xc (park load). `FrontEnd_RequestQuit` 0x005d5db0 sets `+0x14 = 3`. **H**

### 1.2 Front-end manager and components

- The manager holds a list (+0x18) of **components**. The component base vtable is 0x007029d0; the ctor 0x005d5700 registers the component with `g_FrontEnd->vtbl[0]`. Component slots: +4 init, +8 update, +0xc render, +0x10 shutdown, +0x14 message. **H**
- `FrontEnd_Init` 0x005d5970: `Layout_Create(root, 0x00774c18, FrontEnd_RootProc 0x005d58b0)` builds the full-screen front-end root widget 0xbf431. Its child **0xbf432** is the bottom-right "i_mail" (postcards) button at (1904,1402)-(2007,1504), handler 0x005d58e0. The function then creates the singletons 0x00f83774 (0x005d84a0), 0x00f83778 (0x005d8b00), 0x00f83784 (0x005da200) and **the screen manager `DAT_00f85614`** (0x005e3c20, vtable 0x00702d78). **H**
- Screen manager init 0x005e3dc0 creates five front-end screens at +4 (0xe8 bytes, vtable 0x00702ec0), +8 (0xec, 0x00702dd0), +0xc (0x7c, 0x00702e70), +0x10 (0x7c, 0x00702e20) and +0x14 (0x2c, 0x00702da0). These are the 3D island scenes and lobby (base ctors 0x005dfcd0 / 0x005dc580). Their items are 3D objects picked by ray (FrontEnd_PickMenuItem 0x005dc860), **not widgets**. **H** (structure) / **L** (which scene is which)
- At the end of `FrontEnd_Init`: if `DAT_00f81ebc` (player manager)`+0x60 == -1` (no current player), it calls **`FE_PlayerSelect_Build` 0x004a6580** (call at 0x005d5bec). It then activates screen +4 (or +0x10 when online) and +0x14. **H**
- Screen-manager message slot 0x005e41c0: on key-up 0x1000b with key 0x1b (Esc), if no active screen consumed it, it calls **`EscMenu_Show(1)` 0x0048c830** (the front-end Esc menu). **H**

### 1.3 The main menu = player-select panel (layout 0x00753c68)

`FE_PlayerSelect_Build` 0x004a6580 (callers: 0x005d5bec at front-end start, and 0x0048bc5d, the "Select New Player" confirmation):

1. Disables all keybind groups (0x0040cfa0) and hides the front-end root (`[g_FrontEnd+4]->Show(0)`). **H**
2. `g_PlayerSelect` **0x007cb3f8** = `Layout_Create(g_UIRoot 0x00faa5d8, 0x00753c68, PlayerSelect_RootProc 0x004a5fc0)`, then `SetKeyFocus` 0x0065e59b. **H**
3. Tree (virtual 2048x1536, rect = x1,y1,x2,y2):

| id | type | rect | art | role |
|---|---|---|---|---|
| 0x7a13 | 1 | 0,0,2048,1536 | none | panel root (`g_PlayerSelect`) |
| 0x7a14 / 0x7a19 / 0x7a1a / 0x7a1b | 1 | 884,46,2048,193 (then +199 per row: 245, 444, 643) | `b_login` | player slot sign |
| ..child 0x7a15 | 1 | 941,75,1716,164 (per row) | none | slot text ("Create New Player" or the name) |
| ....child 0x7a16 | 2 (BUTTON) | 1919,68,2021,171 | `b_dellog` | delete player |
| ..child 0x7a17 | 6 (IMAGE) | 1762,68,1865,171 | `e_joy` | icon |
| 0x7a18 | 1 | 32,1321,442,1497 | `tpwlogo` | logo |
| **0x7a1c** | 1 | **1264,895,2428,1042**, text rect 1339,924,2039,1013 | `b_login` | **"Quit Game"** |

   Art names were recovered by matching the layout hashes (hash: `h = (h ^ (signed char)c) * 0x2f`, 0x006588b6) against strings in TP.exe and ui.wad. **H**
4. Each text widget (the four 0x7a15 and 0x7a1c) gets this set-up sequence, which **is the recipe for a game-style text button** (all thiscall on the widget unless noted). **H**
   - `SetTextRenderer` 0x0065d1c8(`new(0x1a4)` → `TextRenderer_Ctor` 0x00491920(0))
   - `SetFont` 0x0065c388(`Font_GetFromActiveTier(5)` 0x00485a70, cdecl)
   - `SetTextAlign` 0x0065c428(1,1) (centred both ways, **M** for the meaning)
   - `vtbl+0x24 SetText`(wchar_t*): `GetUIText(8)` for Quit, 0 for the slots (filled later by 0x004a62b0)
   - `Widget_SetYellow` 0x00492d20(widget) (cdecl)
   - `SetColour` 0x0065c5d5(0xec,0xa2,0x04,0xff) (orange, bytes at 0x007540c0/c4/c8)
   - `SetUserData` 0x0065e6d5(0, slotIndex)
   - `SetHandler` 0x0065e526(proc): slots 0x004a6000, Quit 0x004a61d0
5. `PlayerSelect_RefreshSlots` 0x004a62b0 sets each slot's text to the player name or `GetUIText(0x1a5)` "Create New Player" and shows or hides the delete button. **H**

**Click dispatch.** The handler is `int __cdecl proc(Widget* w, int msg, int a, int b)` and passes anything it does not handle to `Widget_DefProc` 0x0065f66e (thiscall w; msg, a, b; it calls the class proc at w+0x118). DefProc records the pressed button in w+0x11c on 0x10005 and posts **0x10006 (click)** on 0x10004 if the button was pressed on the same widget within the click time 0x0077c480 (0x0065f995). Quit's proc: 0x10001 sets red (0xff,0,0), 0x10002 restores orange, 0x10006 opens `MessageBox` 0x0047f020 with `GetUIText(9)` and the "okay" callback 0x004a6290 → `FrontEnd_RequestQuit`. The slot proc on 0x10006 loads the player (0x005c83b0) and closes the panel (`FE_PlayerSelect_Close` 0x004a6a50), or opens the new-player name dialog (0x004a6e40). A type-2 BUTTON child notifies its parent with **msg 0x100 (a = button id, b = state)**, which is how the delete button reaches the slot proc. **H**

`FE_PlayerSelect_Close` 0x004a6a50: destroy panel (`UI_SendMsg(w,4,0,0)`), show the front-end root, re-enable keybinds, then continue into the island (0x004b90f0). **H**

### 1.4 Widget library facts needed to build UI

| Function | Address | Convention | Notes | Conf |
|---|---|---|---|---|
| Layout_Create | 0x0065fd0e | thiscall(parent; const u16* layout, proc), ret 8 | runs bytecode, returns the last top-level widget, sets its proc | H |
| Layout_Exec | 0x0065fd58 | thiscall(parent; layout) | interpreter, index global 0x00faa620 (not re-entrant) | H |
| Widget_CreateChild | 0x0065dd7b | thiscall(parent; type, flags, id, x1, y1, x2, y2), ret 0x1c | type 1 WINDOW 0x138 bytes (ctor 0x0065bcad, vtbl 0x00705980), 2 BUTTON 0x15c (0x006688b3, vtbl 0x00705a50), 3 slider, 6 image … 0xd; appended as last child | H |
| Widget_FindChild | 0x0065d7d0 | thiscall(id), ret 4 | **direct children only** | H |
| Widget_Show | 0x0065d9dd | thiscall(bool), ret 4 | flag bit 0 at +0x48 | H |
| Widget_SetImage | 0x0065d070 | thiscall(renderer), ret 4 | +0x98 (background / art) | H |
| Widget_SetTextRenderer | 0x0065d1c8 | thiscall(renderer), ret 4 | +0x9c | H |
| Widget_SetFont | 0x0065c388 | thiscall(font*), ret 4 | +0x4c; set it **before** SetText | H |
| Widget_SetTextAlign | 0x0065c428 | thiscall(h, v), ret 8 | 1,1 everywhere | H/M |
| Widget_SetColour | 0x0065c5d5 | thiscall(r,g,b,a), ret 0x10 | text tint | H |
| Widget_SetText | vtbl+0x24 = 0x0065f645 | thiscall(wchar_t*), ret 4 | text renderer **rasterises at call time**, so the buffer can be reused at once; SetText again to change it | H (call) / M (copy) |
| Widget_SetHandler | 0x0065e526 | thiscall(proc), ret 4 | +0x114 | H |
| Widget_SetUserData / GetUserData | 0x0065e6d5 / 0x0065e705 | thiscall(i<16, v) / (i) | +0xa0[16] | H |
| Widget_GetId / GetParent | 0x0065d7bc / 0x0065d55a | fastcall | +0x8c / +0x60 | H |
| Widget_SetKeyFocus / ReleaseKeyFocus | 0x0065e59b / 0x0065e5fa | thiscall(0) | focus global 0x00faa5ec, sends 0x1f / 0x20 | H |
| Widget_AttachChild / Detach | 0x0065f447 / 0x0065f538 | thiscall(child, setDepth, setCentre) / fastcall | | H |
| Widget_SetDepth | 0x0065f16b | thiscall(short) | draw order | M |
| UI_SendMsg | 0x0065f697 via 0x00658d9f | cdecl(w, msg, a, b) | msg **4 = destroy** (frees the subtree), 6 = disable (a=0) / enable (a=1) | H (call) / M (meaning of 6) |
| UI_FindImageByName | 0x00659bc9 | cdecl(const char*) | returns a new renderer instance, e.g. "b_login", "lolight", "hilight", "b_okay", "b_exit" | H |
| UI_FindImageByHash | 0x00659c42 | cdecl(u32) | what layout op 1 uses | H |
| TextRenderer_Ctor | 0x00491920 | thiscall(0) on `operator new` 0x0067acc0(0x1a4), ret 4 | vtbl 0x006ff530 | H |
| GetUIText | 0x00485b00 | cdecl(id) | returns the **shared static buffer 0x007ba058**; copy before the next call | H |
| PlaySfx | 0x00485aa0 | cdecl(id) | 0xc1 = menu click | H |
| MessageBox | 0x0047f020 | cdecl(desc*, b1Type, b1Cb, b2Type, b2Cb, b3Type, b3Cb, flag) | desc = {wchar_t* text; u8 r,g,b,pad; int font}; types 1 "b_okay", 2 "b_exit", 0 hidden; callbacks `int cdecl(void)` | H |

Widget fields: rect +0x0c/+0x0e/+0x10/+0x12 (shorts, inside the Rect at +8), text rect +0x2c, flags +0x48 (bit0 visible, bit1 ignored by hit test), next sibling +0x54, previous +0x58, first child +0x5c, parent +0x60. **H**

Hit test 0x0065db25 walks every child and **the last matching sibling wins**, so a widget attached later is on top for input. **M**

Layout bytecode (u16 stream; `Layout_Exec`):

| op | operands | effect |
|---|---|---|
| 0 | type u16, flags u32, id u32, x1 y1 x2 y2 (s16) | CreateChild, then nested block until op 5 |
| 1 / 2 | u32 name hash | SetImage / SetTextRenderer(FindImageByHash) |
| 3 | 4 s16 | text rect (+0x2c) |
| 4 | sub 1/3: rect; 2: circle; 4: n points | hit region |
| 5 | none | end of block |
| 6,7,8,0xe,0xf / 0xc,0x10 (+u16) | rect | auto-created button/image child with nested block |
| 0xb / 0xd / 0x11 / 0x12 | various | columns / ? / +0x90 / +0x94 (always -1) |

**H.** A text-keyword UI-script parser also exists (WINDOW, BUTTON, SCROLL, LISTBOX, EDITBOX, IMAGE, LISTCONTROL, CHECKBUTTONS, BAR, TEXTBOX, KNOB; 0x0066db20 / 0x0066dc2c, table 0x0077c4ec). Its entry 0x006607ca has **no callers and no pointer anywhere in the exe**, so it is dead code. **H**

Message ids seen: 4 destroy, 5 close (player-select root closes on it), 6 enable/disable, 0x11 reset colours, 0x14 being destroyed, 0x15 created, 0x1e per-frame tick, 0x1f/0x20 focus in/out, 0x100 child button, 0x800/0x801/0x802 slider set/drag/release, 0x10001..0x10006 enter/leave/move/up/down/click, 0x1000a/b/c key down/up/char, 0x1000d wheel, 0x100001 TextMenu item chosen. **H** for the ones in handlers quoted above, **M** for the rest.

## 2. The game's Options screen

- **Reachable from:** the front-end Esc menu (`EscMenu_BuildFrontEnd` 0x0048c600, item id 0xb "Options" → handler 0x0048bd40 case 0xb → 0x0048bf28 `call 0x4a3a30`; only added when 0x005b6230() == 0, i.e. offline) and the in-park Esc menu (`EscMenu_BuildPark` 0x0048c150, item id 5 → handler 0x0048b6a0 case 5 → 0x0048b977). The front-end Esc menu only opens from the island/lobby screens (after a player is chosen), not over the player-select panel. **H** (paths) / **M** (not over player-select: focus is on the panel)
- **Build:** `Options_Open` 0x004a3a30: `Config_Backup` 0x00423b00 (copies 15 dwords to +0x3c), disables the HUD or front-end root (msg 6), `g_Options` 0x007cb2fc = `Layout_Create(root, 0x00752f30, Options_Proc 0x004a2bf0)`, title `UI_SetTitleText` 0x00485d20(root, 0x1d4d4, GetUIText(0x13b) "Game Options"). Rows are fixed in the layout: **sliders (type 3, knob art `b_scroller`)** 0x1d4d5 resolution, 0x1d4d6 graphics quality, 0x1d4e0 audio quality, 0x1d4d8/da/dc/de sound/music/speech/movie volume; **toggle buttons (type 2, art `on`/`b_on2`)** 0x1d4d7/d9/db/dd (mute toggles), 0x1d4c1..c7 (advisor, tutorial, popup help, confirmations, push scroll, rotation, scroll); **text rows** 0x1d4c8 (3D card / software), 0x1d4ca (video card), 0x1d4cb..d1. Each row's label (child 0x1d4c9) is "label: value" built with wcscpy 0x0067b0d0 / wcscat 0x0067bed0 into the shared 0x007ba058 buffer and set with SetText. OK = button id -1 (`b_okay`), cancel = -2 (`b_exit`). The page is full: rows already span y 167..1457. **H**
- **Reads:** straight from the config object 0x0078d8d8: 3D 0x0078d8d8, resolution 0x0078d8dc (slider = value*100/2), detail 0x0078d8e0, video card 0x0078d8e4, the volume and enable bytes 0x0078d8e8..0x0078d904, sound quality 0x0078d908, and the flag bytes 0x0078d90c..0x0078d912. **H**
- **Writes:** `Options_SliderChanged` 0x004a2e90 (msg 0x800): resolution → `Config_SetResolution` 0x00423b60((v*2)/100), so **only 0/1/2 = 512x384 / 640x480 / 800x600**, and the label becomes "Screen resolution: " + string 0x155..0x157. Detail → 0x00423ba0. Volumes apply live (0x0051bba0 / 0x0051bb80 / 0x0051bbc0). Toggles → 0x004a3480. **H**
- **Apply (OK):** `Config_Apply` 0x004237f0 (this = 0x0078d8d8). `Config_ComputeChangeLevel` 0x00423740 applies a detail change live (0x00423bc0) and sets +0x94 = 2 ("restart") when 3D, resolution or video card changed. At level 2 it shows MessageBox `GetUIText(0x193)` "RESTART GAME / You need to quit and restart…" with "okay" → 0x00424bb0 (front end: `g_FrontEnd+0x14 = 3`, i.e. quit; park: save and quit) and "exit" → 0x00424c10. It then **writes save\config.tcf at once** (`Config_SerializeTcf` 0x004242c0 mode 1). **Cancel:** `Config_Restore` 0x00423ad0, then destroy. **H** (flow) / **M** (button roles)
- **Could we add rows or replace the resolution control?** Adding rows means new widgets on a full layout, so it is not practical. Replacing the resolution slider is possible but fiddly: hook 0x004a2e90 case 0x1d4d5 (and the label code in 0x004a3a30), map the 0..100 slider to our list, and keep 0x0078d8dc unchanged so the stock restart logic does not fire, or let it fire on purpose. The cheapest useful change is to **hide** that row after 0x004a3a30 returns (`FindChild(g_Options,0x1d4d5)->Show(0)`) or relabel it to point at the HD page, because our patch owns the resolution. **M**

## 3. The TextMenu class (the Esc menus): a ready-made settings list

Object 0x14 bytes: +0 item proc, +4 item count, +8 = -1, +0xc shown, +0x10 container widget. **H**

- `TextMenu_Ctor` 0x00492e40 (fastcall; `new(0x14)`). **H**
- `TextMenu_Init` 0x00492e80 thiscall(menuProc, itemProc), ret 8: creates a **detached full-screen container** (type 1, 0..0x7ff x 0..0x5ff) with image "lolight" (dim backdrop), proc = menuProc, userdata[0] = this, hidden. itemProc = **0x0048b2a0** gives the game's hover fade (teal 0,0xaf,0xbe → white) and click; its tail 0x00492d80 plays sfx 0xc1 and sends **`0x100001(itemId, 0)` to the container**. **H**
- `TextMenu_AddItem` 0x00492f60 thiscall(y, wchar_t* text, id, fontId, r, g, b), ret 0x1c, **returns y + height**. It measures the text (0x004929f0), creates a type-1 child **centred horizontally** and sized to the text, copies the text (userdata[1], freed on 0x14), and applies the same text-button recipe as §1.3. **H** The menus start at y = 5..10 and step by height + 5 (virtual units), so they stack from the top of the screen. **M**
- `TextMenu_Show` 0x00493130 thiscall(bool keepPopups), ret 4: Show(1), attach to the root, put it on top (depth = max + 1), and take the key focus. **H**
- `TextMenu_Hide` 0x004931b0 (fastcall): Show(0), detach, release focus, then `UI_RestoreFocus` 0x004862a0. **H**
- `TextMenu_Destroy` 0x00492e60 (fastcall): `UI_SendMsg(container,4,0,0)`, then `operator delete` 0x0067acb0(obj). **H**
- The menu proc receives `0x100001` with a = item id. Esc arrives as `0x1000b` with a = 0x1b (the game closes the menu on it, 0x0048c095). **Closing and destroying from inside the menu proc is safe**: the game does it in 0x0048b6a0 and 0x0048bd40. **H**

The Esc menus: `EscMenu_Show(int frontEnd)` 0x0048c830 builds into **`g_EscMenu` 0x007c2534** with `call 0x48c600` (front end, at **0x0048c842**) or `call 0x48c150` (park, at **0x0048c85c**, which then pauses through 0x004092a0 on 0x00786b68), then `TextMenu_Show`. Front-end ids: 9/10 go online or offline, 0xb Options, 0xc Select New Player, 0xd, 0xe Resume, 0xf Quit (jump table 0x0048c130 covers ids 9..0xf only). Park ids: 0..0x19 (table 0x0048bbc8; 5 = Options). **Any other id falls through to DefProc**, so a new item id (e.g. 0x40) needs our own handling. **H**

## 4. Text on buttons

- Font: `Font_GetFromActiveTier(id)` 0x00485a70, ids 0..12 from the active tier (text-rendering.md). Seen: **0** for the Esc-menu items, **4** for MessageBox text, **5** for main-menu signs and titles (also used by `UI_SetTitleText`), **6** for the name dialog. Text is centred in the widget's text rect (+0x2c, or the outer rect if unset) by SetTextAlign(1,1). **H** for the ids used, **M** for alignment.
- Text is **UTF-16 `wchar_t*`**. Arbitrary text works: the player names typed by the user are shown this way, and the Options rows concatenate strings at run time. ASCII letters, digits and ": x %" all appear in stock UI strings, so the F4FB fonts cover them. **H** (arbitrary strings) / **M** (glyph coverage)
- `UI_SetTitleText` 0x00485d20(parent, childId, text) uses **one static renderer at 0x007c2118**, so it is good for one title per screen only. **H**
- GetUIText(id) ids that could be useful: 6 "Options", 7 "Resume Game", 8 "Quit Game", 24 "Settings", 315 "Game Options", 319 "Screen resolution:", 332/333 " No"/" Yes", 334/335 " Off"/" On", 403 restart notice, 421 "Create New Player". (UITEXT.str decoded: `BFST`, count, offsets relative to +12, per entry `01 len 00 00` + bytes mapped through MBToUni.dat, table[b-1].) **H**

## 5. Possible blockers

- **No data-file editing is needed.** The layouts are in the exe and every widget can be created at run time. **H**
- **Art** is only what the current sprite banks have registered (list 0x00faa610, registration 0x00659a58 from 0x00488d20 / 0x00488ee0 / 0x00489060). "b_login", "lolight", "hilight", "b_okay" and "b_exit" are live in the front end because the stock screens use them. **M**
- **Focus:** key messages go only to the focus widget (0x00faa5ec). TextMenu_Show takes it; on close, give it back to `g_PlayerSelect` (or call 0x004862a0). The front-end Esc handler runs only when the front-end root has focus, so our page will not trigger it. **M**
- **Overlap:** while our page is open, hide `g_PlayerSelect` (Show(0)) or rely on the full-screen container on top (last sibling wins the hit test). The 3D front end behind gets no mouse moves, so it does not pick. **M**
- **Text width:** a TextMenu item's rect is sized to its first text. A longer value set later with SetText is clipped and off-centre. Either rebuild the page after each change (cheap, and the game rebuilds its menus every time) or create the rows with a fixed wide rect (§6). **M**
- **Layout_Exec is not re-entrant** (global index 0x00faa620). Do not call Layout_Create from inside another layout build. **H**
- **Stock resolution option:** the Options slider still writes 0x0078d8dc (0..2) and asks for a restart. Decide whether our patch ignores it or hides it (§2). **M**

## 6. Recommended design: how to build the Settings page

1. **Entry on the main menu (player-select).** Redirect the two rel32 calls to `FE_PlayerSelect_Build` (E8 at **0x005d5bec** and **0x0048bc5d**) to `HD_PlayerSelect_Build`. That wrapper calls 0x004a6580, then, if `g_PlayerSelect` (0x007cb3f8) != 0, adds one sign under "Quit Game" with our own layout blob (a copy of the Quit record, new id, y + 199):
   `0,  1, 1:u32, 0x48440001:u32, 1264,1094,2428,1241,  0x11,-1:u32, 0x12,-1:u32,  3, 1339,1123,2039,1212,  1, 0xee32be94:u32 ("b_login"),  5,  5`
   → `w = Layout_Create(g_PlayerSelect, blob, HD_SignProc)` (thiscall), then the §1.3 recipe: SetTextRenderer(new 0x1a4 + 0x491920(0)), SetFont(tier font 5), SetTextAlign(1,1), SetText(L"HD Settings"), 0x492d20(w), SetColour(0xec,0xa2,0x04,0xff). `HD_SignProc`: 0x10001 → red, 0x10002 → orange, 0x10006 → PlaySfx(0xc1) and `HD_OpenSettings()`; otherwise return `Widget_DefProc`. The panel destroys the sign with itself.
2. **Optional second entry:** wrap `call 0x48c600` (0x0048c842) and `call 0x48c150` (0x0048c85c). After the original, take the last item's y2 (walk `g_EscMenu+0x10`'s children: +0x5c, +0x54, read short +0x12) and `TextMenu_AddItem(g_EscMenu, y2+5, L"HD Settings", 0x40, 0, 0,0,0)`. Then wrap the menu procs (patch the pushes at 0x0048c645 `push 0x48bd40` and 0x0048c197 `push 0x48b6a0`) to catch `0x100001 / a==0x40`. Copy the Options case: unpause dance through 0x00786ba4 / 0x007c2518, `TextMenu_Hide`, `TextMenu_Destroy`, `g_EscMenu = 0`, then open our page. Forward everything else to the original proc.
3. **The page** = our own TextMenu: `m = new(0x14)`; `TextMenu_Ctor(m)`; `TextMenu_Init(m, HD_PageProc, 0x48b2a0)`. Then:
   - Title: `TextMenu_AddItem(m, 120, L"HD Settings", ID_TITLE, 5, …)`, ignored in the proc.
   - One row per option: build the row text into a local `wchar_t[128]` as "Resolution: 1920 x 1080", "Display: Borderless", "Frame cap: 60", "Fix X: On"…, then `y = TextMenu_AddItem(m, y+10, text, ID_ROW0+i, 0, 0,0,0)`.
   - A footer row L"(changes apply after restart)" and a row L"Back" (ID_BACK).
   - `TextMenu_Show(m, 1)`, and `g_PlayerSelect->Show(0)` if opened from the main menu.
   For stable widths, either append spaces so every row is as long as its longest value, or skip AddItem and create rows yourself: `CreateChild(m->container, 1, 1, id, 424, y, 1624, y+90)`, then the §1.3 recipe with handler 0x0048b2a0 (userdata[1] stays 0, which its destroy code accepts).
4. **HD_PageProc**(w, msg, a, b): on `0x100001` with a = row id, cycle that option in memory, rebuild the row text and `FindChild(container, a)->SetText(text)`, or just rebuild the page. On ID_BACK, or `0x1000b` with a == 0x1b: write our ini, `TextMenu_Hide(m)`, `TextMenu_Destroy(m)`, then `g_PlayerSelect->Show(1)` and `SetKeyFocus(g_PlayerSelect)` (or 0x004862a0). If anything that needs a restart changed, optionally show `MessageBox` with our own text ("Restart now?", font 4, white) whose "okay" callback calls 0x005d5db0 (front end quits cleanly) and "exit" does nothing. Return `Widget_DefProc(w,msg,a,b)` for everything else.
5. **Strings** are our own `wchar_t` literals, so no string-table edits are needed. Use GetUIText only for stock words, and copy its result at once.

## New names

```
0054e360	Game_MainStateMachine	state DAT_0087906c: 1 FE init, 2 FE run, 3 FE exit, 5-8 movies, 10 park
005d5770	FrontEnd_Ctor	0x38-byte manager, vtable 0x007029f0, stored in g_FrontEnd 0x00f82884
005d5970	FrontEnd_Init	Layout_Create(root,0x774c18,0x5d58b0); creates screen manager 0x00f85614; player-select if no player
005d5c50	FrontEnd_Update	per-frame component update (vtbl+8)
005d5cc0	FrontEnd_Render	per-frame component render (vtbl+0xc)
005d5cf0	FrontEnd_Shutdown	component shutdown, frees singletons, destroys FE root
005d5db0	FrontEnd_RequestQuit	g_FrontEnd+0x14 = 3 (Quit Game confirm)
005d5da0	FrontEnd_RegisterComponent	manager vtbl[0]
005d5700	FEComponent_BaseCtor	vtable 0x007029d0, registers with g_FrontEnd
005d58b0	FrontEnd_RootProc	widget proc of FE root 0xbf431, forwards to FrontEnd_HandleMsg
005d58e0	FrontEnd_MailButtonProc	FE child 0xbf432 ("i_mail") click
005e3c20	FEScreenMgr_Ctor	DAT_00f85614, vtable 0x00702d78
005e3dc0	FEScreenMgr_Init	creates 5 FE screens (+4,+8,+0xc,+0x10,+0x14)
005e41c0	FEScreenMgr_HandleMsg	Esc key-up -> EscMenu_Show(1) if no screen consumed it
004a6580	FE_PlayerSelect_Build	main menu: 4 player slots + Quit Game, layout 0x753c68, g_PlayerSelect 0x7cb3f8
004a62b0	PlayerSelect_RefreshSlots	slot text = player name or GetUIText(0x1a5)
004a5fc0	PlayerSelect_RootProc	msg 5 -> close; 0x14 clears g_PlayerSelect
004a6000	PlayerSelect_SlotProc	hover colours; 0x10006 load player / new player; 0x100 delete
004a61d0	PlayerSelect_QuitProc	hover red; 0x10006 -> MessageBox(GetUIText(9))
004a6290	PlayerSelect_QuitConfirmCb	calls FrontEnd_RequestQuit
004a6a50	FE_PlayerSelect_Close	destroys panel, re-shows FE root, enters island
004a6e40	FE_NewPlayerDialog_Open	player name entry dialog
0065fd0e	Layout_Create	thiscall(parent; layout, proc) ret 8: build widget tree from exe bytecode
0065fd58	Layout_Exec	layout bytecode interpreter (index 0x00faa620)
0065dd7b	Widget_CreateChild	thiscall(parent; type, flags, id, x1,y1,x2,y2) ret 0x1c
0065bcad	Widget_BaseCtor	type 1 WINDOW, 0x138 bytes, vtable 0x00705980
006688b3	Button_Ctor	type 2 BUTTON, 0x15c bytes, vtable 0x00705a50
0065d7d0	Widget_FindChild	thiscall(id): direct children only
0065d9dd	Widget_Show	thiscall(bool): flags+0x48 bit0
0065d070	Widget_SetImage	thiscall(renderer) -> +0x98
0065d1c8	Widget_SetTextRenderer	thiscall(renderer) -> +0x9c
0065c388	Widget_SetFont	thiscall(font*) -> +0x4c
0065c428	Widget_SetTextAlign	thiscall(h,v); 1,1 = centred
0065c5d5	Widget_SetColour	thiscall(r,g,b,a)
0065f645	Widget_SetText	vtbl+0x24, thiscall(wchar_t*)
0065e526	Widget_SetHandler	thiscall(proc) -> +0x114; proc = int cdecl(w,msg,a,b)
0065e6d5	Widget_SetUserData	thiscall(i,v) -> +0xa0[i]
0065e705	Widget_GetUserData	thiscall(i)
0065d7bc	Widget_GetId	+0x8c
0065d55a	Widget_GetParent	+0x60
0065e59b	Widget_SetKeyFocus	focus 0x00faa5ec, sends 0x1f/0x20
0065e5fa	Widget_ReleaseKeyFocus	clears focus if this widget has it
0065f447	Widget_AttachChild	thiscall(child, setDepth, setCentre)
0065f538	Widget_Detach	unlinks from parent
0065f16b	Widget_SetDepth	draw-order depth +0xe0, recursive
0065f66e	Widget_DefProc	thiscall(msg,a,b): calls class proc +0x118
0065f697	Widget_Dispatch	called by UI_SendMsg
00658d9f	UI_SendMsg	cdecl(w,msg,a,b); msg 4 destroys
00659bc9	UI_FindImageByName	cdecl(name): hash (h^c)*0x2f, returns new renderer
00659c42	UI_FindImageByHash	cdecl(hash)
006588b6	UI_HashName	h = (h ^ c) * mul
00491920	TextRenderer_Ctor	thiscall(0) on new(0x1a4), vtable 0x006ff530
00492d20	Widget_SetYellow	cdecl(w): colour 0xea,0xef,0x66
00485b00	GetUIText	cdecl(id) -> shared buffer 0x007ba058
00485aa0	PlaySfx	cdecl(id); 0xc1 menu click
00485d20	UI_SetTitleText	cdecl(parent, childId, text); single static renderer 0x007c2118, font 5
0047f020	MessageBox	cdecl(desc*, t1,cb1, t2,cb2, t3,cb3, flag); 1 b_okay, 2 b_exit
004862a0	UI_RestoreFocus	gives key focus back to the active HUD / FE root
00492e40	TextMenu_Ctor	0x14-byte object
00492e80	TextMenu_Init	thiscall(menuProc,itemProc): full-screen "lolight" container
00492f60	TextMenu_AddItem	thiscall(y,text,id,font,r,g,b) ret 0x1c, returns next y
00493130	TextMenu_Show	thiscall(bool): attach on top, take focus
004931b0	TextMenu_Hide	detach, release focus, UI_RestoreFocus
00492e60	TextMenu_Destroy	UI_SendMsg(container,4)
00492d80	TextMenu_ItemDefProc	0x10006 -> sfx 0xc1, 0x100001(id) to container
0048b2a0	TextMenu_ItemProcHover	hover fade teal->white, then ItemDefProc
0048c830	EscMenu_Show	(frontEnd): build into g_EscMenu 0x7c2534 and show
0048c600	EscMenu_BuildFrontEnd	online/offline, Options(0xb), Select New Player, Resume, Quit
0048c150	EscMenu_BuildPark	Load/Save/.../Options(5)/Resume/Quit
0048bd40	EscMenu_FrontEndProc	0x100001 ids 9..0xf; Esc closes
0048b6a0	EscMenu_ParkProc	0x100001 ids 0..0x19
0048bc50	SelectNewPlayer_ConfirmCb	saves player, FE_PlayerSelect_Build
004a3a30	Options_Open	Game Options screen, layout 0x752f30, g_Options 0x7cb2fc
004a2bf0	Options_Proc	0x100 buttons (-1 OK, -2 cancel), 0x800/0x801 sliders
004a2e90	Options_SliderChanged	resolution/detail/volume sliders -> config + label
004a3480	Options_ToggleChanged	toggle buttons
004a4580	Options_Close	UI_SendMsg(g_Options,4)
00423b00	Config_Backup	copies 15 dwords to +0x3c
00423ad0	Config_Restore	restores from +0x3c
00423b60	Config_SetResolution	0x0078d8dc = 0..2
00423ba0	Config_SetDetail	0x0078d8e0
00423740	Config_ComputeChangeLevel	+0x94: 1 audio, 2 restart (3D/res/videocard)
004237f0	Config_Apply	restart popup (GetUIText 0x193), writes save\config.tcf
00424bb0	Config_RestartOkCb	FE: g_FrontEnd+0x14=3; park: save and quit
00f82884	g_FrontEnd	front-end manager (data)
00f85614	g_FEScreenMgr	front-end screen manager (data)
007cb3f8	g_PlayerSelect	player-select panel root widget (data)
007c2534	g_EscMenu	TextMenu of the open Esc menu (data)
007cb2fc	g_Options	Options screen root widget (data)
00faa5d8	g_UIRoot	root widget (data)
00faa5ec	g_UIKeyFocus	key-focus widget (data)
007ba058	g_UITextBuf	shared GetUIText buffer (data)
00753c68	Layout_PlayerSelect	layout bytecode (data)
00752f30	Layout_Options	layout bytecode (data)
00774c18	Layout_FrontEndRoot	layout bytecode (data)
```
