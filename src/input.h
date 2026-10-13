#pragma once

namespace input {
// Install before headless (both hook CreateWindowExA; this one must be inner).
void Install(bool altF4, bool blockActivate, void (*log)(const char*, ...));
}
