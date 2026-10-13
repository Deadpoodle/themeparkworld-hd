#pragma once
#include <string>

namespace texhd {
// Engine texture loading: [Display] SharpUI enlarges the UI textures (or loads
// data\hd\ replacements); [Debug] DumpTextures writes every texture as decoded, in 32-bit.
bool Install(const std::string& gameDir, bool sharpUi, bool dump, void (*log)(const char*, ...));
int Sharpened();  // UI textures enlarged so far
}
