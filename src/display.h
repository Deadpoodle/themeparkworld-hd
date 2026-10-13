#pragma once

namespace display {
// Widest render target the game's Direct3D device accepts (above it the game
// falls back to its software renderer).
constexpr int kMaxD3DWidth = 2048;

struct Size {
    int w, h;  // 0x0 = original handling
};
// "auto" (the desktop's resolution, or the largest display mode up to
// kMaxD3DWidth in its shape), "original", or "WxH".
bool Parse(const char* text, Size* out);
bool Install(Size size, void (*log)(const char*, ...));
// The software renderer's 32-bpp clear writes every second pixel black; fix it.
bool InstallSoftwareClearFix(void (*log)(const char*, ...));
}  // namespace display
