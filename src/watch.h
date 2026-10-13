#pragma once
#include <cstdint>

namespace watch {
// [Debug] Watch=<hex address>: log every write to it on the main thread.
void Install(uintptr_t addr, void (*log)(const char*, ...));
}
