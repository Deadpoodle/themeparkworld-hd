// [Display] LightningFlash: the lightning flash in the storm scene of the main
// menu (and the park weather).
//
// A strike calls Engine_RequestLightningFlash 0x57dc70, which sets engine+4
// (engine = *(0x8bcbac)). On the next frame Engine_DrawLightningFlash 0x57ed50
// clears it and draws one full-screen white quad with render state 0x9a1,
// whose low byte is the blend: 0xa1 = (INVDESTCOLOR, ZERO). The frame comes out
// as a photographic negative: the original effect.
//   soft: blend 0x52 (SRCALPHA, ONE) and a 3/8-opaque white: the frame is
//         brightened instead of inverted.
//   off:  the function only clears the request and returns.
//
// [Debug] ForceLightning=N: request a flash every N seconds wherever the engine
// draws, so the effect can be checked without waiting for the storm scene.
#include "lightning.h"

#include <windows.h>

#include <cstring>

#include "hook.h"

namespace lightning {
namespace {

constexpr uintptr_t kDraw = 0x57ed50, kStateImm = 0x57ed75, kColourImm = 0x57ed91, kEngine = 0x8bcbac;
DWORD g_forceMs, g_lastForce;

}  // namespace

Mode ParseMode(const char* s) {
    if (_stricmp(s, "soft") == 0) return Mode::Soft;
    if (_stricmp(s, "off") == 0) return Mode::Off;
    return Mode::Original;
}

bool Install(Mode mode, int forceEverySec, void (*log)(const char*, ...)) {
    g_forceMs = forceEverySec > 0 ? (DWORD)forceEverySec * 1000 : 0;
    bool ok = true;
    if (mode == Mode::Soft) {
        // mov dword ptr [esp+0x14], 0x9a1  ->  0x952
        const uint8_t s0[8] = {0xc7, 0x44, 0x24, 0x14, 0xa1, 0x09, 0x00, 0x00};
        const uint8_t s1[8] = {0xc7, 0x44, 0x24, 0x14, 0x52, 0x09, 0x00, 0x00};
        // mov dword ptr [esp+0x10], 0xffffffff  ->  0x60ffffff
        const uint8_t c0[8] = {0xc7, 0x44, 0x24, 0x10, 0xff, 0xff, 0xff, 0xff};
        const uint8_t c1[8] = {0xc7, 0x44, 0x24, 0x10, 0xff, 0xff, 0xff, 0x60};
        ok = hook::Patch(kStateImm, s0, s1, 8) && hook::Patch(kColourImm, c0, c1, 8);
    } else if (mode == Mode::Off) {
        // sub esp,0x6c; xor eax,eax; lea..  ->  xor eax,eax; mov [ecx+4],eax; mov al,1; ret
        // (1 = drawn: the caller, the bolt draw 0x57ee30, gives up on 0)
        const uint8_t f0[8] = {0x83, 0xec, 0x6c, 0x33, 0xc0, 0x8d, 0x54, 0x24};
        const uint8_t f1[8] = {0x31, 0xc0, 0x89, 0x41, 0x04, 0xb0, 0x01, 0xc3};
        ok = hook::Patch(kDraw, f0, f1, 8);
    }
    const char* name = mode == Mode::Soft ? "soft (brightens)" : mode == Mode::Off ? "off" : "original (negative frame)";
    log("lightning: flash %s%s", name, ok ? "" : " - PATCH FAILED");
    if (g_forceMs) log("lightning: forced every %d s (debug)", forceEverySec);
    return ok;
}

void Tick() {
    if (!g_forceMs) return;
    DWORD now = GetTickCount();
    if (now - g_lastForce < g_forceMs) return;
    g_lastForce = now;
    if (char* engine = *(char**)kEngine) *(int*)(engine + 4) = 1;
}

}  // namespace lightning
