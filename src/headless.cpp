// [Debug] Headless=1: for automated test runs on a PC someone is using.
//
// The game runs windowed (the caller patches the display flags first), in a
// window that is created hidden and off-screen, can never be shown, activated
// or brought to the front, and believes it has the focus. Its audio session is
// muted (the sound code still runs). Message boxes are logged and answered.
// The single-instance mutex is skipped, so tests run beside a copy being played.
//
// Safety: full screen would change the display mode on the player's screen. The
// window style tells the two apart (re/startup-and-switches.md §1), so a
// full-screen window request ends the process before DirectDraw starts.
#include "headless.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game.h"
#include "hook.h"

namespace headless {
namespace {

void (*g_log)(const char*, ...);
HWND g_hwnd;
bool g_visible;  // Headless=2: on-screen test, the window is left alone
constexpr int kOff = -32000;

decltype(&CreateWindowExA) o_CreateWindowExA;
decltype(&ShowWindow) o_ShowWindow;
decltype(&SetWindowPos) o_SetWindowPos;
decltype(&MoveWindow) o_MoveWindow;
decltype(&GetFocus) o_GetFocus;
decltype(&GetForegroundWindow) o_GetForegroundWindow;
decltype(&MessageBoxA) o_MessageBoxA;
decltype(&MessageBoxW) o_MessageBoxW;
decltype(&CreateMutexA) o_CreateMutexA;

HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                              HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    if (g_visible) {
        HWND hwnd = o_CreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
        if (hwnd && !parent && HIWORD(cls) && strcmp(cls, game::WindowClass) == 0) g_hwnd = hwnd;
        g_log("headless (on screen): window %p %dx%d style %#lx", (void*)hwnd, w, h, (unsigned long)style);
        return hwnd;
    }
    if ((style & game::FullscreenStyle) == game::FullscreenStyle) {
        g_log("headless: the game asked for a full-screen window (style %#lx); quitting so the display mode "
              "is never changed", (unsigned long)style);
        ExitProcess(3);
    }
    HWND hwnd = o_CreateWindowExA(ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, name, style & ~WS_VISIBLE, kOff, kOff,
                                  w, h, parent, menu, inst, param);
    if (hwnd && !parent && HIWORD(cls) && strcmp(cls, game::WindowClass) == 0) g_hwnd = hwnd;
    g_log("headless: window %p \"%s\" %dx%d style %#lx, hidden off-screen", (void*)hwnd,
          HIWORD(cls) ? cls : "(atom)", w, h, (unsigned long)style);
    return hwnd;
}

BOOL WINAPI h_ShowWindow(HWND, int) { return FALSE; }

BOOL WINAPI h_SetWindowPos(HWND hwnd, HWND after, int, int, int w, int h, UINT flags) {
    flags = (flags & ~SWP_SHOWWINDOW) | SWP_NOACTIVATE | SWP_NOZORDER;
    return o_SetWindowPos(hwnd, after, kOff, kOff, w, h, flags);
}

BOOL WINAPI h_MoveWindow(HWND hwnd, int, int, int w, int h, BOOL repaint) {
    return o_MoveWindow(hwnd, kOff, kOff, w, h, repaint);
}

volatile LONG g_shotsPending;
int g_shotsTaken;

// The game presents only while its window has the focus (BDEngine_Present
// 0x583730 asks every frame), so this is also a per-frame point on the game
// thread: requested screenshots are taken here, by the game's own F8 code.
HWND WINAPI h_GetFocus() {
    if (g_shotsPending > 0 && InterlockedDecrement(&g_shotsPending) >= 0) {
        reinterpret_cast<void (*)()>(game::SaveScreenshotTGA)();
        g_log("headless: screenshot Scr%05d.tga", ++g_shotsTaken);
    }
    return g_hwnd && !g_visible ? g_hwnd : o_GetFocus();
}

// The player's real keyboard must not reach a test copy: the game reads key and
// mouse-button state directly (movie skipping, modifiers, clicks), so in this
// mode it sees only the keys the script holds down.
volatile bool g_down[256];
SHORT WINAPI h_GetAsyncKeyState(int vk) { return (vk >= 0 && vk < 256 && g_down[vk]) ? (SHORT)0x8001 : 0; }
SHORT WINAPI h_GetKeyState(int vk) { return (vk >= 0 && vk < 256 && g_down[vk]) ? (SHORT)0x8000 : 0; }
HWND WINAPI h_GetForegroundWindow() { return g_hwnd ? g_hwnd : o_GetForegroundWindow(); }

int Answer(UINT type) { return (type & 0xf) == MB_RETRYCANCEL ? IDCANCEL : IDOK; }

int WINAPI h_MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type) {
    g_log("headless: MessageBox \"%s\": %s", caption ? caption : "", text ? text : "");
    return Answer(type);
}

int WINAPI h_MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type) {
    char t[512] = "", c[128] = "";
    if (text) WideCharToMultiByte(CP_UTF8, 0, text, -1, t, sizeof t, nullptr, nullptr);
    if (caption) WideCharToMultiByte(CP_UTF8, 0, caption, -1, c, sizeof c, nullptr, nullptr);
    g_log("headless: MessageBox \"%s\": %s", c, t);
    return Answer(type);
}

// The cursor set-up (0x634220) calls SwapMouseButton(FALSE) and restores it:
// a system-wide setting a test copy must never touch. Report "not swapped".
BOOL WINAPI h_SwapMouseButton(BOOL) { return FALSE; }

// "TPWMutex" makes a second copy exit silently (0x45f666).
HANDLE WINAPI h_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name) {
    if (name && strcmp(name, "TPWMutex") == 0) {
        g_log("headless: TPWMutex made unnamed (single-instance check skipped)");
        name = nullptr;
    }
    return o_CreateMutexA(sa, owner, name);
}

// Mute this process's audio session on the default output. DirectSound and
// waveOut streams the game opens later join the same session.
void MuteSession() {
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioSessionManager* mgr = nullptr;
    ISimpleAudioVolume* vol = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&en);
    if (SUCCEEDED(hr)) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, (void**)&mgr);
    if (SUCCEEDED(hr)) hr = mgr->GetSimpleAudioVolume(nullptr, FALSE, &vol);
    if (SUCCEEDED(hr)) hr = vol->SetMute(TRUE, nullptr);
    BOOL muted = FALSE;
    if (vol) vol->GetMute(&muted);
    g_log("headless: audio session mute %s (hr %#lx)", SUCCEEDED(hr) && muted ? "on" : "FAILED", (unsigned long)hr);
    // Kept referenced for the life of the process.
}

int g_shotEvery, g_quitAfter;

// A key press as the game sees it: held in the virtual key state for 200 ms
// and posted to the window as WM_KEYDOWN/WM_KEYUP.
void TapKey(UINT vk) {
    LPARAM scan = (LPARAM)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16;
    g_down[vk] = true;
    PostMessageA(g_hwnd, WM_KEYDOWN, vk, scan | 1);
    Sleep(200);
    g_down[vk] = false;
    PostMessageA(g_hwnd, WM_KEYUP, vk, scan | 1 | (1u << 30) | (1u << 31));
}

// Mouse clicks as the game sees them: move first, press a few frames later
// (games compare the press with the previous frame's position), with the
// button held in the virtual key state too. x, y are client pixels.
void Click(int x, int y) {
    LPARAM pos = MAKELPARAM(x, y);
    PostMessageA(g_hwnd, WM_MOUSEMOVE, 0, pos);
    Sleep(150);
    PostMessageA(g_hwnd, WM_MOUSEMOVE, 0, pos);
    Sleep(100);
    g_down[VK_LBUTTON] = true;
    PostMessageA(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
    Sleep(150);
    g_down[VK_LBUTTON] = false;
    PostMessageA(g_hwnd, WM_LBUTTONUP, 0, pos);
}

void Type(const std::string& text) {
    for (char c : text) {
        PostMessageA(g_hwnd, WM_CHAR, (WPARAM)(unsigned char)c, 1);
        Sleep(60);
    }
}

// Script: "3:esc, 6:click 320 40, 8:type Kim, 9:enter, 12:shot". Steps are
// timed in seconds from start. Keys: esc, space, enter, f1..f12, a single
// letter or digit, or a virtual-key code (0x..).
enum class Kind { Key, Shot, Click, Type };
struct Step {
    DWORD ms;
    Kind kind;
    int vk, x, y;
    std::string text;
};
std::vector<Step> g_script;

int KeyCode(const std::string& k) {
    if (k == "esc") return VK_ESCAPE;
    if (k == "space") return VK_SPACE;
    if (k == "enter") return VK_RETURN;
    if (k.size() >= 2 && k[0] == 'f' && isdigit((unsigned char)k[1])) return VK_F1 + atoi(k.c_str() + 1) - 1;
    if (k.size() == 1 && isalnum((unsigned char)k[0])) return toupper((unsigned char)k[0]);
    if (k.rfind("0x", 0) == 0) return (int)strtoul(k.c_str(), nullptr, 16) & 0xff;
    return -1;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void ParseScript(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        size_t end = s.find(',', i);
        std::string item = s.substr(i, end == std::string::npos ? std::string::npos : end - i);
        i = end == std::string::npos ? s.size() : end + 1;
        size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        Step st{(DWORD)(atof(item.c_str()) * 1000), Kind::Key, 0, 0, 0, ""};
        std::string what = Trim(item.substr(colon + 1)), lower;
        for (char c : what) lower += (char)tolower((unsigned char)c);
        if (lower == "shot") {
            st.kind = Kind::Shot;
        } else if (lower.rfind("click ", 0) == 0 && sscanf(lower.c_str() + 6, "%d %d", &st.x, &st.y) == 2) {
            st.kind = Kind::Click;
        } else if (lower.rfind("type ", 0) == 0) {
            st.kind = Kind::Type;
            st.text = what.substr(5);
        } else if ((st.vk = KeyCode(lower)) < 0) {
            g_log("headless: script step \"%s\" not understood", item.c_str());
            continue;
        }
        g_script.push_back(st);
    }
}

DWORD WINAPI Worker(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // this thread only, never the game's
    MuteSession();
    DWORD start = GetTickCount(), lastShot = start;
    size_t next = 0;
    for (;;) {
        Sleep(50);
        DWORD now = GetTickCount();
        while (g_hwnd && next < g_script.size() && now - start >= g_script[next].ms) {
            const Step& s = g_script[next++];
            switch (s.kind) {
                case Kind::Key:
                    g_log("headless: script key %#x", s.vk);
                    TapKey(s.vk);
                    break;
                case Kind::Shot:
                    InterlockedIncrement(&g_shotsPending);
                    break;
                case Kind::Click:
                    g_log("headless: script click %d,%d", s.x, s.y);
                    Click(s.x, s.y);
                    break;
                case Kind::Type:
                    g_log("headless: script type \"%s\"", s.text.c_str());
                    Type(s.text);
                    break;
            }
        }
        if (g_hwnd && g_shotEvery > 0 && now - lastShot >= (DWORD)g_shotEvery * 1000) {
            lastShot = now;
            InterlockedIncrement(&g_shotsPending);
        }
        if (g_quitAfter > 0 && now - start >= (DWORD)g_quitAfter * 1000) {
            g_log("headless: quit after %d s", g_quitAfter);
            if (g_hwnd) PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
            Sleep(15000);
            g_log("headless: still running 15 s after WM_CLOSE; ending the process");
            ExitProcess(4);
        }
    }
}

}  // namespace

bool Install(const Options& opt, void (*log)(const char*, ...)) {
    g_log = log;
    g_shotEvery = opt.screenshotEvery;
    g_quitAfter = opt.quitAfter;
    g_visible = opt.visible;
    ParseScript(opt.script);
    bool ok = hook::Import(game::IatCreateWindowExA, &h_CreateWindowExA, &o_CreateWindowExA);
    decltype(&GetAsyncKeyState) unusedA;
    decltype(&GetKeyState) unusedK;
    ok &= hook::Import(game::IatGetAsyncKeyState, &h_GetAsyncKeyState, &unusedA);
    ok &= hook::Import(game::IatGetKeyState, &h_GetKeyState, &unusedK);
    decltype(&SwapMouseButton) unusedS;
    ok &= hook::Import(game::IatSwapMouseButton, &h_SwapMouseButton, &unusedS);
    if (!g_visible) {
        ok &= hook::Import(game::IatShowWindow, &h_ShowWindow, &o_ShowWindow);
        ok &= hook::Import(game::IatSetWindowPos, &h_SetWindowPos, &o_SetWindowPos);
        ok &= hook::Import(game::IatMoveWindow, &h_MoveWindow, &o_MoveWindow);
        ok &= hook::Import(game::IatGetForegroundWindow, &h_GetForegroundWindow, &o_GetForegroundWindow);
    }
    ok &= hook::Import(game::IatGetFocus, &h_GetFocus, &o_GetFocus);
    ok &= hook::Import(game::IatMessageBoxA, &h_MessageBoxA, &o_MessageBoxA);
    ok &= hook::Import(game::IatMessageBoxW, &h_MessageBoxW, &o_MessageBoxW);
    ok &= hook::Import(game::IatCreateMutexA, &h_CreateMutexA, &o_CreateMutexA);
    if (ok) CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
    log("headless: %s (screenshot every %d s, quit after %d s, %u script steps)", ok ? "on" : "HOOKS FAILED",
        opt.screenshotEvery, opt.quitAfter, (unsigned)g_script.size());
    return ok;
}

}  // namespace headless
