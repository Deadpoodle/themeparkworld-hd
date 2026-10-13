// [Compat] Registry=1: answer the game's registry reads from the ini.
//
// The game opens HKLM\software\Bullfrog Productions Ltd\Theme Park World with
// RegCreateKeyExA (so it would create the key, under UAC virtualisation) and
// reads only the DWORD "Language". If that value is missing it exits silently
// before any window (re/startup-and-switches.md §3). Here those keys are fake
// handles that never reach the registry; every other key passes through.
#include "registry.h"

#include <cstring>

#include "game.h"
#include "hook.h"

namespace registry {
namespace {

// Odd values: real registry handles are multiples of 4.
HKEY const kBullfrog = (HKEY)0x7f5a0001;
HKEY const kGame = (HKEY)0x7f5a0003;

DWORD g_language = 0x409;
void (*g_log)(const char*, ...);

decltype(&RegCreateKeyExA) o_RegCreateKeyExA;
decltype(&RegOpenKeyA) o_RegOpenKeyA;
decltype(&RegQueryValueExA) o_RegQueryValueExA;
decltype(&RegCloseKey) o_RegCloseKey;

bool Fake(HKEY k) { return k == kBullfrog || k == kGame; }

// Which fake key a (parent, subkey) pair names, or nullptr for a real one.
HKEY Resolve(HKEY parent, LPCSTR sub) {
    if (!sub) return Fake(parent) ? parent : nullptr;
    if (parent == HKEY_LOCAL_MACHINE || parent == HKEY_CURRENT_USER) {
        if (_stricmp(sub, "software\\Bullfrog Productions Ltd") == 0) return kBullfrog;
        if (_stricmp(sub, "software\\Bullfrog Productions Ltd\\Theme Park World") == 0) return kGame;
        return nullptr;
    }
    if (parent == kBullfrog && _stricmp(sub, "Theme Park World") == 0) return kGame;
    if (Fake(parent)) return kGame;  // any deeper key: an empty fake
    return nullptr;
}

LSTATUS WINAPI h_RegCreateKeyExA(HKEY parent, LPCSTR sub, DWORD reserved, LPSTR cls, DWORD options, REGSAM sam,
                                 const LPSECURITY_ATTRIBUTES sa, PHKEY result, LPDWORD disposition) {
    if (HKEY k = Resolve(parent, sub)) {
        g_log("registry: create %s -> fake %p", sub ? sub : "", (void*)k);
        *result = k;
        if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
        return ERROR_SUCCESS;
    }
    return o_RegCreateKeyExA(parent, sub, reserved, cls, options, sam, sa, result, disposition);
}

LSTATUS WINAPI h_RegOpenKeyA(HKEY parent, LPCSTR sub, PHKEY result) {
    if (HKEY k = Resolve(parent, sub)) {
        *result = k;
        return ERROR_SUCCESS;
    }
    return o_RegOpenKeyA(parent, sub, result);
}

LSTATUS WINAPI h_RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cb) {
    if (!Fake(key)) return o_RegQueryValueExA(key, name, reserved, type, data, cb);
    if (key != kGame || !name || _stricmp(name, "Language") != 0) {
        g_log("registry: %s not answered", name ? name : "(default)");
        return ERROR_FILE_NOT_FOUND;
    }
    if (type) *type = REG_DWORD;
    if (!data) {
        if (cb) *cb = sizeof(DWORD);
        return ERROR_SUCCESS;
    }
    if (!cb || *cb < sizeof(DWORD)) {
        if (cb) *cb = sizeof(DWORD);
        return ERROR_MORE_DATA;
    }
    g_log("registry: Language -> %#lx", (unsigned long)g_language);
    memcpy(data, &g_language, sizeof(DWORD));
    *cb = sizeof(DWORD);
    return ERROR_SUCCESS;
}

LSTATUS WINAPI h_RegCloseKey(HKEY key) { return Fake(key) ? ERROR_SUCCESS : o_RegCloseKey(key); }

}  // namespace

bool Install(DWORD language, void (*log)(const char*, ...)) {
    g_language = language;
    g_log = log;
    bool ok = hook::Import(game::IatRegCreateKeyExA, &h_RegCreateKeyExA, &o_RegCreateKeyExA);
    ok &= hook::Import(game::IatRegOpenKeyA, &h_RegOpenKeyA, &o_RegOpenKeyA);
    ok &= hook::Import(game::IatRegQueryValueExA, &h_RegQueryValueExA, &o_RegQueryValueExA);
    ok &= hook::Import(game::IatRegCloseKey, &h_RegCloseKey, &o_RegCloseKey);
    log("registry: Bullfrog keys answered from the ini (Language=%#lx)%s", (unsigned long)language,
        ok ? "" : " - HOOKS FAILED");
    return ok;
}

}  // namespace registry
