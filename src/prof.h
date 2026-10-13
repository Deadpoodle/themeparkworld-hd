#pragma once

namespace prof {
// [Debug] Profile=1: sample the main thread; log where time goes every 5 s.
// Call from DllMain (the main thread).
void Install(void (*log)(const char*, ...));
}
