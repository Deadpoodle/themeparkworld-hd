// Engine texture loading (re/ui-art-pipeline.md §1.2): every engine texture
// (UI, sky, park) is read by Texture_LoadImageFile 0x574bd0 from
// Texture_CreateFromFile (the call at 0x5756c5). For a .wct it returns the
// still-compressed wavelet decoder object; the pixels are decoded at upload
// by the object's DecodeInto (vtable slot 1 of its inner part, 0x564ef0) in
// the cache's pixel format g_TexFormat 0x8bd554.
//
// [Debug] DumpTextures=1: decode each texture once more, in 32-bit (format 3,
// ARGB8888), and write it to dump\textures\<dir>\<name>.tga: the clean art,
// exactly as the game decodes it.
#include "texhd.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hook.h"

namespace texhd {
namespace {

void (*g_log)(const char*, ...);
std::string g_dumpDir;  // game folder + "dump\textures\", empty = off
int g_dumped;

constexpr uintptr_t kLoadImage = 0x574bd0, kCallLoadImage = 0x5756c5;
constexpr uintptr_t kTexFormat = 0x8bd554;    // int g_TexFormat (2 = 4444, 3 = 8888)
constexpr uintptr_t kLoadFlags = 0x87a24c;    // g_TexLoadFlags
constexpr uintptr_t kWaveletPart = 0x87a254;  // the returned object's inner part (decoder)

typedef void*(__cdecl* LoadImageFn)(const char* path, unsigned* w, unsigned* h, unsigned flags, int lut, int bpp);

struct Desc {  // what DecodeInto reads: pixels, width, rows, pitch
    void* pixels;
    int width, height, pitch;
};

bool Decode32(void* part, int w, int h, std::vector<unsigned>& out) {
    out.assign((size_t)w * h, 0);
    Desc d = {out.data(), w, h, w * 4};
    int& fmt = *(int*)kTexFormat;
    int saved = fmt;
    fmt = 3;
    typedef int(__fastcall * DecodeFn)(void*, void*, Desc*, int);
    int r = ((DecodeFn)(*(void***)part)[1])(part, nullptr, &d, 0);
    fmt = saved;
    // The codec keeps format-dependent tables from its last decode; decode once
    // more in the game's own format (into a scratch buffer) so its next decode
    // isn't made with our 8888 tables.
    std::vector<unsigned> scratch((size_t)w * h, 0);
    Desc d2 = {scratch.data(), w, h, w * 4};
    ((DecodeFn)(*(void***)part)[1])(part, nullptr, &d2, 0);
    return r != 0;
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

void* __cdecl h_LoadImage(const char* path, unsigned* w, unsigned* h, unsigned flags, int lut, int bpp) {
    void* r = reinterpret_cast<LoadImageFn>(kLoadImage)(path, w, h, flags, lut, bpp);
    unsigned loadFlags = *(unsigned*)kLoadFlags;
    void* part = *(void**)kWaveletPart;
    if (r && part && (loadFlags & 0x20) && !g_dumpDir.empty() && *w && *h && *w <= 1024 && *h <= 1024) {
        std::vector<unsigned> px;
        bool ok = Decode32(part, (int)*w, (int)*h, px);
        // path is like ...\data\ui.wad\textures\b_login.wct or a loose file: keep the last two parts
        std::string p = path, name = p.substr(p.find_last_of("\\/") + 1), dir = "misc";
        size_t cut = p.find_last_of("\\/");
        if (cut != std::string::npos && cut > 0) {
            size_t prev = p.find_last_of("\\/", cut - 1);
            dir = p.substr(prev == std::string::npos ? 0 : prev + 1, cut - (prev == std::string::npos ? 0 : prev + 1));
        }
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name.resize(dot);
        CreateDirectoryA((g_dumpDir + dir).c_str(), nullptr);
        if (ok) WriteTga(g_dumpDir + dir + "\\" + name + ".tga", (int)*w, (int)*h, px);
        if (g_dumped++ < 5) g_log("texhd: %s %ux%u flags %#x decode %s", path, *w, *h, loadFlags, ok ? "ok" : "FAILED");
    }
    return r;
}

}  // namespace

bool Install(const std::string& gameDir, bool dump, void (*log)(const char*, ...)) {
    g_log = log;
    if (dump) {
        g_dumpDir = gameDir + "dump\\";
        CreateDirectoryA(g_dumpDir.c_str(), nullptr);
        g_dumpDir += "textures\\";
        CreateDirectoryA(g_dumpDir.c_str(), nullptr);
    }
    if (!dump) return true;
    bool ok = hook::Redirect(kCallLoadImage, 0xe8, kLoadImage, (void*)&h_LoadImage);
    log("texhd: texture dump to %s %s", g_dumpDir.c_str(), ok ? "on" : "patch FAILED");
    return ok;
}

}  // namespace texhd
