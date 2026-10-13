// Compatibility fixes for modern Windows and DirectX wrappers. Always on: the
// stock behaviour only ever hangs.
#include "compat.h"

#include <windows.h>

#include "game.h"
#include "hook.h"

namespace compat {
namespace {

// Run the game's clock functions with the x87 FPU in double precision
// (precision control 53-bit), then put back whatever Direct3D had set.
typedef int(__fastcall* ClockFn)(void* timer, void* unused);
ClockFn o_Accumulate, o_AccumulateRaw;

inline unsigned short GetCw() {
    unsigned short cw;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    return cw;
}
inline void SetCw(unsigned short cw) { __asm__ __volatile__("fldcw %0" : : "m"(cw)); }

int __fastcall h_Accumulate(void* timer, void* unused) {
    unsigned short cw = GetCw();
    SetCw((cw & ~0x0300) | 0x0200);  // PC = 53-bit
    int r = o_Accumulate(timer, unused);
    SetCw(cw);
    return r;
}

int __fastcall h_AccumulateRaw(void* timer, void* unused) {
    unsigned short cw = GetCw();
    SetCw((cw & ~0x0300) | 0x0200);
    int r = o_AccumulateRaw(timer, unused);
    SetCw(cw);
    return r;
}

}  // namespace

void Install(const Options& opt, void (*log)(const char*, ...)) {
    // The game clock stops on a PC that has been up for more than ~5 hours:
    // see game::TimerAccumulate. Animation then froze at any frame rate above
    // ~30 fps (frames shorter than half the float step round away).
    if (opt.clock) {
    o_Accumulate = (ClockFn)hook::Detour(game::TimerAccumulate, game::TimerPrologue, sizeof game::TimerPrologue,
                                         (void*)&h_Accumulate);
    o_AccumulateRaw = (ClockFn)hook::Detour(game::TimerAccumulateRaw, game::TimerPrologue,
                                            sizeof game::TimerPrologue, (void*)&h_AccumulateRaw);
    log("compat: game clock in double precision %s", o_Accumulate && o_AccumulateRaw ? "on" : "patch FAILED");
    }

    // Cursor/input set-up lists display modes through a second DirectDraw
    // object, first taking EXCLUSIVE|FULLSCREEN (0x55) on a topmost
    // desktop-sized popup (_BullfrogLibScreen). Full screen through dgVoodoo2,
    // that steals the game's focus and the game is minimised during the intro
    // movie. Listing modes needs only DDSCL_NORMAL.
    const uint8_t from[] = {0x6a, 0x55}, to[] = {0x6a, 0x08};
    if (opt.modeList) log("compat: mode list without exclusive mode %s",
        hook::Patch(game::ModeListCoopPush, from, to, 2) ? "on" : "patch FAILED");

    // The movie player clears its surface with `while (Blt(colour fill) != DD_OK)`.
    // A lost surface (game minimised) fails forever and the game stops
    // responding. Try once more and go on instead.
    const uint8_t loop[] = {0x75, 0xe9}, once[] = {0x90, 0x90};
    if (opt.movies) {
        bool ok = hook::Patch(game::MovieFillRetry1, loop, once, 2) && hook::Patch(game::MovieFillRetry2, loop, once, 2);
        log("compat: movie clear retry loops bounded %s", ok ? "on" : "patch FAILED");
    }

    // Full-screen present by Blt asked for DDBLT_ALPHADEST (1) where it meant
    // DDBLT_WAIT; dgVoodoo2 refuses it and almost no frame is shown.
    if (opt.present) log("compat: present Blt waits instead of DDBLT_ALPHADEST %s",
        hook::Patch(game::PresentBltFlags, game::PresentBltFlagsBytes, game::PresentBltWaitBytes, sizeof game::PresentBltWaitBytes) ? "on"
                                                                                                    : "patch FAILED");
}

}  // namespace compat
