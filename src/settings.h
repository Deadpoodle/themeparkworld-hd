#pragma once
#include <string>

namespace settings {
// "HD Settings" on the main menu: a page of the patch's options (saved to the ini).
bool Install(const std::string& iniPath, void (*log)(const char*, ...));
}
