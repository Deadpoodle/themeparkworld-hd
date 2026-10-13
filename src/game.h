#pragma once
// Every TP.exe address the patch uses, in one place. Target: the de-SafeDisc'd
// v2.0 TP.exe (3,734,528 bytes, image base 0x400000, no relocations), which every
// modern community fix also targets. Findings and confidence: re/*.md.
#include <cstdint>

namespace game {

// --- Import table slots (IAT), from tools/tpwre.py / pefile -----------------
constexpr uintptr_t IatRegCloseKey = 0x6fd000;
constexpr uintptr_t IatRegOpenKeyA = 0x6fd004;
constexpr uintptr_t IatRegQueryValueExA = 0x6fd008;
constexpr uintptr_t IatRegCreateKeyExA = 0x6fd00c;
constexpr uintptr_t IatDirectDrawCreate = 0x6fd018;
constexpr uintptr_t IatCreateMutexA = 0x6fd134;
constexpr uintptr_t IatMoveWindow = 0x6fd31c;
constexpr uintptr_t IatGetAsyncKeyState = 0x6fd320;
constexpr uintptr_t IatGetKeyState = 0x6fd324;
constexpr uintptr_t IatGetFocus = 0x6fd328;
constexpr uintptr_t IatSwapMouseButton = 0x6fd330;  // system-wide setting; the game sets and restores it
constexpr uintptr_t IatShowWindow = 0x6fd368;
constexpr uintptr_t IatGetForegroundWindow = 0x6fd370;
constexpr uintptr_t IatMessageBoxW = 0x6fd378;
constexpr uintptr_t IatMessageBoxA = 0x6fd37c;
constexpr uintptr_t IatSetWindowPos = 0x6fd380;
constexpr uintptr_t IatCreateWindowExA = 0x6fd398;

// --- Code ----------------------------------------------------------------
// Config_InitDisplayFlags: `mov dword [0x7a1a8c], 0x000c0e15`. Bit 0 of the
// display flags = fullscreen; it also picks the window style (re/startup-and-switches.md §1).
constexpr uintptr_t DisplayFlagsInit = 0x449e1c;
constexpr uint8_t DisplayFlagsInitBytes[] = {0xc7, 0x05, 0x8c, 0x1a, 0x7a, 0x00, 0x15, 0x0e, 0x0c, 0x00};
constexpr uintptr_t DisplayFlagsInitLowByte = DisplayFlagsInit + 6;  // 0x15 -> 0x14 = windowed
// Bit 0x200000 = 32-bpp full screen (the default is 16 bpp, which current
// drivers don't offer at most sizes: SetDisplayMode fails, E_NOTIMPL).
constexpr uintptr_t DisplayFlagsInitByte2 = DisplayFlagsInit + 8;  // 0x0c -> 0x2c
// The detail presets (low/med/high.sam) all say RENDER32 0, applied at
// 0x44db4f as `and eax, 0xffdfffff`, which clears the bit again.
constexpr uintptr_t Render32Off = 0x44db4f;
constexpr uint8_t Render32OffBytes[] = {0x25, 0xff, 0xff, 0xdf, 0xff};  // and eax, 0xffdfffff
constexpr uint8_t Render32OnBytes[] = {0x0d, 0x00, 0x00, 0x20, 0x00};   // or eax, 0x200000

// Font_SelectResolutionTier (thiscall(width), ret 4): used to recognise the exe.
constexpr uintptr_t FontSelectTier = 0x41a5a0;
constexpr uint8_t FontSelectTierBytes[] = {0x8b, 0x44, 0x24, 0x04, 0x3d, 0x00, 0x02, 0x00, 0x00};

// Debug_SaveScreenshotTGA (the F8 handler 0x40c470 calls it): void(void), writes
// the next free Scr%05ld.tga in the working directory. Game thread only.
constexpr uintptr_t SaveScreenshotTGA = 0x550460;

// Resolution and fonts (re/text-rendering.md). The font loader picks its tier
// from the RESOLUTION setting, the selector from the real width; WinMain calls
// them in this order on the first idle tick:
constexpr uintptr_t CallLoadTextTables = 0x45ab6f;  // call Lang_LoadTextTables (fastcall, ecx = 0x78b530)
constexpr uintptr_t LoadTextTables = 0x419710;
constexpr uintptr_t CallVideoInitResolution = 0x45abbb;  // call Video_InitResolution (void)
constexpr uintptr_t VideoInitResolution = 0x449ec0;
// RESOLUTION 7 (2048x1536) branch of Video_InitResolution: the only slot with
// its own stores that the loader maps to the large-font tier 3.
constexpr uintptr_t Res7WidthImm = 0x44a0ed + 6;   // mov [0x7a1968], 0x800
constexpr uintptr_t Res7HeightImm = 0x44a0f7 + 6;  // mov [0x7a1930], 0x600
constexpr uintptr_t ResolutionSetting = 0x7858ec;  // Resolution.sam RESOLUTION (0 = options screen)

// Software renderer frame copy + clear, 32 bpp (0x6787f0 is the 16-bpp one):
// cdecl, 10 dword args; clears with `movq mm4, [ebp+0x2c]`, i.e. the colour
// (arg 9) plus whatever dword follows it on the stack, so at 32 bpp every
// second pixel gets that junk (black): the stripes seen above 2048 wide,
// where Direct3D fails and the game falls back to this renderer.
constexpr uintptr_t CallSoftCopyClear32 = 0x573dbd;
constexpr uintptr_t SoftCopyClear32 = 0x678c20;

// Mode list (0x5fa2f0, from the cursor set-up 0x489cb1 -> 0x5fa1e0):
// `push 0x55` = SetCooperativeLevel(EXCLUSIVE|FULLSCREEN|...) on _BullfrogLibScreen.
constexpr uintptr_t ModeListCoopPush = 0x5fa3ac;
// Movie player (0x51b010): `jne` back to retry a failed colour-fill Blt, forever.
constexpr uintptr_t MovieFillRetry1 = 0x51b207;
constexpr uintptr_t MovieFillRetry2 = 0x51b240;

// DirectX error -> message (cdecl, hr); first 9 bytes: mov eax,[esp+4]; cmp eax,0x80004005
constexpr uintptr_t MapDxError = 0x56a770;
constexpr uint8_t MapDxErrorBytes[] = {0x8b, 0x44, 0x24, 0x04, 0x3d, 0x05, 0x40, 0x00, 0x80};

// Full-screen present by Blt (DirectDraw_Flip 0x564210, path 0x564275):
// flags = ~[0x8bcbc8] & 1, meant as "wait" but 1 is DDBLT_ALPHADEST for Blt
// (DDFLIP_WAIT is 1 only for Flip). Windows' DirectDraw ignores it; dgVoodoo2
// rejects the Blt (DDERR_UNSUPPORTED), so frames never reach the screen.
constexpr uintptr_t PresentBltFlags = 0x56427e;
// not edx; mov ecx, [eax]; and edx, 1  ->  mov edx, DDBLT_WAIT; mov ecx, [eax]
constexpr uint8_t PresentBltFlagsBytes[] = {0xf7, 0xd2, 0x8b, 0x08, 0x83, 0xe2, 0x01};
constexpr uint8_t PresentBltWaitBytes[] = {0xba, 0x00, 0x00, 0x00, 0x01, 0x8b, 0x08};

// The game clock accumulates milliseconds since Windows started in doubles:
// Timer_Accumulate (0x4033a0, the scaled game time at timer+0x18) and its
// sibling 0x402f40 (raw time at +8). Direct3D leaves the x87 FPU in single
// precision, so once the total passes ~2^24 ms (4.7 h of uptime) short frames
// round away and the clock stops. Both are thiscall, no stack arguments, and
// start with these 6 relocatable bytes: sub esp,8; push esi; mov esi,ecx.
constexpr uintptr_t TimerAccumulate = 0x4033a0;
constexpr uintptr_t TimerAccumulateRaw = 0x402f40;
constexpr uint8_t TimerPrologue[] = {0x83, 0xec, 0x08, 0x56, 0x8b, 0xf1};

// Park pause (object 0x786b68): Pause 0x4092a0 (thiscall(reason, bySystem),
// ret 8), Unpause 0x409300 (thiscall, no args); paused flag at +0x1c, ready
// when +0x3c == 1. WM_ACTIVATEAPP(FALSE) in a park (no front end, 0xf82884 == 0)
// pauses (0x46b74c), and nothing unpauses on activation.
constexpr uintptr_t PauseObject = 0x786b68;
constexpr uintptr_t Unpause = 0x409300;
constexpr uintptr_t FrontEnd = 0xf82884;  // g_FrontEnd, 0 in a park

// Texture quality and memory (Display_ApplyDetailSamOptions 0x44d8f0 and the
// texture cache init 0x58d400):
// - TEXTURE32 from the detail preset is read at 0x44db6a (`mov eax, [0x7858ac]`);
//   0 (all stock presets) leaves textures in 16 bits (format 4444/1555). Forcing 1
//   (8888) corrupts every texture (stripes, no text): tested, so it stays off.
// - The cache budget = (256x256 textures the probe 0x58d1d0 could create, at
//   most 256) * bytes per pixel << 16: 32 MB at 16 bits whatever the card has
//   (log: "Amount of texture space safely allocated 2000000", hex). Big parks
//   outgrow it (white textures). The community fix: << 19 (8x).
constexpr uintptr_t Texture32Read = 0x44db6a;
constexpr uint8_t Texture32ReadBytes[] = {0xa1, 0xac, 0x58, 0x78, 0x00};  // mov eax, [0x7858ac]
constexpr uint8_t Texture32OnBytes[] = {0xb8, 0x01, 0x00, 0x00, 0x00};    // mov eax, 1
constexpr uintptr_t TextureBudgetShift = 0x58d46d;
constexpr uint8_t TextureBudgetShiftBytes[] = {0xc1, 0xe0, 0x10};  // shl eax, 16
constexpr uint8_t TextureBudgetX8Bytes[] = {0xc1, 0xe0, 0x13};     // shl eax, 19

// Present: BDEngine_Present 0x583730 (void, returns eax; returns at once when
// the window lacks focus). The main loop reaches it through this thunk.
constexpr uintptr_t PresentThunk = 0x44e550;  // jmp 0x583730
constexpr uintptr_t Present = 0x583730;

// UI input (re/mouse-input.md), cdecl, game thread only, coordinates in the
// 2048x1536 UI space. The colour cursor ignores window mouse messages
// (0x7b4b88) and reads DirectInput, so the test harness calls these directly.
constexpr uintptr_t UIMouseMove = 0x6588ef;        // (short x, short y)
constexpr uintptr_t UIMouseButtonDown = 0x658af1;  // (int button, short x, short y)
constexpr uintptr_t UIMouseButtonUp = 0x658b5b;
constexpr uintptr_t UIScreenW = 0xfaa5c4;  // int16, client size the UI maps from
constexpr uintptr_t UIScreenH = 0xfaa5c0;

// --- Globals -------------------------------------------------------------
constexpr uintptr_t ScreenWidth = 0x7a1968;
constexpr uintptr_t ScreenHeight = 0x7a1930;
constexpr uintptr_t DisplayFlags = 0x7a1a8c;

// --- Window --------------------------------------------------------------
constexpr char WindowClass[] = "Theme Park World";
constexpr uint32_t FullscreenStyle = 0x90000000;  // WS_POPUP | WS_VISIBLE

}  // namespace game
