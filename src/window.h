#pragma once

namespace window {
enum class Mode { Borderless, Fullscreen, Window };
// "borderless" (default), "fullscreen", "window".
Mode ParseMode(const char* text);
// gameW/H: the game resolution (0 = the game's own choice). Install before the
// input and headless hooks (all hook CreateWindowExA; this one is innermost).
bool Install(Mode mode, int gameW, int gameH, bool headless, void (*log)(const char*, ...));
}  // namespace window
