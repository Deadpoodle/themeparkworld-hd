#pragma once

namespace lightning {
// [Display] LightningFlash: the lightning flash (original negative frame, soft, or off).
enum class Mode { Original, Soft, Off };
Mode ParseMode(const char* text);
bool Install(Mode mode, int forceEverySec, void (*log)(const char*, ...));
void Tick();  // once a frame (the debug ForceLightning timer)
}
