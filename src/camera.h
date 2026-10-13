#pragma once

namespace camera {
// [Input] MaxTurnSpeed: limit the middle-button camera turn (degrees per second, 0 = original).
bool Install(int maxDegPerSec, void (*log)(const char*, ...));
}
