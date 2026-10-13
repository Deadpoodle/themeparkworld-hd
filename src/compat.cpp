// Compatibility fixes for modern Windows and DirectX wrappers. Always on: the
// stock behaviour only ever hangs.
#include "compat.h"

#include <windows.h>

#include "game.h"
#include "hook.h"

namespace compat {

void Install(void (*log)(const char*, ...)) {
    // Cursor/input set-up lists display modes through a second DirectDraw
    // object, first taking EXCLUSIVE|FULLSCREEN (0x55) on a topmost
    // desktop-sized popup (_BullfrogLibScreen). Full screen through dgVoodoo2,
    // that steals the game's focus and the game is minimised during the intro
    // movie. Listing modes needs only DDSCL_NORMAL.
    const uint8_t from[] = {0x6a, 0x55}, to[] = {0x6a, 0x08};
    log("compat: mode list without exclusive mode %s",
        hook::Patch(game::ModeListCoopPush, from, to, 2) ? "on" : "patch FAILED");

    // The movie player clears its surface with `while (Blt(colour fill) != DD_OK)`.
    // A lost surface (game minimised) fails forever and the game stops
    // responding. Try once more and go on instead.
    const uint8_t loop[] = {0x75, 0xe9}, once[] = {0x90, 0x90};
    bool ok = hook::Patch(game::MovieFillRetry1, loop, once, 2) && hook::Patch(game::MovieFillRetry2, loop, once, 2);
    log("compat: movie clear retry loops bounded %s", ok ? "on" : "patch FAILED");
}

}  // namespace compat
