#pragma once

namespace movie {
// [Display] SharpMovies: deblock the movies and enlarge them 2x before the screen stretch.
// saveDir (debug): write one processed frame there as a TGA, or nullptr.
bool Install(const char* saveDir, void (*log)(const char*, ...));
}
