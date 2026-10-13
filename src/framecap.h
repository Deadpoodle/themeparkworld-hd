#pragma once

namespace framecap {
// maxFps 0 = no cap; stats = log the frame rate every 5 s.
bool Install(int maxFps, bool stats, void (*log)(const char*, ...));
}
