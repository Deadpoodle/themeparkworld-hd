# Theme Park World subsystem map

The index future work starts from: for each subsystem, the entry points, key functions and globals of
`TP.exe` (v2.0 noCD, image base 0x400000), with a pointer to the report that has the details.
Everything is from static analysis. Confidence: **H** read from the code, **M** mechanism clear but
purpose inferred, **L** a guess to test in play.

- **Names are the ones in `re/names.tsv`**, which is authoritative (its notes say which report
  established each name and record older names as "(also: X)"). Struct layouts are in `re/types.tsv`.
  Both are applied to the Ghidra project and the export `re/ghidra-export/` (see `re/deep-pass.md`). The
  older reports sometimes use a different name for the same address; grep the address.
- **Export files:** `decompiled.c` (`// ==== <entry> <name>` per function), `functions.tsv`, `xrefs.tsv`,
  `strings.tsv`, `symbols.tsv`, `types.h`, `by-class/<Class>.c`, `classes.tsv` (the 52 RTTI classes, all
  `std::`) and **`vtables.tsv`** (the 498 game vtables: class, vtable, slots, ctors/dtors, `name@address`
  per slot). Addresses are 8-digit lowercase hex without `0x`.
- **Generated names:** `Class_<vtable>::vfXX` = unnamed virtual function at slot offset XX of the class whose
  vtable is at `<vtable>` (the game has no RTTI, so classes are named after their vtable until someone
  names them with a `Class::vftable` line in names.tsv); `Class_x::Class_x` / `~Class_x` = constructor /
  destructor by heuristic. `StaticInit_<addr>` = one of the 7,155 CRT static initialisers in the table at
  0x00727004 (almost all build global `BFString` path constants), `StaticDtor_<addr>` = the atexit
  destructor it registers. Skip both when reading code.

| Subsystem | Report |
|---|---|
| Start-up, switches, registry, config files, debug features | `re/startup-and-switches.md` |
| Display, DirectDraw/Direct3D init, present | `re/display-pipeline.md`, `re/startup-and-switches.md` 1, `re/sky-2048.md`, `WIKI_LOG.md` |
| Game loop, timing | `re/frame-timing.md` (5.1 and 9.2 are corrected in names.tsv/types.tsv) |
| Front end, widgets, menus, Options | `re/frontend-menus.md` |
| Mouse, keyboard, DirectInput | `re/mouse-input.md`, `re/startup-and-switches.md` 8-9 |
| Text and fonts | `re/text-rendering.md` |
| Movies, sound, files, park simulation, camera | no report yet: this file and names.tsv only |

---

## 1. Start-up and configuration

**Order (H, startup-and-switches.md 0).** CRT entry `CRT_WinMainCRTStartup` 0x0067ee10 runs the 7,225
static initialisers, then `App_WinMain` 0x0045a960:

1. `Startup_RegistryAndMutexCheck` 0x0045f640 (`Startup_CheckRegistryLanguage` 0x0045f6b0, mutex
   `TPWMutex` into `g_InstanceMutex`). Failure exits silently.
2. Base dir = cwd (`Path_SetBaseDir` 0x005f5690 → `g_BaseDir`), `Paths_Init` 0x005c0c10,
   `Lang_SelectFromRegistry` 0x005c0d00 (LCID → `g_LanguageFolder`; default 0x809).
3. `Config_InitDisplayFlags` 0x00449df0: `g_DisplayFlags` = 0xc0e15 (label
   `Config_InitDisplayFlags_DefaultStore` 0x00449e1c is the HD patch site), `DialogTcf_Load` 0x00424a60.
4. `Config_LoadTcf` 0x00424930 into `g_Config` 0x0078d8d8 (struct `Config`).
5. Message loop; on the first idle tick (`g_AppFlags & 4` clear): `Lang_LoadTextTables` 0x00419710
   (call site `App_WinMain_CallLoadTextTables`), `Startup_CDCheck` 0x0045f7a0, `Resources_MountAll`
   0x00408470, `Video_InitResolution` 0x00449ec0, `Font_SelectResolutionTier` 0x0041a5a0,
   `Display_CreateGameWindowAndInit` 0x0044e080, then `Game_Init` 0x0054dcf0.
6. `Game_Init`: `Timer_BeginFixedStep(32)`, `CmdLine_ParseSwitches` 0x0040f000, `Keybind_InitGroups`
   0x0040cb80, `Sound_Init` 0x0051b660, volumes, `UI_InitCursorAndInput` 0x00489ca0 (at 0x0054deb3).

**Config and errors.** `Config_SetDefaults` 0x00423690, `Config_SerializeTcf` 0x004242c0 (read 0 / write 1),
`Config_SaveTcf` 0x00424820, `Config_Apply` 0x004237f0 (restart popup), `Config_ApplyVolumes` 0x00423dd0.
`ErrorReporter_Set` 0x00405710 / `ErrorReporter_Show` 0x00405750 (`g_ErrorReporter`; the box needs
`g_LanguageLoaded`). Registry: `Reg_*` 0x005f8ae0..0x005f8d40. CD: `CD_FindDiscByLabel` 0x005aa500 (noCD jmp).
Settings files: `SAM_LoadFile` 0x00401650 / `SAM_ParseFile` 0x004017a0 (`Resolution.sam`, detail presets via
`Display_LoadDetailSam` 0x0044dc60 and `Display_ApplyDetailSamOptions` 0x0044d8f0).

**Globals:** `g_AppFlags` 0x007a1a14 (2 quit, 4 initialised, 8 active), `g_DisplayFlags` 0x007a1a8c,
`g_DebugFlags` 0x0079d86c (dialog.tcf), `g_ResolutionSetting` 0x007858ec, `g_Config` 0x0078d8d8,
`g_CmdLine` 0x008787a0, `g_BwCursor` / `g_FlMouse`, `g_CheatState` 0x00fb3520.

## 2. Display: window, DirectDraw, Direct3D initialisation

- `Video_InitResolution` 0x00449ec0: RESOLUTION → `g_ScreenWidth`/`g_ScreenHeight` (0x007a1968 / 0x007a1930);
  dev sizes from flag bits when dialog.tcf is loaded. RESOLUTION 7 stores are labelled
  `Video_InitResolution_Res7Width/Height` (HD patch sites). **H**
- `Display_CreateGameWindowAndInit` 0x0044e080: window class and style (`g_DisplayFlags` bit 0), then
  `Display_SelectRenderer` 0x0044de20 (or `_Debug` 0x0044dd50) and `Display_InitRendererWithFallback`
  0x0044df20 (D3D, else software). **H**
- `DirectDraw_CreateContext` 0x005632b0 → `DirectDraw_InitDisplay` 0x00563460 (cooperative level,
  SetDisplayMode, primary/back buffer, clipper; `DirectDraw_EnumerateCallback` 0x005633e0). Context object
  `g_DDrawContext` 0x00879a24, class `DDrawContext` (vtable 0x007013c8). **H**
- `BDEngine_InitSelectedRenderer` 0x005836d0 → `Direct3D_CreateContext` 0x00572c40 → `Direct3D_Init`
  0x00573080 (IDirect3D3, Z-buffer via `D3D_EnumZBufferCallback` 0x00573660, HAL device, viewport
  `D3D_SetupViewport` 0x00572dd0), or `SoftwareRenderer_CreateContext` 0x00573960 → `SoftwareRenderer_Init`
  0x00573b90. Renderer interfaces: class `D3DRenderer` (vtable 0x00701630) and `SoftRenderer` (0x00701688);
  `g_RendererType` 0x008bd524 (1 D3D, 4 software). **H**
- Engine metrics: `BDEngine_SetDisplayMetrics` 0x00582eb0, `g_EngineFlags` 0x008bcbc8, `g_RenderWidth`/
  `g_RenderHeight` 0x008bcbd0/0x008bcbd4, `g_EngineDebugFlags` 0x008bd508, `g_EngineHwnd` 0x008bd518.
- The hidden `_BullfrogLibScreen` and second DirectDraw: `LibScreen_Create` 0x005fa1e0,
  `LibScreen_EnumModes` 0x005fa2f0 (label `LibScreen_EnumModes_CoopPush` 0x005fa3ac, HD compat patch). **M**
- Errors: `DirectX_GetErrorMessage` 0x0056a770 (the HD patch's error hook), `EngineDebug_Log` 0x00564650,
  `FallbackLog_Write` 0x005645e0, `System_LogDiagnostics` 0x005835c0.
- Limits on modern drivers (CreateDevice > 2048 wide, 16 bpp): `WIKI_LOG.md`, PLAYBOOK.

## 3. Rendering and present

- Per frame (park): `World_RenderFrame` 0x00576ec0 → `World_RenderView` 0x00576a00: clear
  (`D3D_ClearViewport` 0x00573740 through `D3DRenderer` slot 0), fog `D3D_SetFog` 0x00567ad0,
  `D3D_BeginScene` 0x00566fa0, sky (`Sky_Render` 0x005863c0, call site `World_RenderView_CallSkyRender`),
  land/objects, `D3D_EndScene` 0x00567010. **H** (sky-2048.md)
- Primitive path: `Render_BeginPrim` 0x00565240 → `Render_ClipAddIndices` 0x00565960 → `Render_EndPrim`
  0x005658c0 → `Render_FlushPrim` 0x00565590 → `D3D_DrawPrim` 0x005680b0; state word →
  `D3D_ApplyStateFlags` 0x00567620; transforms `Xform_TransformX87` 0x00579330 / `Xform_ProjectX87`
  0x00579690. **H/M**
- Present: `thunk_BDEngine_Present` 0x0044e550 (the HD patch redirects it) → `BDEngine_Present` 0x00583730
  (returns at once unless GetFocus() == window; `g_PresentCount`) → renderer slot 1: `Direct3D_PresentThunk`
  0x00573890 or `SoftwareRenderer_Present` 0x00573cc0 (`SoftRender_CopyClear32` 0x00678c20, the stripe bug)
  → `DirectDraw_FlipBuffers` 0x00564210 (labels `_BltPath` 0x00564275, `_BltFlags` 0x0056427e). **H**
- Loading and splash screens: `Splash_Display` 0x00587380, `LoadingScreen_Update` 0x00587c80,
  `UI_RenderLoadingBar` 0x00587a70, `Screen_BlitFullscreenImage` 0x00587130 (they present directly, not
  through the thunk).
- 2D text is drawn by the CPU into the locked back buffer (section 7).

## 4. Game loop and timing

- `App_WinMain` 0x0045a960: PeekMessage loop; when idle, `Online_UpdateNews` 0x005b5bf0 and
  `Game_MainStateMachine` 0x0054e360 once per iteration; no frame limiter. **H**
- `Game_MainStateMachine` (state `g_GameState` 0x0087906c): 1 front-end init (`Timer_EndFixedStep`),
  2 front-end run, 3 front-end exit, 5-8 intro movies, 9/0xc park load, 10 park. **H** (frontend-menus.md 1.1)
- Per-frame tail: `Game_FrameCallback` 0x0054e2b0, registered with `Engine_SetFrameCallback` 0x00576260:
  `Animation_UpdateModels`, `Game_FrameTimerStep` 0x00409260 (→ `Timer_AdvanceFixedStep`), more. **M**
- **Game clock `g_GameTimer` 0x00785970** (struct `GameTimer` in types.tsv; layout corrected in the deep
  pass, H): `Timer_GetGameTimeMs` 0x004031e0 (what every system reads; most callers use the thunk
  `Timer_GetGlobalGameTime` 0x00402d70) = `Timer_GetTime` 0x00402f10 + `setOffsetMs`. Fixed-step mode
  (`fixedStep`, `fixedTimeMs += stepMs` per frame): `Timer_BeginFixedStep` 0x00402ea0 (Game_Init, 32 fps;
  park state while `g_CaptureEveryFrame`), `Timer_EndFixedStep` 0x00402ed0 (front-end init; park otherwise),
  `Timer_AdvanceFixedStep` 0x00402ef0. Otherwise scaled real time: `Timer_UpdateAccumulatedTime` 0x004033a0
  (`speed` double, `Timer_GetSpeedMultiplier` 0x00403390), pause `Timer_Pause`/`Resume`/`TogglePause`
  0x00403030..0x00403080. Unscaled clock at +0x50 (`RealClock_*`, `Timer_GetRealTime` 0x00402d80). Pausing
  the game: `Game_Pause` 0x004092a0 / `Game_Resume` 0x00409300 / `Game_TogglePause` 0x00409350 on `g_Game`
  0x00786b68. Clock source: `Platform_GetTimeMs` 0x005f5f10 (QPC, timeGetTime fallback).
- **Park state 10:** `g_SimNowMs` = game time; while `g_SimLastTickMs < g_SimNowMs`: += 31 ms,
  `g_SimTickCount`++, tick functions (section 11); backlog clamped to 2000 ms (500 in the front end). Then
  interpolation `Render_InterpolateEntities` 0x00519060 and rendering. Per-frame, not per-tick:
  camera (section 12) with `g_FrameDeltaSec` 0x007b497c from `Camera_UpdateTimeDelta` 0x00473440. **H**
- Threads: `Audio_StartStreamingThread` 0x005fbc30, `IO_StartAsyncLoaderThread` 0x00632600, `Input_ThreadLoop`
  0x006324e0, `QSound_StartMixerThread` 0x0066efd0, `Net_SocketThreadProc` 0x005d2850. **M**
- Debug: `g_ShowFps` 0x007b05d0 (nothing sets it), `Debug_RenderFps` 0x0046c0d0, `Debug_Trace` 0x005da3c0
  (empty), `Debug_SaveScreenshotTGA` 0x00550460 (F8), `g_CaptureEveryFrame` 0x00878128.

## 5. Front end, widgets and menus

- Front-end manager `g_FrontEnd` 0x00f82884 (class `FrontEnd`, `FrontEnd_Ctor` 0x005d5770): `FrontEnd_Init`
  0x005d5970, `FrontEnd_Update` 0x005d5c50, `FrontEnd_Render` 0x005d5cc0, `FrontEnd_Shutdown` 0x005d5cf0,
  `FrontEnd_RequestQuit` 0x005d5db0; components `FEComponent` (vtable 0x007029d0). **H**
- 3D front end: `g_FEScreenMgr` 0x00f85614 (`FEScreenMgr_Ctor` 0x005e3c20, `FEScreenMgr_Init` 0x005e3dc0,
  `FEScreenMgr_HandleMsg` 0x005e41c0 = Esc → `EscMenu_Show`), screens `FEScreen_Slot04`..`FEScreen_Slot14`,
  ray pick `FrontEnd_PickMenuItem` 0x005dc860, fly-by world `FEFantasyWorld` (0x00702a94). **H/M**
- Main menu = player select: `FE_PlayerSelect_Build` 0x004a6580 (layout `Layout_PlayerSelect` 0x00753c68,
  `g_PlayerSelect`), procs 0x004a5fc0..0x004a6290, `FE_PlayerSelect_Close` 0x004a6a50. **H**
- Esc menus: `EscMenu_Show` 0x0048c830 → `EscMenu_BuildFrontEnd` 0x0048c600 / `EscMenu_BuildPark` 0x0048c150,
  procs `EscMenu_FrontEndProc` 0x0048bd40 / `EscMenu_ParkProc` 0x0048b6a0, built on **TextMenu**
  (`TextMenu_Ctor` 0x00492e40, `_Init` 0x00492e80, `_AddItem` 0x00492f60, `_Show` 0x00493130, `_Hide`
  0x004931b0, `_Destroy` 0x00492e60; struct `TextMenu`). **H** The recommended base for an HD Settings page.
- Options screen: `Options_Open` 0x004a3a30, `Options_Proc` 0x004a2bf0, `Options_SliderChanged` 0x004a2e90,
  `Options_ToggleChanged` 0x004a3480; `Config_Backup`/`Restore`/`Apply`. **H**
- Widget library (class `Widget`, vtable 0x00705980, struct in types.tsv; `Button` 0x00705a50; `TextRenderer`
  0x006ff530): `Layout_Create` 0x0065fd0e / `Layout_Exec` 0x0065fd58, `Widget_CreateChild` 0x0065dd7b,
  `Widget_FindChild`, `Widget_Show`, `Widget_SetText` (vtbl+0x24), `Widget_SetFont`, `Widget_SetHandler`,
  `Widget_DefProc` 0x0065f66e, `UI_SendMsg` 0x00658d9f (4 destroy), `MessageBox` 0x0047f020, `GetUIText`
  0x00485b00, `UI_SetTitleText` 0x00485d20, `PlaySfx` 0x00485aa0. Message ids: frontend-menus.md 1.4. **H**
- Roots and focus: `g_UIRoot` 0x00faa5d8 (2048x1536, `UI_CreateRoot` 0x006584df), `g_UIKeyFocus` 0x00faa5ec,
  `UI_RestoreFocus` 0x004862a0. Art by name hash: `UI_FindImageByName` 0x00659bc9, `UI_HashName` 0x006588b6,
  `UI_RegisterSpriteBank` 0x00659a58.

## 6. Input

- Window: `WndProc` 0x0046bcb0 → `WndProc_HandleInput` 0x0046b600 (mouse scaled to 2048x1536 with
  `g_UIScreenW`/`g_UIScreenH`; keys via `MapVirtualKeyA`; skipped entirely for the mouse when
  `UI_IsDInputCursor`). **H**
- Widget layer (cdecl, game thread): `UI_MouseMove` 0x006588ef, `UI_MouseButtonDown` 0x00658af1,
  `UI_MouseButtonUp` 0x00658b5b, `UI_MouseWheel` 0x00658bcb, `UI_KeyDown`/`UI_KeyUp`/`UI_Char`
  0x00658c38..0x00658c96, queue `UI_PostWidgetMsg` 0x00658ccc / `UI_DispatchQueue` 0x00658e42, hit test
  `UI_HitTest` 0x0065db25; gate `g_UIInputEnabled` 0x0077c488 (`UI_EnableInput`/`UI_DisableInput`). **H**
- Per frame: `UI_Update` 0x0048a580 (dispatches the queue; in DirectInput mode polls the cursor tracker
  and `UI_DInputButtonCallback` 0x00486c70). **H**
- DirectInput: `UI_InitCursorAndInput` 0x00489ca0 → `InputMgr_CreateKeyboard`/`Mouse` 0x005f55c0/0x005f5600,
  `UI_InitColourCursor` 0x0047e7d0 (`g_DInputCursor` 0x007b4b88 = 1); `g_InputManager` 0x00fa6e40 (struct
  `InputManager`), classes `DIMouse` (0x00703cf8), `DIKeyboard` (0x00703d38), `CursorTracker` (0x00703cc8;
  `CursorTracker_OnAxis` 0x00634360), `DIDevice_*` 0x00633a00..0x0063e970, `InputMgr_OnEngineMsg` 0x00618610
  (cooperative levels). **H**
- Keybinds: `Keybind_InitGroups` 0x0040cb80, `Keybind_DispatchKeyDown` 0x0040c900 / `KeyUp` 0x0040c990,
  tables `Keybinds_System`..`Keybinds_Shortcuts` 0x00747f38..0x00748378, handlers `Key_*` / `Debug_Quick*`
  / `Cheat_*` (cheat group gated by `Cheat_IsEnabled` 0x00405560). **H/M**
- Frontend 3D menu input: `FrontEnd_HandleMsg` 0x005d5dd0 (needs a second move, 0x10003). **H**

## 7. Text and fonts

- Bitmap UI fonts (F4FB, 4-bit alpha): loaded for one tier by `Lang_LoadTextTables` 0x00419710 (tier from
  RESOLUTION), selected by `Font_SelectResolutionTier` 0x0041a5a0 (tier from the real width, `g_FontTier`
  0x0078b534), fetched by `Font_GetFromActiveTier` 0x00485a70 (`g_FontTierTable` 0x0078b54c, 13 fonts x 4
  tiers), loaded by `Font_LoadF4FBResource` 0x006b0680, drawn by `Font_BlitGlyph4BitAlpha` 0x006b15f0 into
  the locked back buffer. Unload: `Language_UnloadFonts` 0x0041a5f0. **H** (text-rendering.md)
- Strings: `GetUIText` 0x00485b00 (shared buffer `g_UITextBuf` 0x007ba058), widget text through
  `TextRenderer` (`TextRenderer_Ctor` 0x00491920). UTF-16 throughout.
- 3D ride signs (TrueType + Uniscribe into a texture): `RideSign_CreateForRide` 0x004682b0,
  `RideSign_GenerateTexture` 0x00467a80, `SignTexture_RenderDualLineText` 0x005ecb40,
  `Uniscribe_RenderTextLineToDIB` 0x005eab70, `Font_RegisterTTFResource` 0x005ec680 (`g_FontResourceList`).

## 8. Movies (TGQ)

- Intros: `Movie_PlayIntro1` 0x0054df20 (`bf.tgq`), `Movie_PlayIntro2` 0x0054e070 (a random ride movie), run
  from `Game_MainStateMachine` states 5-8; Esc/Space skip polls `GetAsyncKeyState`. **H**
- `Video_PlayTGQFrame` 0x0051b010 **starts** a movie (despite the name): colour-fills the surfaces (retry
  loops at labels `Video_PlayTGQFrame_FillRetry1/2`, HD compat patch), `TGQ_Open` 0x0066f4e0 into
  `g_MovieHandle` 0x00802bb4, `TGQ_SetVolume` 0x0066f520. Then `Movie_IsFinished` 0x0051b300 (polls
  `TGQ_IsPlaying` 0x0066f540), `Movie_Stop` 0x0051b350, `Movie_Shutdown` 0x0051b3e0; the decoder runs on its
  own thread (`TGQ_PlayerThreadProc` 0x0066f090, L; MPEG/IDCT tables in the `idct_dat`/`lbmpeg_d` sections,
  used by 0x006ef7f0, 0x006f02e0, 0x006f0a50). **M**
- Movie volume: `g_Config.movieVolume`, `Movie_SetVolume` 0x0051b3c0. Files: `data\movies` (local first, then
  the CD root; startup-and-switches.md 4).

## 9. Sound (QMixer / DirectSound)

- `Sound_Init` 0x0051b660 (from `Game_Init`, enable = `g_DisplayFlags & 0x400`), `Sound_Shutdown`
  0x0051b7c0; state `g_SoundEnabled` 0x00802bc8 / `g_SoundInitialised` 0x00802bd4. **H/M**
- Commands: the game talks to the sound system by posting small one-slot command objects
  (`Sound_PostCommand` 0x006b5bd0; vtables 0x00700b2c..0x00700bbc, `Class_00700bxx::vf00` are the
  commands): `Sound_PauseAll` 0x0051bcf0 / `Sound_ResumeAll` 0x0051bd30 (`SoundCmdPause` / `SoundCmdResume`),
  `Sound_SetSfxVolume` 0x0051bba0, `Sound_SetMusicVolume` 0x0051bb80, `Sound_SetSpeechVolume` 0x0051bbc0,
  `Sound_SetListener` 0x0051c1d0 (camera position each frame). **M**
- Driver: class `QMixerDriver` (vtable 0x00711920, ctor 0x006d1e30) wraps the QMixer imports:
  `QMixerDriver::Init` 0x006d2110, `::PlayWave` 0x006d2380, `::StopChannel` .. `::SetListenerOrientation`.
  `DirectSoundCreate` is called from 0x00672910 and 0x006c4090. QSound mixer thread
  `QSound_StartMixerThread` 0x0066efd0, streaming `Audio_StartStreamingThread` 0x005fbc30. **H** (imports) /
  **M** (roles)
- Data: `data\global\sound.wad`, `speech.wad`, `music.wad`, `sound.sam` (static path strings);
  scripted effects "RSSE" (0x005573d0, 0x0055a3e0). Save module "Sound".

## 10. Files, WADs, paths, saves

- Paths: `Path_SetBaseDir` 0x005f5690, `Paths_Init` 0x005c0c10, `Path_GetExeDirFromCmdLine` 0x005f5950,
  `CD_SetCDRootPath` 0x005aa650; lookup `File_FindLocalThenCD` 0x0041cc20 (`File_FindLocal` 0x0041cc40,
  `File_FindOnCD` 0x0041cd50). Global path strings are `BFString` objects (`BFString_Ctor` 0x005f60b0, vtable
  0x00703090) built by the `StaticInit_` functions. **H/M**
- Mounts: `Resources_MountAll` 0x00408470 (data.wad, ui.wad, esprites, generic, language, 2DMap, Postcard,
  Lobby, sound/speech/music wads, `data\movies`). Mount classes: `FileMount` (abstract, 0x00703898),
  `DirMount` (folder, 0x007038d0), `WadMount` (archive, 0x00703908; `WadMount_ReadDirectory` 0x0061a250 checks
  'DWFB'); stream class `Win32File` (0x00703e30, CreateFileA/ReadFile). Open: `FileSys_Open` 0x005c4d50
  (stream vtable: +0 delete, +4 close, +8 read, +0xc write, +0x14 size). `WAD_ExtractFileToDisk` 0x005c5010.
  Async loader `IO_StartAsyncLoaderThread` 0x00632600. **M**
- Settings files: `.sam` (`SAM_LoadFile` / `SAM_ParseFile`), `save\config.tcf` (`Config_LoadTcf`/`SaveTcf`),
  `dialog.tcf` (`DialogTcf_Load`). **H**
- Savegames (`.TPWS`): `SaveGame_SaveModules` 0x004164c0 / `SaveGame_LoadModules` 0x00415270 write and read
  the modules UI, AdvisorScoring, Cheat, Sound, Advisor, Coasters, Camera, RSSE, FlyingRides, TrackRides,
  RideSystem, GameSystem, VanillaTime, Clock, MessageCentre, Particles, Scripts, World, GActionRec, through
  `Stream_WriteChecked` 0x005f5e50 / `Stream_ReadChecked` 0x005f5e20. QuickSave/QuickLoad: `Debug_QuickSave`
  0x0040bf90 / `Debug_QuickLoad` 0x0040c040, command-line `.TPWS` via `Level_ApplyQuickLoad` 0x00407e00. **H**

## 11. Park simulation

Mostly unexplored. What is known (H, from `Game_MainStateMachine` state 10):

| Rate | Functions |
|---|---|
| every 31 ms tick | `Sim_TickSimulation` 0x00520130 (also ticks in the front end; walks a table of 0x140-byte objects at 0x0080cefc, so probably not the whole simulation), 0x00546c80, 0x005516b0 |
| every 2nd tick | 0x0055abf0, 0x00475360 |
| every 8th tick | 0x00516380, 0x0055a470 |
| every 32nd tick | 0x00519510, 0x004c81e0, 0x004c8d30 (values clamped to 0x59 / 100, passed to 0x0051e790 / 0x0051e7b0), 0x0055ab50 |

- Each of the first few is timed with GetTickCount into 8-entry history arrays (0x008780c8.., 0x008780e8..),
  which feed the dead profiler line (`g_ProfilerText`).
- Between ticks: `Render_InterpolateEntities` 0x00519060 → `Render_InterpolateThing` 0x00546280 with
  fraction (now - last) / 31. Model animation per frame: `Animation_UpdateModels` 0x0044e410
  (`g_FrameDeltaSec`).
- Rides and signs: `RideSign_*` (section 7); coaster track building around 0x00433300..0x00442f40 (pylon and
  brace log strings). Save modules (section 10) list the simulation's parts.
- Next step: a report on the park objects (the `World`, rides, peeps) starting from the save modules.

## 12. Camera

- Per frame in the park, not per tick: `Camera_UpdateTimeDelta` 0x00473440 (`g_FrameDeltaSec`,
  `g_FrameTimeMs`), `Camera_UpdatePosition` 0x00443340 (frame-locked movement: the high-fps speed-up in
  frame-timing.md 6), `Camera_Update` 0x0042b1c0 (reads `g_AppFlags & 8`, edge scroll?), 0x00435a30. **H/M**
- Keys: camera keybind group (`Key_CameraMoveA`/`B` 0x0042a500/0x0042a520), `Key_RotateCameraCW`/`CCW`,
  `Key_ZoomIn`/`Out`/`Reset`. **M**
- State: `g_CameraEye` 0x007909c0 (also the sound listener position), `g_CameraFlags` 0x00790ab0 (bits 0x16
  change how the eye and listener are updated in `Game_FrameCallback` and the park loop); strings
  `gui_CameraFlags`, `gf_CameraZoom` (used by 0x0042cec0); save module "Camera". **M**
