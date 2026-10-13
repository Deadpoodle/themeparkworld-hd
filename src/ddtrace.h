#pragma once

namespace ddtrace {
// [Debug] DDTrace=1: log DirectDraw/Direct3D set-up calls and results.
void Install(void (*log)(const char*, ...));
}
