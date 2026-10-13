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

// Replace a 32-bit value at addr, if `expect` is there now.
inline bool PatchU32(uintptr_t addr, uint32_t expect, uint32_t value) {
    return Patch(addr, (const uint8_t*)&expect, (const uint8_t*)&value, 4);
}

// Redirect a `call rel32` (0xe8) or `jmp rel32` (0xe9) at site from its
// current target `expect` to `target`.
inline bool Redirect(uintptr_t site, uint8_t opcode, uintptr_t expect, const void* target) {
    uint8_t want[5] = {opcode}, now[5] = {opcode};
    int32_t from = (int32_t)(expect - (site + 5)), to = (int32_t)((uintptr_t)target - (site + 5));
    memcpy(want + 1, &from, 4);
    memcpy(now + 1, &to, 4);
    return Patch(site, want, now, 5);
}

}  // namespace hook
