// [Display] ShowFps / F2: a frame counter in the top-left corner.
//
// The game has an FPS counter compiled in, but its text is never drawn (the
// draw call only stores it; re/music-zoom-fps.md §3), and it measures game
// time, not frames. Ours counts presented frames and draws on the back buffer
// just before the flip, for both renderers: the two render callers of
// DirectDraw_FlipBuffers 0x564210 (thiscall, ecx = g_DDrawContext, no stack
// arguments) are redirected here. The text is rendered with GDI into our own
// bitmap and copied into the locked back buffer (a surface DC from dgVoodoo2
// did not write the pixels). Verified by reading the pixels back from the
// flipped surface; the game's own F8 screenshot reads another copy.
#include "fps.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "hook.h"

namespace fps {
namespace {

void (*g_log)(const char*, ...);
volatile bool g_show;
LONGLONG g_freq, g_since;
int g_frames;
char g_text[32] = "-- fps";
HFONT g_font;

constexpr uintptr_t kFlip = 0x564210;
constexpr uintptr_t kD3DJump = 0x573896, kSoftCall = 0x573e17;

void Draw(void* ctx) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    ++g_frames;
    if (now.QuadPart - g_since >= g_freq / 2) {
        snprintf(g_text, sizeof g_text, "%.0f fps", g_frames * (double)g_freq / (now.QuadPart - g_since));
        g_since = now.QuadPart;
        g_frames = 0;
    }
    if (!g_show) return;
    void* back = *(void**)((char*)ctx + 0x14);  // IDirectDrawSurface4*
    if (!back) return;

    // Text into our own bitmap with GDI, then copied into the locked back
    // buffer: a surface DC from a DirectDraw wrapper may not write the pixels.
    int h = *(int*)0x7a1930;  // g_ScreenHeight
    int size = h / 45 > 14 ? h / 45 : 14;
    static HDC mem;
    static HBITMAP bmp;
    static unsigned* bits;
    static int bw, bh, bsize;
    if (!mem || size != bsize) {
        if (bmp) DeleteObject(bmp);
        if (g_font) DeleteObject(g_font);
        if (!mem) mem = CreateCompatibleDC(nullptr);
        bsize = size;
        bw = size * 8;
        bh = size * 3 / 2;
        BITMAPINFO bi = {};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), bw, -bh, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
        bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        SelectObject(mem, bmp);
        g_font = CreateFontA(-size, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             NONANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
        SelectObject(mem, g_font);
        SetBkMode(mem, TRANSPARENT);
    }
    if (!bits) return;
    memset(bits, 0, bw * bh * 4);
    int n = (int)strlen(g_text);
    SetTextColor(mem, RGB(1, 1, 1));  // shadow, non-zero so it is copied
    TextOutA(mem, 2, 2, g_text, n);
    SetTextColor(mem, RGB(255, 255, 80));
    TextOutA(mem, 0, 0, g_text, n);
    GdiFlush();

    typedef HRESULT(__stdcall * LockFn)(void*, RECT*, void*, DWORD, HANDLE);
    typedef HRESULT(__stdcall * UnlockFn)(void*, RECT*);
    void** vt = *(void***)back;
    unsigned char desc[124] = {};
    *(DWORD*)desc = sizeof desc;
    HRESULT hr = ((LockFn)vt[25])(back, nullptr, desc, 0x1 | 0x20 /* WAIT | WRITEONLY */, nullptr);
    static bool logged;
    if (!logged) {
        logged = true;
        g_log("fps: back buffer Lock -> %#lx (%lu bpp)", (unsigned long)hr, (unsigned long)*(DWORD*)(desc + 0x54));
    }
    if (FAILED(hr)) return;
    int sw = *(int*)(desc + 0xc), sh = *(int*)(desc + 8);
    LONG pitch = *(LONG*)(desc + 0x10);
    unsigned char* px = *(unsigned char**)(desc + 0x24);
    DWORD bpp = *(DWORD*)(desc + 0x54);
    int ox = size / 2, oy = size / 2;
    for (int y = 0; y < bh && oy + y < sh; ++y) {
        unsigned char* row = px + (oy + y) * pitch;
        for (int x = 0; x < bw && ox + x < sw; ++x) {
            unsigned c = bits[y * bw + x] & 0xffffff;
            if (!c) continue;
            if (c == 0x010101) c = 0;  // the shadow
            if (bpp == 32) ((unsigned*)row)[ox + x] = 0xff000000 | c;
            else if (bpp == 16)
                ((unsigned short*)row)[ox + x] =
                    (unsigned short)(((c >> 19) & 0x1f) << 11 | ((c >> 10) & 0x3f) << 5 | ((c >> 3) & 0x1f));
        }
    }
    ((UnlockFn)vt[32])(back, nullptr);
}

int __fastcall h_Flip(void* ctx, void* unused) {
    Draw(ctx);
    return reinterpret_cast<int(__fastcall*)(void*, void*)>(kFlip)(ctx, unused);
}

}  // namespace

void Toggle() {
    g_show = !g_show;
    if (g_log) g_log("fps: counter %s", g_show ? "shown" : "hidden");
}

bool Install(bool show, void (*log)(const char*, ...)) {
    g_log = log;
    g_show = show;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    bool ok = hook::Redirect(kD3DJump, 0xe9, kFlip, (void*)&h_Flip) &&
              hook::Redirect(kSoftCall, 0xe8, kFlip, (void*)&h_Flip);
    log("fps: counter %s (F2 toggles)%s", show ? "shown" : "hidden", ok ? "" : " - PATCH FAILED");
    return ok;
}

}  // namespace fps
