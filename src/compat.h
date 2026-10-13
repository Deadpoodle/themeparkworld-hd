#pragma once

namespace compat {
// Always-on fixes: the mode list's exclusive mode (focus theft under
// dgVoodoo2), the movie player's endless Blt retry loops.
struct Options {
    bool clock = true, modeList = true, movies = true, present = true;  // [Compat] FixClock, FixModeList, FixMovies, FixPresent
    bool signText = true;         // [Compat] FixSignText: ride/world signs drawn with today's Uniscribe
    bool textures32 = false;      // [Display] Textures32: BROKEN, corrupts every texture (the loader writes 16-bit data)
    bool textureMemoryX8 = true;  // [Compat] TextureMemoryX8: texture cache budget x8 (white textures in big parks)
};
void Install(const Options& opt, void (*log)(const char*, ...));
}
