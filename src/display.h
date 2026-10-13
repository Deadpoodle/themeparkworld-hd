#pragma once

namespace display {
struct Size {
    int w, h;  // 0x0 = original handling
};
// "auto" (the desktop's resolution), "original", or "WxH".
bool Parse(const char* text, Size* out);
bool Install(Size size, void (*log)(const char*, ...));
}  // namespace display
