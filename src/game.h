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
constexpr uintptr_t IatCreateMutexA = 0x6fd134;
constexpr uintptr_t IatMoveWindow = 0x6fd31c;
constexpr uintptr_t IatGetAsyncKeyState = 0x6fd320;
constexpr uintptr_t IatGetKeyState = 0x6fd324;
constexpr uintptr_t IatGetFocus = 0x6fd328;
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

// Font_SelectResolutionTier (thiscall(width), ret 4): used to recognise the exe.
constexpr uintptr_t FontSelectTier = 0x41a5a0;
constexpr uint8_t FontSelectTierBytes[] = {0x8b, 0x44, 0x24, 0x04, 0x3d, 0x00, 0x02, 0x00, 0x00};

// Debug_SaveScreenshotTGA (the F8 handler 0x40c470 calls it): void(void), writes
// the next free Scr%05ld.tga in the working directory. Game thread only.
constexpr uintptr_t SaveScreenshotTGA = 0x550460;

// --- Globals -------------------------------------------------------------
constexpr uintptr_t ScreenWidth = 0x7a1968;
constexpr uintptr_t ScreenHeight = 0x7a1930;
constexpr uintptr_t DisplayFlags = 0x7a1a8c;

// --- Window --------------------------------------------------------------
constexpr char WindowClass[] = "Theme Park World";
constexpr uint32_t FullscreenStyle = 0x90000000;  // WS_POPUP | WS_VISIBLE

}  // namespace game
