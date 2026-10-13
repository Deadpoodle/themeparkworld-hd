#pragma once
// Import-table hooks and verified code patches.
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace hook {

// Swap the exe's import slot for `fn`; the previous pointer goes to *orig, so
// hooks chain when installed in a fixed order.
template <typename T>
bool Import(uintptr_t slot, T fn, T* orig) {
    DWORD old;
    if (!VirtualProtect((void*)slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = *(T*)slot;
    *(T*)slot = fn;
    VirtualProtect((void*)slot, sizeof(void*), old, &old);
    return true;
}

inline bool Matches(uintptr_t addr, const uint8_t* bytes, size_t n) {
    return memcmp((const void*)addr, bytes, n) == 0;
}

// Write n bytes at addr, but only if `expect` (same length) is there now.
inline bool Patch(uintptr_t addr, const uint8_t* expect, const uint8_t* bytes, size_t n) {
    if (!Matches(addr, expect, n)) return false;
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, bytes, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return true;
}

}  // namespace hook
