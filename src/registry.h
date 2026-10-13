#pragma once
#include <windows.h>

namespace registry {
// Answer the game's Bullfrog registry keys from the ini; nothing is written.
bool Install(DWORD language, void (*log)(const char*, ...));
}
