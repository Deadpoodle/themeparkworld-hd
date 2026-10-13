// [Debug] DDTrace=1: log DirectDraw/Direct3D set-up calls and their results
// (surface creation, attachments, device creation), to find which one fails.
// The COM vtables are shared per interface, so each is patched once.
// Diagnosis only: with dgVoodoo2 these hooks disturb presentation (picture
// updates fell from ~97 to ~5 a second on screen), so never leave it on.
#include "ddtrace.h"

#include <windows.h>

#include "game.h"
#include "hook.h"

namespace ddtrace {
namespace {

void (*g_log)(const char*, ...);

template <typename T>
T Method(void* obj, int index) {
    return reinterpret_cast<T>((*reinterpret_cast<void***>(obj))[index]);
}

bool PatchSlot(void* obj, int index, void* fn, void** orig) {
    void** vt = *reinterpret_cast<void***>(obj);
    DWORD old;
    if (!VirtualProtect(&vt[index], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = vt[index];
    vt[index] = fn;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return true;
}

const GUID IID_DD4 = {0x9c59509a, 0x39bd, 0x11d1, {0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}};
const GUID IID_D3D3 = {0xbb223240, 0xe72b, 0x11d0, {0xa9, 0xb4, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e}};

typedef HRESULT(__stdcall* AddAttachedFn)(void*, void*);
AddAttachedFn o_AddAttached;
HRESULT __stdcall h_AddAttached(void* self, void* other) {
    HRESULT hr = o_AddAttached(self, other);
    g_log("ddtrace: Surface4::AddAttachedSurface(%p, %p) -> %#lx", self, other, (unsigned long)hr);
    return hr;
}

// Surface4::Blt (5) and Flip (11): log failures, the first 12 of each.
typedef HRESULT(__stdcall* BltFn)(void*, void*, void*, void*, DWORD, void*);
BltFn o_Blt;
HRESULT __stdcall h_Blt(void* self, void* dr, void* src, void* sr, DWORD flags, void* fx) {
    HRESULT hr = o_Blt(self, dr, src, sr, flags, fx);
    static int n;
    if (FAILED(hr) && n++ < 12)
        g_log("ddtrace: Surface4::Blt(%p <- %p, flags %#lx) -> %#lx (from %#x)", self, src, (unsigned long)flags,
              (unsigned long)hr, (unsigned)(uintptr_t)__builtin_return_address(0));
    return hr;
}
typedef HRESULT(__stdcall* FlipFn)(void*, void*, DWORD);
FlipFn o_Flip;
HRESULT __stdcall h_Flip(void* self, void* target, DWORD flags) {
    HRESULT hr = o_Flip(self, target, flags);
    static int n;
    if (FAILED(hr) && n++ < 12)
        g_log("ddtrace: Surface4::Flip(%p, flags %#lx) -> %#lx (from %#x)", self, (unsigned long)flags,
              (unsigned long)hr, (unsigned)(uintptr_t)__builtin_return_address(0));
    return hr;
}

typedef HRESULT(__stdcall* CreateSurfaceFn)(void*, void* desc, void** out, void*);
CreateSurfaceFn o_CreateSurface;
HRESULT __stdcall h_CreateSurface(void* dd, void* desc, void** out, void* outer) {
    HRESULT hr = o_CreateSurface(dd, desc, out, outer);
    const DWORD* d = (const DWORD*)desc;
    const DWORD* pf = d + 0x48 / 4;  // DDPIXELFORMAT
    g_log("ddtrace: DD4::CreateSurface flags %#lx %lux%lu caps %#lx caps2 %#lx pf flags %#lx bits %lu -> %#lx (%p)",
          d[1], d[3], d[2], d[0x68 / 4], d[0x6c / 4], pf[1], pf[3], (unsigned long)hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out && !o_AddAttached) {
        PatchSlot(*out, 3, (void*)&h_AddAttached, (void**)&o_AddAttached);
        PatchSlot(*out, 5, (void*)&h_Blt, (void**)&o_Blt);
        PatchSlot(*out, 11, (void*)&h_Flip, (void**)&o_Flip);
    }
    return hr;
}

typedef HRESULT(__stdcall* CreateDeviceFn)(void*, const GUID&, void* surface, void** out, void*);
CreateDeviceFn o_CreateDevice;
HRESULT __stdcall h_CreateDevice(void* d3d, const GUID& g, void* surface, void** out, void* outer) {
    HRESULT hr = o_CreateDevice(d3d, g, surface, out, outer);
    g_log("ddtrace: D3D3::CreateDevice({%08lx-...}, surface %p) -> %#lx", g.Data1, surface, (unsigned long)hr);
    return hr;
}

// The engine's DirectX error mapper (0x56a770, cdecl(hr) -> message): log
// each calling site and error once, so the failing call can be found.
typedef const char*(__cdecl* MapErrorFn)(HRESULT);
MapErrorFn o_MapError;
const char* __cdecl h_MapError(HRESULT hr) {
    static uintptr_t seen[64][2];
    static int n;
    uintptr_t site = (uintptr_t)__builtin_return_address(0);
    bool known = false;
    for (int i = 0; i < n; ++i) known |= seen[i][0] == site && seen[i][1] == (uintptr_t)hr;
    if (!known && n < 64) {
        seen[n][0] = site;
        seen[n][1] = (uintptr_t)hr;
        ++n;
        g_log("ddtrace: DirectX error %#lx reported at %#x", (unsigned long)hr, (unsigned)(site - 5));
    }
    return o_MapError(hr);
}

typedef HRESULT(WINAPI* DirectDrawCreateFn)(GUID*, void**, void*);
DirectDrawCreateFn o_DirectDrawCreate;

HRESULT WINAPI h_DirectDrawCreate(GUID* guid, void** dd, void* outer) {
    HRESULT hr = o_DirectDrawCreate(guid, dd, outer);
    g_log("ddtrace: DirectDrawCreate -> %#lx", (unsigned long)hr);
    if (SUCCEEDED(hr) && dd && *dd && !o_CreateSurface) {
        void* dd4 = nullptr;
        if (SUCCEEDED(Method<HRESULT(__stdcall*)(void*, const GUID&, void**)>(*dd, 0)(*dd, IID_DD4, &dd4)) && dd4) {
            PatchSlot(dd4, 6, (void*)&h_CreateSurface, (void**)&o_CreateSurface);
            Method<ULONG(__stdcall*)(void*)>(dd4, 2)(dd4);
        }
        void* d3d = nullptr;
        if (SUCCEEDED(Method<HRESULT(__stdcall*)(void*, const GUID&, void**)>(*dd, 0)(*dd, IID_D3D3, &d3d)) && d3d) {
            PatchSlot(d3d, 8, (void*)&h_CreateDevice, (void**)&o_CreateDevice);
            Method<ULONG(__stdcall*)(void*)>(d3d, 2)(d3d);
        }
        g_log("ddtrace: vtables patched (CreateSurface %s, CreateDevice %s)", o_CreateSurface ? "yes" : "no",
              o_CreateDevice ? "yes" : "no");
    }
    return hr;
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    hook::Import(game::IatDirectDrawCreate, &h_DirectDrawCreate, &o_DirectDrawCreate);
    o_MapError = (MapErrorFn)hook::Detour(game::MapDxError, game::MapDxErrorBytes, sizeof game::MapDxErrorBytes,
                                          (void*)&h_MapError);
    log("ddtrace: on (error sites %s)", o_MapError ? "logged" : "NOT logged");
}

}  // namespace ddtrace
