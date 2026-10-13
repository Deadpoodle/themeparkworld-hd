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

}  // namespace

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
    bool ok = hook::Redirect(game::CallLoadTextTables, 0xe8, game::LoadTextTables, (void*)&h_LoadTextTables) &&
              hook::Redirect(game::CallVideoInitResolution, 0xe8, game::VideoInitResolution,
                             (void*)&h_VideoInitResolution);
    log("display: %dx%d requested%s", size.w, size.h, ok ? "" : " - CALL PATCH FAILED");
    return ok;
}

}  // namespace display
