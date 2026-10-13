// [Display] Resolution: run the game at any size, with its text.
//
// The engine supports RESOLUTION 1..7 (512x384..2048x1536), and its menu fonts
// come in four tiers by width. The font loader picks the tier from the
// RESOLUTION setting and the selector from the real width; the community hex
// edit changes the width only, so the selector asks for a tier that was never
// loaded and all text disappears (re/text-rendering.md).
//
// Here the size goes into the RESOLUTION 7 slot and the setting is forced to 7
// just before both the loader and Video_InitResolution run, so both land on
// tier 3 (wider than 800). The stock sizes up to 800 wide use their own slots.
#include "display.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game.h"
#include "hook.h"

namespace display {
namespace {

void (*g_log)(const char*, ...);
int g_setting;  // RESOLUTION value to force, 0 = leave the game's own

int& Setting() { return *(int*)game::ResolutionSetting; }

int __fastcall h_LoadTextTables(void* self) {
    Setting() = g_setting;
    return reinterpret_cast<int(__fastcall*)(void*)>(game::LoadTextTables)(self);
}

void h_VideoInitResolution() {
    Setting() = g_setting;
    reinterpret_cast<void (*)()>(game::VideoInitResolution)();
    g_log("display: game resolution %dx%d (RESOLUTION %d)", *(int*)game::ScreenWidth, *(int*)game::ScreenHeight,
          g_setting);
}

// Pass the colour twice, so the 64-bit fill pattern is two pixels of it.
typedef void(__cdecl* SoftCopyClear32Fn)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                         DWORD);
void __cdecl h_SoftCopyClear32(DWORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7,
                               DWORD a8, DWORD colour) {
    reinterpret_cast<SoftCopyClear32Fn>(game::SoftCopyClear32)(a0, a1, a2, a3, a4, a5, a6, a7, a8, colour, colour);
}

}  // namespace

bool InstallSoftwareClearFix(void (*log)(const char*, ...)) {
    bool ok = hook::Redirect(game::CallSoftCopyClear32, 0xe8, game::SoftCopyClear32, (void*)&h_SoftCopyClear32);
    log("display: software renderer 32-bit clear fix %s", ok ? "on" : "patch FAILED");
    return ok;
}

bool Parse(const char* text, Size* out) {
    if (_stricmp(text, "original") == 0) {
        *out = {0, 0};
        return true;
    }
    if (_stricmp(text, "auto") == 0 || !*text) {
        DEVMODEA dm = {};
        dm.dmSize = sizeof dm;
        if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) return false;  // real pixels, not DPI-scaled
        *out = {(int)dm.dmPelsWidth, (int)dm.dmPelsHeight};
        if (out->w <= kMaxD3DWidth) return true;
        // The game's Direct3D (DX6 HAL) can't make a device on a render target
        // wider than 2048 (CreateDevice fails, 0x88760082) and falls back to the
        // software renderer. Take the largest display mode within that, in the
        // desktop's shape, and let the display scale it.
        Size best{0, 0}, desk = *out;
        for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
            int w = (int)dm.dmPelsWidth, h = (int)dm.dmPelsHeight;
            if (w > kMaxD3DWidth || dm.dmBitsPerPel != 32) continue;
            if (abs(w * desk.h - h * desk.w) > desk.w * desk.h / 100) continue;  // same shape, within 1%
            if (w > best.w) best = {w, h};
        }
        *out = best.w ? best : Size{1920, 1080};
        return true;
    }
    int w, h;
    if (sscanf(text, "%dx%d", &w, &h) != 2 || w < 320 || h < 240) return false;
    *out = {w, h};
    return true;
}

bool Install(Size size, void (*log)(const char*, ...)) {
    g_log = log;
    if (!size.w) {
        log("display: original resolution handling");
        return true;
    }
    // Sizes up to 800 wide: only the stock ones, in their own slots (tiers 0..2).
    if (size.w <= 800) {
        if (size.w == 512 && size.h == 384) g_setting = 1;
        else if (size.w == 640 && size.h == 480) g_setting = 2;
        else if (size.w == 800 && size.h == 600) g_setting = 3;
        else {
            log("display: %dx%d not supported (up to 800 wide only 512x384, 640x480, 800x600); original kept", size.w,
                size.h);
            return false;
        }
    } else {
        bool ok = hook::PatchU32(game::Res7WidthImm, 0x800, (uint32_t)size.w) &&
                  hook::PatchU32(game::Res7HeightImm, 0x600, (uint32_t)size.h);
        if (!ok) {
            log("display: resolution slot not as expected; original kept");
            return false;
        }
        g_setting = 7;
    }
    // Full screen at 32 bpp: 16-bpp modes at modern sizes are refused by the driver.
    const uint8_t from = 0x0c, to = 0x2c;
    bool deep = hook::Patch(game::DisplayFlagsInitByte2, &from, &to, 1) &&
                hook::Patch(game::Render32Off, game::Render32OffBytes, game::Render32OnBytes, 5);
    log("display: 32-bit colour %s", deep ? "on" : "patch FAILED");

    bool ok = hook::Redirect(game::CallLoadTextTables, 0xe8, game::LoadTextTables, (void*)&h_LoadTextTables) &&
              hook::Redirect(game::CallVideoInitResolution, 0xe8, game::VideoInitResolution,
                             (void*)&h_VideoInitResolution);
    log("display: %dx%d requested%s", size.w, size.h, ok ? "" : " - CALL PATCH FAILED");
    if (size.w > kMaxD3DWidth)
        log("display: wider than %d: Direct3D can't start there, so the game uses its software renderer", kMaxD3DWidth);
    return ok;
}

}  // namespace display
