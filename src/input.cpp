// [Input] AltF4=1: Alt+F4 quits, as in every modern program.
//
// The game's window procedure (0x46bcb0) swallows WM_SYSKEYDOWN F4, so
// Alt+F4 did nothing. The game window is subclassed when it is created and
// Alt+F4 posts WM_CLOSE, which takes the game's own shutdown path (WM_DESTROY
// -> quit message), the same one the test harness uses.
#include "input.h"

#include <windows.h>

#include <cstring>

#include "game.h"
#include "hook.h"

namespace input {
namespace {

void (*g_log)(const char*, ...);
WNDPROC g_orig;
bool g_blockActivate;  // [Debug] BlockActivateApp=1 (experiment)
bool g_altF4;
bool g_resumeOnFocus;   // [Input] ResumeOnFocus=1
bool g_pausedByFocus;   // the park pause we let the game make when focus went
decltype(&CreateWindowExA) o_CreateWindowExA;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_blockActivate && msg == WM_ACTIVATEAPP) {
        g_log("input: WM_ACTIVATEAPP(%d) kept from the game (debug)", (int)wp);
        return 0;
    }
    if (g_resumeOnFocus && msg == WM_ACTIVATEAPP) {
        int& paused = *(int*)(game::PauseObject + 0x1c);
        bool inPark = *(void**)game::FrontEnd == nullptr;
        if (!wp) {
            bool before = paused != 0;
            LRESULT r = CallWindowProcA(g_orig, hwnd, msg, wp, lp);
            g_pausedByFocus = inPark && !before && paused;
            return r;
        }
        LRESULT r = CallWindowProcA(g_orig, hwnd, msg, wp, lp);
        if (g_pausedByFocus && inPark && paused) {
            reinterpret_cast<void(__fastcall*)(void*, void*)>(game::Unpause)((void*)game::PauseObject, nullptr);
            g_log("input: focus back, park unpaused");
        }
        g_pausedByFocus = false;
        return r;
    }
    if (g_altF4 && msg == WM_SYSKEYDOWN && wp == VK_F4) {
        g_log("input: Alt+F4, closing");
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    return CallWindowProcA(g_orig, hwnd, msg, wp, lp);
}

HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                              HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    HWND hwnd = o_CreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (hwnd && !parent && !g_orig && HIWORD(cls) && strcmp(cls, game::WindowClass) == 0)
        g_orig = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)(LONG_PTR)&WndProc);
    return hwnd;
}

}  // namespace

void Install(bool altF4, bool resumeOnFocus, bool blockActivate, void (*log)(const char*, ...)) {
    g_log = log;
    g_blockActivate = blockActivate;
    g_altF4 = altF4;
    g_resumeOnFocus = resumeOnFocus;
    if (!altF4 && !resumeOnFocus && !blockActivate) return;
    bool ok = hook::Import(game::IatCreateWindowExA, &h_CreateWindowExA, &o_CreateWindowExA);
    log("input: window hooks %s (Alt+F4 %s, resume on focus %s)", ok ? "on" : "FAILED", altF4 ? "on" : "off",
        resumeOnFocus ? "on" : "off");
}

}  // namespace input
