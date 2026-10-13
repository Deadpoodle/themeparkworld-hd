// [Input] MaxTurnSpeed: a limit on the middle-button camera turn.
//
// Holding the middle button in a park turns the camera like a joystick: on
// every camera update message (0x1e) Camera_OnWidgetMsg 0x42a760 adds
//     (cursor x in UI units - 1024) * 2^-11 rad   (constant 0x74c9c0)
// to the camera's target yaw 0x790a38. That is a fixed step per message, with
// no clamp, and the message comes twice a frame: measured at 60 fps, 54 deg/s
// with the cursor 16 units from the centre, 3000 deg/s near the edge (eight
// turns a second). The keyboard turn is 0.002 rad/ms, 115 deg/s.
//
// The multiply-add at 0x42a7ff (fmul [0x74c9c0]; fadd [0x790a38]; fstp
// [0x790a38]) becomes a call here with the cursor offset. The original speed
// at 60 fps stays the speed near the centre; it is soft-limited (tanh) to the
// maximum and applied per second of real time, so it no longer depends on
// the frame rate either.
#include "camera.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

#include "hook.h"

namespace camera {
namespace {

void (*g_log)(const char*, ...);
double g_max;  // rad/s; 0 = the original step
LONGLONG g_freq, g_last;
int g_calls;
LONGLONG g_since;
double g_turned;

constexpr uintptr_t kSite = 0x42a7ff, kYaw = 0x790a38;
const uint8_t kOriginal[18] = {0xd8, 0x0d, 0xc0, 0xc9, 0x74, 0x00,   // fmul dword ptr [0x74c9c0]
                               0xd8, 0x05, 0x38, 0x0a, 0x79, 0x00,   // fadd dword ptr [0x790a38]
                               0xd9, 0x1d, 0x38, 0x0a, 0x79, 0x00};  // fstp dword ptr [0x790a38]

}  // namespace

extern "C" void __cdecl TpwhdTurnStep(float offset) {
    float& yaw = *(float*)kYaw;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = g_last ? (double)(now.QuadPart - g_last) / g_freq : 0;
    g_last = now.QuadPart;
    if (dt <= 0 || dt > 0.1) dt = 1 / 120.0;  // the first message of a turn, or after a stall
    double step = offset * (1.0 / 2048);       // the original step, rad per message
    double add = step;
    if (g_max > 0) {
        double w = step * 120;  // rad/s as now: the message comes twice a frame, 120/s at 60 fps (measured)
        add = g_max * tanh(w / g_max) * dt;
    }
    yaw = (float)(yaw + add);

    // Log the rate of messages and the turn speed once a second while turning.
    if (!g_since) g_since = now.QuadPart;
    ++g_calls;
    g_turned += add;
    double secs = (double)(now.QuadPart - g_since) / g_freq;
    if (secs >= 1) {
        if (secs < 1.5)
            g_log("camera: turn %d messages/s, %.0f deg/s (cursor offset %.0f)", (int)(g_calls / secs),
                  g_turned / secs * 57.29578, offset);
        g_since = now.QuadPart;
        g_calls = 0;
        g_turned = 0;
    }
}

namespace {
// st(0) holds the cursor offset; pass it as a cdecl float.
__attribute__((naked)) void TurnStub() {
    asm volatile(
        "sub $4, %esp\n\t"
        "fstps (%esp)\n\t"
        "call _TpwhdTurnStep\n\t"
        "add $4, %esp\n\t"
        "ret");
}
}  // namespace

bool Install(int maxDegPerSec, void (*log)(const char*, ...)) {
    g_log = log;
    g_max = maxDegPerSec > 0 ? maxDegPerSec * 3.14159265358979 / 180 : 0;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    uint8_t patch[18];
    int32_t rel = (int32_t)((uintptr_t)&TurnStub - (kSite + 5));
    patch[0] = 0xe8;
    memcpy(patch + 1, &rel, 4);
    memset(patch + 5, 0x90, sizeof patch - 5);
    bool ok = hook::Patch(kSite, kOriginal, patch, sizeof patch);
    if (maxDegPerSec > 0)
        log("camera: middle-button turn at most %d deg/s, time-based%s", maxDegPerSec, ok ? "" : " - PATCH FAILED");
    else
        log("camera: middle-button turn as the original (logged only)%s", ok ? "" : " - PATCH FAILED");
    return ok;
}

}  // namespace camera
