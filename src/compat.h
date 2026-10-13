#pragma once

namespace compat {
// Always-on fixes: the mode list's exclusive mode (focus theft under
// dgVoodoo2), the movie player's endless Blt retry loops.
struct Options {
    bool clock = true, modeList = true, movies = true, present = true;  // [Compat] FixClock, FixModeList, FixMovies, FixPresent
};
void Install(const Options& opt, void (*log)(const char*, ...));
}
