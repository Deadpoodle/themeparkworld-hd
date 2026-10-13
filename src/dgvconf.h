#pragma once
#include <string>

namespace dgvconf {
// [Display] Antialiasing / Anisotropic, forwarded to dgVoodoo2's dgVoodoo.conf ([DirectX]).
std::string AntialiasingValue(const char* setting);  // off|2x|4x|8x|16x -> dgVoodoo value
std::string FilteringValue(int anisotropic);         // 0/1 = appdriven, 2..16
// Returns true if dgVoodoo.conf was changed.
bool Sync(const std::string& gameDir, const std::string& aa, const std::string& filtering,
          void (*log)(const char*, ...));
}
