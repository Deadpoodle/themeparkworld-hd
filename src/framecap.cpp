// [Display] MaxFPS: cap the frame rate at present time.
//
// The park simulation runs on a fixed 31 ms tick, but the camera and some
// animation move once per frame (re/frame-timing.md), so above ~60 fps the game
// runs too fast. The main loop presents through the thunk at 0x44e550, which is
// redirected here: wait for the next frame slot, then present. Loading screens
// call the present function directly and are not capped.
#include "framecap.h"

#include <windows.h>

#include "game.h"
#include "hook.h"

namespace framecap {
namespace {

void (*g_log)(const char*, ...);
LONGLONG g_freq, g_interval, g_next;
HANDLE g_timer;
bool g_stats;

LONGLONG Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void WaitUntil(LONGLONG deadline) {
    for (;;) {
        LONGLONG left = deadline - Now();
        if (left <= 0) return;
        LONGLONG us = left * 1000000 / g_freq;
        if (us > 1500) {
            if (g_timer) {
                LARGE_INTEGER due;
                due.QuadPart = -(us - 500) * 10;  // relative, 100 ns units
                SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(g_timer, INFINITE);
            } else {
                Sleep((DWORD)((us - 1000) / 1000));
            }
        } else if (us > 200) {
            Sleep(0);
        } else {
            YieldProcessor();
        }
    }
}

// Frame statistics for the log: average fps and the slowest frame, per 5 s.
LONGLONG g_statStart, g_last, g_worst;
int g_frames;

void Stats(LONGLONG now) {
    if (!g_statStart) g_statStart = g_last = now;
    LONGLONG d = now - g_last;
    if (d > g_worst) g_worst = d;
    g_last = now;
    ++g_frames;
    if (now - g_statStart >= 5 * g_freq) {
        g_log("framecap: %.1f fps, slowest frame %.1f ms", g_frames * (double)g_freq / (now - g_statStart),
              g_worst * 1000.0 / g_freq);
        g_statStart = now;
        g_frames = 0;
        g_worst = 0;
    }
}

int h_Present() {
    if (g_interval) {
        LONGLONG now = Now();
        if (!g_next || now - g_next > g_interval) g_next = now;  // first frame, or fell behind: no catch-up burst
        WaitUntil(g_next);
        g_next += g_interval;
    }
    if (g_stats) Stats(Now());
    return reinterpret_cast<int (*)()>(game::Present)();
}

}  // namespace

bool Install(int maxFps, bool stats, void (*log)(const char*, ...)) {
    g_log = log;
    g_stats = stats;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    g_interval = maxFps > 0 ? g_freq / maxFps : 0;
    if (g_interval) {
        g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                         TIMER_ALL_ACCESS);
        if (!g_timer) timeBeginPeriod(1);  // older Windows: 1 ms Sleep
    }
    if (!g_interval && !stats) return true;
    bool ok = hook::Redirect(game::PresentThunk, 0xe9, game::Present, (void*)&h_Present);
    log("framecap: %s%s", maxFps > 0 ? "on" : "off (stats only)", ok ? "" : " - PATCH FAILED");
    if (ok && maxFps > 0) log("framecap: %d fps, %s timer", maxFps, g_timer ? "high-resolution" : "1 ms");
    return ok;
}

}  // namespace framecap
