// Engine texture loading (re/ui-art-pipeline.md §1.2): every engine texture
// (UI, sky, park) is read by Texture_LoadImageFile 0x574bd0 from
// Texture_CreateFromFile (the call at 0x5756c5). For a .wct it returns the
// still-compressed wavelet decoder object; the pixels are decoded at upload
// by the object's inner part (0x87a254): DecodeInto (vtable slot 1, 0x564ef0,
// wavelet codecs above 0x12) or, when that returns 0, DecodeLinear (slot 2,
// 0x564e20) into a buffer that is copied. Both decode in the cache's pixel
// format g_TexFormat 0x8bd554, top row first; WCT textures carry load flag
// 0x20 (texture flag 0x40000), which flips them at upload, so engine
// textures are bottom row first like a stock TGA.
//
// [Display] SharpUI=1: the UI textures (data\ui\textures, 16..128 px) are
// decoded here in 32-bit, enlarged up to 256 px (the texture cache's largest
// slot) with a Lanczos-3 filter on premultiplied alpha, and returned as a raw
// image like the TGA path, so the GPU's bilinear stretch starts from 2-4x the
// pixels. Replacement art, if present, is used instead: data\hd\<dir>\<name>.png
// (any size up to 256; tools/dump2png.py writes the originals there by name).
// The cache fixes the UVs per texture, so a bigger image is a drop-in.
//
// [Debug] DumpTextures=1: write each texture as decoded, in 32-bit, to
// dump\textures\<dir>\<name>.tga: the clean art, exactly as the game decodes it.
#include "texhd.h"

#include <windows.h>
#include <wincodec.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hook.h"

namespace texhd {
namespace {

void (*g_log)(const char*, ...);
std::string g_gameDir;
std::string g_dumpDir;  // game folder + "dump\textures\", empty = off
bool g_sharp;
int g_dumped, g_sharpened, g_replaced;

constexpr uintptr_t kLoadImage = 0x574bd0, kCallLoadImage = 0x5756c5;
constexpr uintptr_t kTexFormat = 0x8bd554;    // int g_TexFormat (2 = 4444, 3 = 8888)
constexpr uintptr_t kLoadFlags = 0x87a24c;    // g_TexLoadFlags
constexpr uintptr_t kWaveletPart = 0x87a254;  // the returned object's inner part (decoder)
constexpr uintptr_t kExtraFrames = 0x87a264;  // frames after the first (animated textures)
constexpr uintptr_t kBytesPerPixel = 0x7016f8;  // int[4] per format
constexpr uintptr_t kAlloc = 0x579b40, kConvert = 0x575160, kGetLut = 0x5839e0, kLutOwner = 0x8bd550;
constexpr int kMaxSize = 256;  // the texture cache's largest slot

typedef void*(__cdecl* LoadImageFn)(const char* path, unsigned* w, unsigned* h, unsigned flags, int lut, int bpp);

struct Desc {  // what both decoders read: pixels, width, rows, pitch
    void* pixels;
    int width, height, pitch;
};

// One decode in the current g_TexFormat into desc; false if neither decoder
// handles this codec.
bool DecodeOnce(void* part, Desc* d) {
    typedef int(__fastcall * IntoFn)(void*, void*, Desc*, int);
    typedef void(__fastcall * LinearFn)(void*, void*, Desc*);
    void** vt = *(void***)part;
    if (((IntoFn)vt[1])(part, nullptr, d, 0)) return true;
    if (*((unsigned char*)part - 0x28) > 0x12) return false;  // DecodeInto's own codec test: it failed for real
    ((LinearFn)vt[2])(part, nullptr, d);
    return true;
}

// Decode to ARGB8888 (format 3), top row first.
bool Decode32(void* part, int w, int h, std::vector<unsigned>& out) {
    out.assign((size_t)w * h, 0);
    Desc d = {out.data(), w, h, w * 4};
    int& fmt = *(int*)kTexFormat;
    int saved = fmt;
    fmt = 3;
    bool ok = DecodeOnce(part, &d);
    fmt = saved;
    // The codec keeps format-dependent tables from its last decode; decode once
    // more in the game's own format (into a scratch buffer) so its next decode
    // isn't made with our 8888 tables.
    std::vector<unsigned> scratch((size_t)w * h, 0);
    Desc d2 = {scratch.data(), w, h, w * 4};
    DecodeOnce(part, &d2);
    return ok;
}

void WriteTga(const std::string& path, int w, int h, const std::vector<unsigned>& px) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    unsigned char hdr[18] = {0, 0, 2};
    hdr[12] = (unsigned char)w;
    hdr[13] = (unsigned char)(w >> 8);
    hdr[14] = (unsigned char)h;
    hdr[15] = (unsigned char)(h >> 8);
    hdr[16] = 32;
    hdr[17] = 0x28;  // top-left origin, 8 alpha bits
    fwrite(hdr, 1, 18, f);
    fwrite(px.data(), 4, px.size(), f);
    fclose(f);
}

// Replacement art: a PNG (or anything WIC reads), as ARGB8888 top row first.
bool LoadImageFile(const std::string& path, int* w, int* h, std::vector<unsigned>& out) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    static IWICImagingFactory* factory;
    if (!factory) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // already set up on this thread is fine
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            return false;
    }
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wpath, MAX_PATH);
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    UINT uw = 0, uh = 0;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(wpath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(conv->GetSize(&uw, &uh)) && uw && uh && uw <= kMaxSize && uh <= kMaxSize) {
        out.assign((size_t)uw * uh, 0);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, uw * 4, uw * uh * 4, (BYTE*)out.data()));
        *w = (int)uw;
        *h = (int)uh;
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (!ok && uw) g_log("texhd: %s not used (must be at most %d px a side)", path.c_str(), kMaxSize);
    return ok;
}

double Lanczos3(double x) {
    if (x == 0) return 1;
    if (x <= -3 || x >= 3) return 0;
    const double pi = 3.14159265358979323846;
    return 3 * sin(pi * x) * sin(pi * x / 3) / (pi * pi * x * x);
}

// Enlarge by an integer factor with a separable Lanczos-3 filter, on
// premultiplied alpha so transparent pixels' colour doesn't bleed into edges.
void Upscale(const std::vector<unsigned>& src, int w, int h, int s, std::vector<unsigned>& out) {
    int ow = w * s, oh = h * s;
    std::vector<float> pre((size_t)w * h * 4);
    for (size_t i = 0; i < src.size(); ++i) {
        unsigned c = src[i];
        float a = (c >> 24) / 255.f;
        pre[i * 4 + 0] = (c & 0xff) * a;
        pre[i * 4 + 1] = (c >> 8 & 0xff) * a;
        pre[i * 4 + 2] = (c >> 16 & 0xff) * a;
        pre[i * 4 + 3] = a * 255.f;
    }
    // The s output positions within a source pixel share one set of 6 weights each.
    std::vector<double> wt((size_t)s * 6);
    std::vector<int> base(s);
    for (int k = 0; k < s; ++k) {
        double c = (k + 0.5) / s - 0.5;  // offset from the source pixel's centre
        int b = (int)floor(c) - 2;
        base[k] = b;
        double sum = 0;
        for (int t = 0; t < 6; ++t) sum += wt[k * 6 + t] = Lanczos3(c - (b + t));
        for (int t = 0; t < 6; ++t) wt[k * 6 + t] /= sum;
    }
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    std::vector<float> mid((size_t)ow * h * 4);  // horizontal pass
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < ow; ++x) {
            int sx = x / s, k = x % s;
            float acc[4] = {};
            for (int t = 0; t < 6; ++t) {
                const float* p = &pre[((size_t)y * w + clampi(sx + base[k] + t, 0, w - 1)) * 4];
                float f = (float)wt[k * 6 + t];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * f;
            }
            memcpy(&mid[((size_t)y * ow + x) * 4], acc, sizeof acc);
        }
    out.assign((size_t)ow * oh, 0);
    for (int y = 0; y < oh; ++y) {  // vertical pass, then un-premultiply
        int sy = y / s, k = y % s;
        for (int x = 0; x < ow; ++x) {
            float acc[4] = {};
            for (int t = 0; t < 6; ++t) {
                const float* p = &mid[((size_t)clampi(sy + base[k] + t, 0, h - 1) * ow + x) * 4];
                float f = (float)wt[k * 6 + t];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * f;
            }
            float a = acc[3] < 0 ? 0 : acc[3] > 255 ? 255 : acc[3];
            unsigned px = (unsigned)(a + 0.5f) << 24;
            if (a >= 0.5f)
                for (int c = 0; c < 3; ++c) {
                    float v = acc[c] * 255.f / a;
                    px |= (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f) << (c * 8);
                }
            out[(size_t)y * ow + x] = px;
        }
    }
}

// The texture's place under the data folder, like "ui\textures\b_login":
// returns dir ("textures") and name ("b_login").
void SplitPath(const char* path, std::string* dir, std::string* name) {
    std::string p = path;
    size_t cut = p.find_last_of("\\/");
    *name = p.substr(cut == std::string::npos ? 0 : cut + 1);
    *dir = "misc";
    if (cut != std::string::npos && cut > 0) {
        size_t prev = p.find_last_of("\\/", cut - 1);
        size_t from = prev == std::string::npos ? 0 : prev + 1;
        *dir = p.substr(from, cut - from);
    }
    size_t dot = name->find_last_of('.');
    if (dot != std::string::npos) name->resize(dot);
}

bool IsUiTexture(const char* path) {
    std::string p = path;
    for (char& c : p) c = (char)tolower((unsigned char)c);
    return p.find("ui\\textures\\") != std::string::npos || p.find("ui/textures/") != std::string::npos;
}

// Hand the image to the engine as a raw texture, as the TGA path does: a
// buffer from the game's allocator in the cache format, bottom row first.
// For the 4-bit cache format (4444): an ordered (Bayer 4x4) dither to exact
// 4-bit steps (v = 17q, which any LUT maps back to q), instead of truncation,
// whose bands show up as streaks once the art is enlarged.
void Dither4(std::vector<unsigned>& px, int w, int h) {
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned& c = px[(size_t)y * w + x];
            int t = bayer[(y & 3) * 4 + (x & 3)] * 16 + 8;  // threshold 8..248 of 255
            unsigned out = 0;
            for (int ch = 0; ch < 4; ++ch) {
                int v = (int)(c >> (ch * 8) & 0xff) * 15;  // 0..3825: q.f in units of 1/255 step
                int q = v / 255 + (v % 255 > t * 255 / 256 ? 1 : 0);
                out |= (unsigned)(q > 15 ? 255 : q * 17) << (ch * 8);
            }
            c = out;
        }
}

void* MakeRaw(std::vector<unsigned>& px, int w, int h, unsigned alphaFlags) {
    int fmt = *(int*)kTexFormat;
    if (fmt == 2) Dither4(px, w, h);
    int bpp = ((int*)kBytesPerPixel)[fmt & 3];
    void* buf = reinterpret_cast<void*(__cdecl*)(int)>(kAlloc)(bpp * w * h + 4);
    if (!buf) return nullptr;
    int lut = reinterpret_cast<int(__fastcall*)(void*)>(kGetLut)((void*)kLutOwner);
    // Image_ConvertPixelsLUT(dst, src, w, h, lut, dstBytes, srcBytes, topRowFirst): converts
    // through the format's LUT, flips a top-first source, returns the alpha flags.
    typedef unsigned(__cdecl * ConvertFn)(void*, const void*, int, int, int, int, int, int);
    unsigned alpha = reinterpret_cast<ConvertFn>(kConvert)(buf, px.data(), w, h, lut, bpp, 4, 1);
    // Keep the original's alpha kind too (2 = blended edges): from the converter's
    // count alone an enlarged plate came out alpha-tested, with jagged edges.
    *(unsigned*)kLoadFlags = 0x300 | alpha | alphaFlags;
    if (g_sharpened + g_replaced < 4) g_log("texhd: %dx%d raw, alpha flags %#x (original %#x)", w, h, alpha, alphaFlags);
    return buf;
}

void* __cdecl h_LoadImage(const char* path, unsigned* w, unsigned* h, unsigned flags, int lut, int bpp) {
    void* r = reinterpret_cast<LoadImageFn>(kLoadImage)(path, w, h, flags, lut, bpp);
    unsigned loadFlags = *(unsigned*)kLoadFlags;
    void* part = *(void**)kWaveletPart;
    if (!r || !part || !(loadFlags & 0x20) || !*w || !*h || *w > 1024 || *h > 1024) return r;
    bool sharp = g_sharp && IsUiTexture(path) && *(int*)kExtraFrames == 0 && lut == 0 && bpp == 0;
    if (g_dumpDir.empty() && !sharp) return r;

    std::string dir, name;
    SplitPath(path, &dir, &name);
    std::vector<unsigned> px;
    bool ok = Decode32(part, (int)*w, (int)*h, px);
    if (!g_dumpDir.empty()) {
        CreateDirectoryA((g_dumpDir + dir).c_str(), nullptr);
        if (ok) WriteTga(g_dumpDir + dir + "\\" + name + ".tga", (int)*w, (int)*h, px);
        if (g_dumped++ < 5 || !ok) g_log("texhd: %s %ux%u flags %#x decode %s", path, *w, *h, loadFlags, ok ? "ok" : "FAILED");
    }
    if (!sharp) return r;

    std::vector<unsigned> big;
    int bw = 0, bh = 0;
    if (LoadImageFile(g_gameDir + "data\\hd\\" + dir + "\\" + name + ".png", &bw, &bh, big)) {
        if (g_replaced++ < 20) g_log("texhd: %s -> data\\hd\\%s\\%s.png (%dx%d)", path, dir.c_str(), name.c_str(), bw, bh);
    } else if (ok) {
        int s = 1;
        while (s < 4 && (int)*w * s * 2 <= kMaxSize && (int)*h * s * 2 <= kMaxSize) s *= 2;
        if (s == 1) return r;
        Upscale(px, (int)*w, (int)*h, s, big);
        bw = (int)*w * s;
        bh = (int)*h * s;
        ++g_sharpened;
    } else {
        return r;
    }
    void* raw = MakeRaw(big, bw, bh, loadFlags & 3);
    if (!raw) {
        *(unsigned*)kLoadFlags = loadFlags;
        return r;
    }
    // The wavelet object is no longer wanted: its scalar deleting destructor
    // (the inner part's vtable slot 0), as the loader itself frees one.
    void* inner = (char*)r + (*(int**)r)[1];
    if (inner == part) reinterpret_cast<void(__fastcall*)(void*, void*, int)>((*(void***)part)[0])(part, nullptr, 1);
    *w = (unsigned)bw;
    *h = (unsigned)bh;
    return raw;
}

}  // namespace

bool Install(const std::string& gameDir, bool sharpUi, bool dump, void (*log)(const char*, ...)) {
    g_log = log;
    g_gameDir = gameDir;
    g_sharp = sharpUi;
    if (dump) {
        g_dumpDir = gameDir + "dump\\";
        CreateDirectoryA(g_dumpDir.c_str(), nullptr);
        g_dumpDir += "textures\\";
        CreateDirectoryA(g_dumpDir.c_str(), nullptr);
    }
    if (!dump && !sharpUi) return true;
    bool ok = hook::Redirect(kCallLoadImage, 0xe8, kLoadImage, (void*)&h_LoadImage);
    log("texhd: %s%s%s", sharpUi ? "sharper UI art (data\\hd\\ replacements, else Lanczos up to 256 px)" : "",
        dump ? (" texture dump to " + g_dumpDir).c_str() : "", ok ? "" : " - PATCH FAILED");
    return ok;
}

int Sharpened() { return g_sharpened; }

}  // namespace texhd
