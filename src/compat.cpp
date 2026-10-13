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

typedef HRESULT(WINAPI* SSOutFn)(void*, int, int, UINT, const RECT*, int, int, BOOL);
SSOutFn o_ScriptStringOut;
HRESULT WINAPI h_ScriptStringOut(void* ssa, int x, int y, UINT opts, const RECT* clip, int selMin, int selMax,
                                 BOOL disabled) {
    if (!clip) opts &= ~ETO_CLIPPED;  // clipping to nothing: rejected by current Uniscribe
    return o_ScriptStringOut(ssa, x, y, opts, clip, selMin, selMax, disabled);
}

typedef int(__stdcall* MatchFormatFn)(void*, const unsigned*, int);
int __stdcall h_MatchTextureFormat(void* ctx, const unsigned* pf, int z) {
    if (!(pf[1] & 0x40)) return 1;  // not DDPF_RGB (luminance, bump, FourCC): keep enumerating
    return reinterpret_cast<MatchFormatFn>(0x572f70)(ctx, pf, z);
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
    if (opt.signText)
        log("compat: sign text through current Uniscribe %s",
            hook::Import(game::IatScriptStringOut, &h_ScriptStringOut, &o_ScriptStringOut) ? "on" : "hook FAILED");

    // Texture formats: the matcher (0x572f70, stdcall(ctx, DDPIXELFORMAT*, 0),
    // called from the EnumTextureFormats callback at 0x57306c) compares only
    // the bit counts of the four masks. dgVoodoo2 lists A8L8 (LUMINANCE |
    // ALPHAPIXELS) with 0xff in all three colour masks, after ARGB8888: it
    // counts as 8,8,8,8 and replaces it, so 32-bit textures were created as
    // 16-bit grey surfaces. Only RGB formats are offered to the matcher.
    if (opt.textureFormats) {
        bool ok = hook::Redirect(0x57306c, 0xe8, 0x572f70, (void*)&h_MatchTextureFormat);
        log("compat: texture formats matched only against RGB formats %s", ok ? "on" : "patch FAILED");
    }

    if (opt.textures32)
        log("compat: 32-bit textures %s",
            hook::Patch(game::Texture32Read, game::Texture32ReadBytes, game::Texture32OnBytes, 5) ? "on" : "patch FAILED");
    if (opt.textureMemoryX8)
        log("compat: texture cache budget x8 %s",
            hook::Patch(game::TextureBudgetShift, game::TextureBudgetShiftBytes, game::TextureBudgetX8Bytes, 3)
                ? "on"
                : "patch FAILED");

    if (opt.present) log("compat: present Blt waits instead of DDBLT_ALPHADEST %s",
        hook::Patch(game::PresentBltFlags, game::PresentBltFlagsBytes, game::PresentBltWaitBytes, sizeof game::PresentBltWaitBytes) ? "on"
                                                                                                    : "patch FAILED");
}

}  // namespace compat
