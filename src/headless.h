#pragma once
#include <string>

namespace headless {
struct Options {
    int screenshotEvery = 0;  // seconds; 0 = off
    int quitAfter = 0;        // seconds; 0 = never
    bool visible = false;     // on-screen test: the window is not hidden (full screen allowed)
    std::string script;       // "3:esc, 9:shot": timed key presses and screenshots
};
// Hidden, non-activating, muted game window for automated test runs. The
// display flags must already be patched to windowed.
bool Install(const Options& opt, void (*log)(const char*, ...));
}  // namespace headless
