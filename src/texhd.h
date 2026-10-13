#pragma once
#include <string>

namespace texhd {
// Engine texture loading: [Debug] DumpTextures writes every texture as decoded, in 32-bit.
bool Install(const std::string& gameDir, bool dump, void (*log)(const char*, ...));
}
