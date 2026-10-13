// [Display] Mode=borderless (default) | fullscreen | window.
//
// Exclusive full screen through 1999 DirectDraw is fragile on current Windows:
// hitches, lost surfaces after Alt+Tab ("surface memory is gone"), focus
// trouble. Borderless runs the engine's own windowed path (display-flags bit 0
// clear: DDSCL_NORMAL, back buffer blitted to the window) in a window without
// a frame that covers the screen exactly, so nothing changes the display mode.
// When the game resolution differs from the desktop's, borderless can't fill
// the screen without scaling (not ours yet), so exclusive full screen is used.
#include "window.h"

#include <windows.h>

#include <cstring>

#include "game.h"
#include "hook.h"

namespace window {
namespace {

void (*g_log)(const char*, ...);
int g_w, g_h;
HWND g_hwnd;

decltype(&CreateWindowExA) o_CreateWindowExA;
decltype(&MoveWindow) o_MoveWindow;
decltype(&SetWindowPos) o_SetWindowPos;

bool IsGame(LPCSTR cls, HWND parent) { return !parent && HIWORD(cls) && strcmp(cls, game::WindowClass) == 0; }

HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                              HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    if (!IsGame(cls, parent)) return o_CreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    DWORD s = WS_POPUP | (style & WS_VISIBLE);
    g_hwnd = o_CreateWindowExA(ex & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE), cls, name, s, 0, 0, g_w, g_h, parent, menu,
                               inst, param);
    g_log("window: borderless %dx%d at 0,0", g_w, g_h);
    return g_hwnd;
}

// The windowed path fits the window to its client size; keep it on the screen.
BOOL WINAPI h_MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
    if (hwnd == g_hwnd) return o_MoveWindow(hwnd, 0, 0, g_w, g_h, repaint);
    return o_MoveWindow(hwnd, x, y, w, h, repaint);
}

BOOL WINAPI h_SetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
    if (hwnd == g_hwnd) {
        x = y = 0;
        w = g_w;
        h = g_h;
        flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }
    return o_SetWindowPos(hwnd, after, x, y, w, h, flags);
}

bool DesktopSize(int* w, int* h) {
    DEVMODEA dm = {};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) return false;
    *w = (int)dm.dmPelsWidth;
    *h = (int)dm.dmPelsHeight;
    return true;
}

bool PatchWindowed() {
    const uint8_t from = 0x15, to = 0x14;  // display flags bit 0 = fullscreen
    return hook::Patch(game::DisplayFlagsInitLowByte, &from, &to, 1);
}

}  // namespace

Mode ParseMode(const char* text) {
    if (_stricmp(text, "fullscreen") == 0) return Mode::Fullscreen;
    if (_stricmp(text, "window") == 0 || _stricmp(text, "windowed") == 0) return Mode::Window;
    return Mode::Borderless;
}

bool Install(Mode mode, int gameW, int gameH, bool headless, void (*log)(const char*, ...)) {
    g_log = log;
    if (headless) mode = Mode::Window;  // the test harness hides that window itself
    if (mode == Mode::Borderless) {
        int dw, dh;
        if (!gameW || !DesktopSize(&dw, &dh) || dw != gameW || dh != gameH) {
            log("window: borderless needs the game at the desktop's size (game %dx%d); using full screen", gameW,
                gameH);
            mode = Mode::Fullscreen;
        }
    }
    if (mode == Mode::Fullscreen) {
        log("window: exclusive full screen");
        return true;
    }
    if (!PatchWindowed()) {
        log("window: windowed patch FAILED");
        return false;
    }
    if (mode == Mode::Window) {
        log("window: in a window");
        return true;
    }
    // Real pixels: a DPI-scaled desktop would otherwise stretch the window.
    if (auto aware = (BOOL(WINAPI*)())GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDPIAware")) aware();
    g_w = gameW;
    g_h = gameH;
    bool ok = hook::Import(game::IatCreateWindowExA, &h_CreateWindowExA, &o_CreateWindowExA) &&
              hook::Import(game::IatMoveWindow, &h_MoveWindow, &o_MoveWindow) &&
              hook::Import(game::IatSetWindowPos, &h_SetWindowPos, &o_SetWindowPos);
    log("window: borderless %s", ok ? "on" : "HOOKS FAILED");
    return ok;
}

}  // namespace window
