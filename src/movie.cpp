// [Display] SharpMovies=1: the 320x352 movies (EA TQI, MPEG-like intra frames)
// are deblocked and enlarged 2x with a Lanczos filter on the CPU before the
// DirectDraw Blt stretches them to the screen.
//
// The player's present (TGQ_PresentFrame 0x66f600, thiscall; its context
// *this holds the destination surface at +0x14, the movie surface at +0x1c
// and the movie size at +0x64/+0x60) Blts the movie surface to the screen in
// one of several ways. Rather than follow each, the destination's Blt (vtable
// slot 5) is wrapped: a Blt whose source is the movie surface takes our
// enlarged copy and a source rectangle twice the size instead. The copy is
// made once per presented frame. Anything unexpected falls back to the
// original Blt.
//
// Measured offline on the game's own movies (ffmpeg, same filters): 2x here
// plus the GPU's bilinear stretch looks the same as a full-size Lanczos, at a
// quarter of the cost.
#include "movie.h"

#include <windows.h>
#include <ddraw.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "hook.h"

namespace movie {
namespace {

void (*g_log)(const char*, ...);
const char* g_saveDir;  // [Debug] SaveMovieFrame: write one processed frame as a TGA

constexpr uintptr_t kPresent = 0x66f600;
const uint8_t kPresentPrologue[6] = {0x81, 0xec, 0xa0, 0x00, 0x00, 0x00};  // sub esp, 0xa0

typedef void(__fastcall* PresentFn)(void* self, void*);
PresentFn o_Present;
typedef HRESULT(__stdcall* BltFn)(void*, RECT*, void*, RECT*, DWORD, void*);
BltFn o_Blt;
void** g_bltVtable;

void* g_src;         // the movie surface of the frame being presented
void* g_srcs[4];     // the movie surfaces the player alternates between
int g_nextSrc;
void* g_big;         // our 2x surface, the same interface version as g_src
int g_w, g_h;        // movie size
bool g_v4;           // surfaces are IDirectDrawSurface4 (DDSURFACEDESC2)
bool g_dirty;        // a new frame was presented since the last copy
bool g_failed;       // gave up for this surface
int g_frames;
double g_msTotal;

struct Format {
    int bits;
    DWORD r, g, b;
};

int Shift(DWORD m) {
    int s = 0;
    while (m && !(m & 1)) m >>= 1, ++s;
    return s;
}
int Bits(DWORD m) {
    int n = 0;
    for (; m; m >>= 1) n += m & 1;
    return n;
}

// Unpack one pixel to 0..255 per channel through the masks.
inline void Unpack(const Format& f, DWORD p, float* rgb) {
    const DWORD masks[3] = {f.r, f.g, f.b};
    for (int c = 0; c < 3; ++c) {
        DWORD m = masks[c];
        int s = Shift(m), n = Bits(m);
        DWORD v = (p & m) >> s;
        rgb[c] = n ? v * 255.f / ((1u << n) - 1) : 0;
    }
}

// Light deblocking across the 8-pixel block edges, on RGB: where the step at
// the edge is small (a block seam, not a real edge) and both sides are flat,
// spread it over the four pixels around the edge.
void Deblock(std::vector<float>& img, int w, int h) {
    const float kStep = 28, kFlat = 10;
    auto filter = [&](float* p1, float* p0, float* q0, float* q1) {
        for (int c = 0; c < 3; ++c) {
            float d = q0[c] - p0[c];
            if (fabsf(d) >= kStep || fabsf(p1[c] - p0[c]) >= kFlat || fabsf(q1[c] - q0[c]) >= kFlat) continue;
            p1[c] += d / 8;
            p0[c] += d * 3 / 8;
            q0[c] -= d * 3 / 8;
            q1[c] -= d / 8;
        }
    };
    for (int y = 0; y < h; ++y)
        for (int x = 8; x + 1 < w; x += 8) {
            float* r = &img[((size_t)y * w + x) * 3];
            filter(r - 6, r - 3, r, r + 3);
        }
    for (int y = 8; y + 1 < h; y += 8)
        for (int x = 0; x < w; ++x) {
            float* r = &img[((size_t)y * w + x) * 3];
            size_t row = (size_t)w * 3;
            filter(r - 2 * row, r - row, r, r + row);
        }
}

// 2x Lanczos-3: for output pixel 2i+k (k = 0, 1) the source taps are fixed.
void Upscale2(const std::vector<float>& src, int w, int h, std::vector<float>& out) {
    static float wt[2][6];
    static bool init;
    if (!init) {
        init = true;
        const double pi = 3.14159265358979323846;
        for (int k = 0; k < 2; ++k) {
            double c = (k + 0.5) / 2 - 0.5, sum = 0;
            double v[6];
            for (int t = 0; t < 6; ++t) {
                double x = c - ((int)floor(c) - 2 + t);  // taps floor(c)-2 .. floor(c)+3
                v[t] = x == 0 ? 1 : fabs(x) >= 3 ? 0 : 3 * sin(pi * x) * sin(pi * x / 3) / (pi * pi * x * x);
                sum += v[t];
            }
            for (int t = 0; t < 6; ++t) wt[k][t] = (float)(v[t] / sum);
        }
    }
    auto base = [](int k) { return (int)floor((k + 0.5) / 2 - 0.5) - 2; };
    const int b0 = base(0), b1 = base(1);
    int ow = w * 2, oh = h * 2;
    std::vector<float> mid((size_t)ow * h * 3);
    for (int y = 0; y < h; ++y) {
        const float* row = &src[(size_t)y * w * 3];
        float* o = &mid[(size_t)y * ow * 3];
        for (int x = 0; x < ow; ++x) {
            int k = x & 1, b = (x >> 1) + (k ? b1 : b0);
            float a0 = 0, a1 = 0, a2 = 0;
            for (int t = 0; t < 6; ++t) {
                int sx = b + t;
                sx = sx < 0 ? 0 : sx >= w ? w - 1 : sx;
                const float* p = row + sx * 3;
                float f = wt[k][t];
                a0 += p[0] * f, a1 += p[1] * f, a2 += p[2] * f;
            }
            o[x * 3] = a0, o[x * 3 + 1] = a1, o[x * 3 + 2] = a2;
        }
    }
    out.resize((size_t)ow * oh * 3);
    for (int y = 0; y < oh; ++y) {
        int k = y & 1, b = (y >> 1) + (k ? b1 : b0);
        const float* rows[6];
        for (int t = 0; t < 6; ++t) {
            int sy = b + t;
            sy = sy < 0 ? 0 : sy >= h ? h - 1 : sy;
            rows[t] = &mid[(size_t)sy * ow * 3];
        }
        float* o = &out[(size_t)y * ow * 3];
        for (int i = 0; i < ow * 3; ++i) {
            float a = 0;
            for (int t = 0; t < 6; ++t) a += rows[t][i] * wt[k][t];
            o[i] = a;
        }
    }
}

bool LockSurface(void* s, bool v4, void** bits, LONG* pitch, Format* f) {
    if (v4) {
        DDSURFACEDESC2 d = {};
        d.dwSize = sizeof d;
        if (FAILED(((IDirectDrawSurface4*)s)->Lock(nullptr, &d, DDLOCK_WAIT, nullptr))) return false;
        *bits = d.lpSurface, *pitch = d.lPitch;
        *f = {(int)d.ddpfPixelFormat.dwRGBBitCount, d.ddpfPixelFormat.dwRBitMask, d.ddpfPixelFormat.dwGBitMask,
              d.ddpfPixelFormat.dwBBitMask};
    } else {
        DDSURFACEDESC d = {};
        d.dwSize = sizeof d;
        if (FAILED(((IDirectDrawSurface2*)s)->Lock(nullptr, &d, DDLOCK_WAIT, nullptr))) return false;
        *bits = d.lpSurface, *pitch = d.lPitch;
        *f = {(int)d.ddpfPixelFormat.dwRGBBitCount, d.ddpfPixelFormat.dwRBitMask, d.ddpfPixelFormat.dwGBitMask,
              d.ddpfPixelFormat.dwBBitMask};
    }
    return true;
}
void UnlockSurface(void* s, bool v4) {
    if (v4) ((IDirectDrawSurface4*)s)->Unlock(nullptr);
    else ((IDirectDrawSurface2*)s)->Unlock(nullptr);
}

void SaveTga(const char* path, const std::vector<float>& img, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    unsigned char hdr[18] = {0, 0, 2};
    hdr[12] = (unsigned char)w, hdr[13] = (unsigned char)(w >> 8);
    hdr[14] = (unsigned char)h, hdr[15] = (unsigned char)(h >> 8);
    hdr[16] = 24, hdr[17] = 0x20;
    fwrite(hdr, 1, 18, f);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        unsigned char bgr[3];
        for (int c = 0; c < 3; ++c) {
            float v = img[i * 3 + 2 - c];
            bgr[c] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
        }
        fwrite(bgr, 1, 3, f);
    }
    fclose(f);
}

// Find which surface interface the game uses, and make our 2x surface in it.
bool MakeBig() {
    IUnknown* src = (IUnknown*)g_src;
    void* q = nullptr;
    g_v4 = SUCCEEDED(src->QueryInterface(IID_IDirectDrawSurface4, &q)) && q == g_src;
    if (q) ((IUnknown*)q)->Release();
    if (!g_v4) {
        q = nullptr;
        bool v2 = SUCCEEDED(src->QueryInterface(IID_IDirectDrawSurface2, &q)) && q == g_src;
        if (q) ((IUnknown*)q)->Release();
        if (!v2) {
            g_log("movie: the movie surface is neither Surface2 nor Surface4; left alone");
            return false;
        }
    }
    IUnknown* ddUnk = nullptr;
    HRESULT hr = g_v4 ? ((IDirectDrawSurface4*)g_src)->GetDDInterface((void**)&ddUnk)
                      : ((IDirectDrawSurface2*)g_src)->GetDDInterface((void**)&ddUnk);
    if (FAILED(hr) || !ddUnk) return false;
    bool ok = false;
    if (g_v4) {
        IDirectDraw4* dd = nullptr;
        DDSURFACEDESC2 sd = {};
        sd.dwSize = sizeof sd;
        if (SUCCEEDED(ddUnk->QueryInterface(IID_IDirectDraw4, (void**)&dd)) &&
            SUCCEEDED(((IDirectDrawSurface4*)g_src)->GetSurfaceDesc(&sd))) {
            DDSURFACEDESC2 d = {};
            d.dwSize = sizeof d;
            d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
            d.dwWidth = g_w * 2, d.dwHeight = g_h * 2;
            d.ddpfPixelFormat = sd.ddpfPixelFormat;
            d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | (sd.ddsCaps.dwCaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY));
            IDirectDrawSurface4* s = nullptr;
            hr = dd->CreateSurface(&d, &s, nullptr);
            ok = SUCCEEDED(hr);
            if (ok) g_big = s;
            g_log("movie: Surface4 %dx%d, %lu bpp, caps %#lx; 2x surface -> %#lx", g_w, g_h,
                  (unsigned long)sd.ddpfPixelFormat.dwRGBBitCount, (unsigned long)sd.ddsCaps.dwCaps, (unsigned long)hr);
        }
        if (dd) dd->Release();
    } else {
        IDirectDraw2* dd = nullptr;
        DDSURFACEDESC sd = {};
        sd.dwSize = sizeof sd;
        if (SUCCEEDED(ddUnk->QueryInterface(IID_IDirectDraw2, (void**)&dd)) &&
            SUCCEEDED(((IDirectDrawSurface2*)g_src)->GetSurfaceDesc(&sd))) {
            DDSURFACEDESC d = {};
            d.dwSize = sizeof d;
            d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
            d.dwWidth = g_w * 2, d.dwHeight = g_h * 2;
            d.ddpfPixelFormat = sd.ddpfPixelFormat;
            d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | (sd.ddsCaps.dwCaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY));
            IDirectDrawSurface* s1 = nullptr;
            hr = dd->CreateSurface(&d, &s1, nullptr);
            if (SUCCEEDED(hr)) {
                ok = SUCCEEDED(s1->QueryInterface(IID_IDirectDrawSurface2, &g_big));
                s1->Release();
            }
            g_log("movie: Surface2 %dx%d, %lu bpp, caps %#lx; 2x surface -> %#lx", g_w, g_h,
                  (unsigned long)sd.ddpfPixelFormat.dwRGBBitCount, (unsigned long)sd.ddsCaps.dwCaps, (unsigned long)hr);
        }
        if (dd) dd->Release();
    }
    ddUnk->Release();
    return ok;
}

// Movie surface -> deblock -> 2x -> our surface.
bool Process(void* src) {
    LARGE_INTEGER t0, t1, fq;
    QueryPerformanceCounter(&t0);
    void* bits;
    LONG pitch;
    Format f;
    if (!LockSurface(src, g_v4, &bits, &pitch, &f)) return false;
    if (f.bits != 16 && f.bits != 32) {
        UnlockSurface(src, g_v4);
        g_log("movie: %d bpp movie surface not handled", f.bits);
        return false;
    }
    std::vector<float> img((size_t)g_w * g_h * 3);
    if (f.bits == 32 && f.r == 0xff0000 && f.g == 0xff00 && f.b == 0xff) {
        for (int y = 0; y < g_h; ++y) {
            const unsigned char* row = (const unsigned char*)bits + (size_t)y * pitch;
            float* o = &img[(size_t)y * g_w * 3];
            for (int x = 0; x < g_w; ++x) o[x * 3] = row[x * 4 + 2], o[x * 3 + 1] = row[x * 4 + 1], o[x * 3 + 2] = row[x * 4];
        }
    } else {
        for (int y = 0; y < g_h; ++y) {
            const unsigned char* row = (const unsigned char*)bits + (size_t)y * pitch;
            for (int x = 0; x < g_w; ++x) {
                DWORD p = f.bits == 16 ? ((const WORD*)row)[x] : ((const DWORD*)row)[x];
                Unpack(f, p, &img[((size_t)y * g_w + x) * 3]);
            }
        }
    }
    UnlockSurface(src, g_v4);

    Deblock(img, g_w, g_h);
    std::vector<float> big;
    Upscale2(img, g_w, g_h, big);
    int bw = g_w * 2, bh = g_h * 2;

    if (!LockSurface(g_big, g_v4, &bits, &pitch, &f)) return false;
    if (f.bits == 32 && f.r == 0xff0000 && f.g == 0xff00 && f.b == 0xff) {
        for (int y = 0; y < bh; ++y) {
            DWORD* row = (DWORD*)((unsigned char*)bits + (size_t)y * pitch);
            const float* p = &big[(size_t)y * bw * 3];
            for (int x = 0; x < bw; ++x, p += 3) {
                int c[3];
                for (int k = 0; k < 3; ++k) {
                    int v = (int)(p[k] + 0.5f);
                    c[k] = v < 0 ? 0 : v > 255 ? 255 : v;
                }
                row[x] = 0xff000000u | c[0] << 16 | c[1] << 8 | c[2];
            }
        }
    } else {
        const DWORD masks[3] = {f.r, f.g, f.b};
        int sh[3], n[3];
        for (int c = 0; c < 3; ++c) sh[c] = Shift(masks[c]), n[c] = Bits(masks[c]);
        static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        for (int y = 0; y < bh; ++y) {
            unsigned char* row = (unsigned char*)bits + (size_t)y * pitch;
            for (int x = 0; x < bw; ++x) {
                const float* p = &big[((size_t)y * bw + x) * 3];
                float dith = (bayer[y & 3][x & 3] + 0.5f) / 16.f;  // 0..1 of one output step
                DWORD out = 0;
                for (int c = 0; c < 3; ++c) {
                    float maxv = (float)((1u << n[c]) - 1);
                    float v = p[c] * maxv / 255.f + (f.bits == 16 ? dith : 0.5f);
                    int q = (int)v;
                    q = q < 0 ? 0 : q > (int)maxv ? (int)maxv : q;
                    out |= (DWORD)q << sh[c];
                }
                if (f.bits == 16) ((WORD*)row)[x] = (WORD)out;
                else ((DWORD*)row)[x] = out | 0xff000000u;
            }
        }
    }
    UnlockSurface(g_big, g_v4);

    if (g_saveDir && g_frames == 60) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%smovie-frame.tga", g_saveDir);
        SaveTga(path, big, bw, bh);
        snprintf(path, sizeof path, "%smovie-frame-orig.tga", g_saveDir);
        SaveTga(path, img, g_w, g_h);  // (deblocked)
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&fq);
    g_msTotal += (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart;
    if (++g_frames == 30) g_log("movie: %d frames, %.1f ms each", g_frames, g_msTotal / g_frames);
    return true;
}

DWORD g_lastPresent;  // GetTickCount of the last movie present

// A movie surface, while a movie is playing: once it ends its surfaces are
// freed, and another surface may get the same address.
bool IsMovieSurface(void* s) {
    if (GetTickCount() - g_lastPresent > 500) return false;
    for (void* m : g_srcs)
        if (m && m == s) return true;
    return false;
}

HRESULT __stdcall h_Blt(void* self, RECT* dr, void* src, RECT* sr, DWORD flags, void* fx) {
    if (src && IsMovieSurface(src) && !g_failed) {
        if (!g_big && !MakeBig()) g_failed = true;
        if (g_big && g_dirty) {
            g_dirty = false;
            if (!Process(src)) {
                g_failed = true;
                g_log("movie: processing failed; original movie from here on");
            }
        }
        if (g_big && !g_failed) {
            RECT r = sr ? *sr : RECT{0, 0, g_w, g_h};
            r.left *= 2, r.top *= 2, r.right *= 2, r.bottom *= 2;
            HRESULT hr = o_Blt(self, dr, g_big, &r, flags, fx);
            if (SUCCEEDED(hr)) return hr;
            g_failed = true;
            g_log("movie: Blt from the 2x surface -> %#lx; original movie from here on", (unsigned long)hr);
        }
    }
    return o_Blt(self, dr, src, sr, flags, fx);
}

void __fastcall h_Present(void* self, void* unused) {
    char* ctx = *(char**)self;
    void* dest = ctx ? *(void**)(ctx + 0x14) : nullptr;
    void* src = ctx ? *(void**)(ctx + 0x1c) : nullptr;
    int w = ctx ? *(int*)(ctx + 0x64) : 0, h = ctx ? *(int*)(ctx + 0x60) : 0;
    g_lastPresent = GetTickCount();
    if (src && !IsMovieSurface(src)) {
        // The player alternates between a few movie surfaces; remember them.
        // A different size is a different movie: start over.
        if (w != g_w || h != g_h) {
            if (g_big) ((IUnknown*)g_big)->Release();
            g_big = nullptr;
            memset(g_srcs, 0, sizeof g_srcs);
            g_w = w, g_h = h;
            g_failed = g_w <= 0 || g_h <= 0 || g_w > 1024 || g_h > 1024;
            g_frames = 0;
            g_msTotal = 0;
        }
        g_srcs[g_nextSrc++ % 4] = src;
    }
    g_src = src;
    if (dest && !g_failed) {
        void** vt = *(void***)dest;
        if (vt != g_bltVtable && vt[5] != (void*)&h_Blt) {  // wrap this interface's Blt once
            DWORD old;
            if (VirtualProtect(&vt[5], sizeof(void*), PAGE_READWRITE, &old)) {
                if (!o_Blt) o_Blt = (BltFn)vt[5];
                if ((BltFn)vt[5] == o_Blt) vt[5] = (void*)&h_Blt;
                VirtualProtect(&vt[5], sizeof(void*), old, &old);
                g_bltVtable = vt;
            }
        }
    }
    g_dirty = true;
    o_Present(self, unused);
}

}  // namespace

bool Install(const char* saveDir, void (*log)(const char*, ...)) {
    g_log = log;
    g_saveDir = saveDir;
    o_Present = (PresentFn)hook::Detour(kPresent, kPresentPrologue, sizeof kPresentPrologue, (void*)&h_Present);
    log("movie: sharper movies (deblock, Lanczos 2x) %s", o_Present ? "on" : "patch FAILED");
    return o_Present != nullptr;
}

}  // namespace movie
