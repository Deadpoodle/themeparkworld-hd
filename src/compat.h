#pragma once

namespace compat {
// Always-on fixes: the mode list's exclusive mode (focus theft under
// dgVoodoo2), the movie player's endless Blt retry loops.
void Install(void (*log)(const char*, ...));
}
