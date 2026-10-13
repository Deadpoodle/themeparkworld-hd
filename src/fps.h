#pragma once

namespace fps {
// [Display] ShowFps / F2: our frame counter, drawn on the back buffer before the flip.
bool Install(bool show, void (*log)(const char*, ...));
void Toggle();
}
