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
#include <cmath>
#include <cstring>

#include "hook.h"
#include "lightning.h"

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

// [Debug] FlashDetect=1: sample the back buffer every frame (a 64x36 grid) and
// log frames whose average colour jumps from the frames around them, or that
// are mostly one colour (a cleared, never-drawn frame); save the first three
// inverted ones. Found the storm menu's lightning: Engine_DrawLightningFlash
// 0x57ed50 draws one full-screen white quad with blend 0xa1 (INVDESTCOLOR,
// ZERO), a one-frame negative, by design.
bool g_detect;
const char* g_dir = "";
void Detect(void* ctx) {
    void* back = *(void**)((char*)ctx + 0x14);
    if (!back) return;
    typedef HRESULT(__stdcall * LockFn)(void*, RECT*, void*, DWORD, HANDLE);
    typedef HRESULT(__stdcall * UnlockFn)(void*, RECT*);
    void** vt = *(void***)back;
    unsigned char desc[124] = {};
    *(DWORD*)desc = sizeof desc;
    if (FAILED(((LockFn)vt[25])(back, nullptr, desc, 0x1 | 0x10 /* WAIT | READONLY */, nullptr))) return;
    int sw = *(int*)(desc + 0xc), sh = *(int*)(desc + 8);
    LONG pitch = *(LONG*)(desc + 0x10);
    unsigned char* px = *(unsigned char**)(desc + 0x24);
    DWORD bpp = *(DWORD*)(desc + 0x54);
    double sum[3] = {};
    int n = 0, same = 0;
    unsigned first = 0;
    for (int gy = 0; gy < 36; ++gy)
        for (int gx = 0; gx < 64; ++gx) {
            int x = (gx * 2 + 1) * sw / 128, y = (gy * 2 + 1) * sh / 72;
            unsigned c;
            if (bpp == 32) c = ((unsigned*)(px + y * pitch))[x] & 0xffffff;
            else {
                unsigned v = ((unsigned short*)(px + y * pitch))[x];
                c = ((v >> 11) & 31) << 19 | ((v >> 5) & 63) << 10 | (v & 31) << 3;
            }
            if (!n) first = c;
            same += c == first;
            sum[0] += c >> 16 & 255, sum[1] += c >> 8 & 255, sum[2] += c & 255;
            ++n;
        }
    static double prev[3];
    static int saved, frame, logged;
    {
        double m0 = sum[0] / n, m1 = sum[1] / n, m2 = sum[2] / n;
        bool inverted = fabs(m0 + prev[0] - 255) < 40 && fabs(m1 + prev[1] - 255) < 40 && fabs(m2 + prev[2] - 255) < 40 &&
                        fabs(m0 - prev[0]) > 60;
        if (inverted && frame > 600 && saved < 3 && bpp == 32) {  // keep the frame for a look (and the one after, below)
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%sflash%d.tga", g_dir, saved++);
            if (FILE* f = fopen(path, "wb")) {
                unsigned char hdr[18] = {0, 0, 2};
                hdr[12] = (unsigned char)sw, hdr[13] = (unsigned char)(sw >> 8);
                hdr[14] = (unsigned char)sh, hdr[15] = (unsigned char)(sh >> 8);
                hdr[16] = 32, hdr[17] = 0x28;
                fwrite(hdr, 1, 18, f);
                for (int y = 0; y < sh; ++y) fwrite(px + y * pitch, 4, sw, f);
                fclose(f);
            }
        }
    }
    ((UnlockFn)vt[32])(back, nullptr);
    double m[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
    double jump = fabs(m[0] - prev[0]) + fabs(m[1] - prev[1]) + fabs(m[2] - prev[2]);
    if (frame++ > 0 && (jump > 40 || same > n * 9 / 10) && logged < 200) {
        ++logged;
        g_log("fps: frame %d colour jump %.0f: mean %.0f,%.0f,%.0f (was %.0f,%.0f,%.0f), %d%% one colour %06x",
              frame, jump, m[0], m[1], m[2], prev[0], prev[1], prev[2], same * 100 / n, first);
    }
    memcpy(prev, m, sizeof m);
}

int __fastcall h_Flip(void* ctx, void* unused) {
    lightning::Tick();
    if (g_detect) Detect(ctx);
    Draw(ctx);
    return reinterpret_cast<int(__fastcall*)(void*, void*)>(kFlip)(ctx, unused);
}

}  // namespace

void Toggle() {
    g_show = !g_show;
    if (g_log) g_log("fps: counter %s", g_show ? "shown" : "hidden");
}

bool Install(bool show, bool detect, const char* dir, void (*log)(const char*, ...)) {
    g_log = log;
    g_detect = detect;
    g_dir = dir;
    if (detect) log("fps: flash detector on (inverted frames are saved as flash<N>.tga)");
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
