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

// Present: BDEngine_Present 0x583730 (void, returns eax; returns at once when
// the window lacks focus). The main loop reaches it through this thunk.
constexpr uintptr_t PresentThunk = 0x44e550;  // jmp 0x583730
constexpr uintptr_t Present = 0x583730;

// --- Globals -------------------------------------------------------------
constexpr uintptr_t ScreenWidth = 0x7a1968;
constexpr uintptr_t ScreenHeight = 0x7a1930;
constexpr uintptr_t DisplayFlags = 0x7a1a8c;

// --- Window --------------------------------------------------------------
constexpr char WindowClass[] = "Theme Park World";
constexpr uint32_t FullscreenStyle = 0x90000000;  // WS_POPUP | WS_VISIBLE

}  // namespace game
