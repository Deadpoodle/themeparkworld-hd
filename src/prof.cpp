// [Debug] Profile=1: sample where the game's main thread spends its time.
//
// A thread of our own suspends the main thread every 5 ms, reads its
// instruction pointer, and buckets it by module. When it is outside TP.exe,
// the first TP.exe return address on its stack is taken as the game code that
// made the call. Every 5 s the top entries go to the log, with the frame rate
// from framecap's stats. For problems seen only on the player's screen (focus,
// full screen), where the hidden tests can't reach.
#include "prof.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace prof {
namespace {

void (*g_log)(const char*, ...);
HANDLE g_main;

std::string ModuleOf(DWORD_PTR ip) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)ip, &m) &&
        GetModuleFileNameA(m, name, sizeof name)) {
        const char* slash = strrchr(name, '\\');
        return slash ? slash + 1 : name;
    }
    return name;
}

bool InExe(DWORD v) { return v >= 0x401000 && v < 0x6f0000; }

// The first TP.exe address above esp that follows a call instruction.
DWORD Caller(DWORD esp) {
    const DWORD* p = (const DWORD*)esp;
    for (int i = 0; i < 512; ++i) {
        DWORD v;
        if (IsBadReadPtr(p + i, 4)) break;
        v = p[i];
        if (!InExe(v)) continue;
        const BYTE* c = (const BYTE*)v;
        if (c[-5] == 0xe8 || (c[-6] == 0xff && (c[-5] & 0x38) == 0x10) || (c[-2] == 0xff && (c[-1] & 0x38) == 0x10) ||
            (c[-3] == 0xff && (c[-2] & 0x38) == 0x10))
            return v;
    }
    return 0;
}

template <typename K, typename Fmt>
std::string Top(const std::map<K, int>& m, int total, int n, Fmt fmt) {
    std::vector<std::pair<int, K>> v;
    for (auto& kv : m) v.push_back({kv.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    std::string out;
    char buf[96];
    for (int i = 0; i < (int)v.size() && i < n; ++i) {
        fmt(buf, sizeof buf, v[i].second);
        out += " ";
        out += buf;
        snprintf(buf, sizeof buf, " %d%%", v[i].first * 100 / total);
        out += buf;
    }
    return out;
}

void Name(char* buf, size_t n, const std::string& s) { snprintf(buf, n, "%s", s.c_str()); }
void Addr(char* buf, size_t n, DWORD a) { snprintf(buf, n, "%06lx", (unsigned long)a); }

DWORD WINAPI Sampler(LPVOID) {
    std::map<std::string, int> modules;
    std::map<DWORD, int> exeIps, callers;
    int total = 0;
    DWORD start = GetTickCount();
    for (;;) {
        Sleep(5);
        if (SuspendThread(g_main) == (DWORD)-1) return 0;
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL;
        BOOL ok = GetThreadContext(g_main, &c);
        DWORD caller = ok && !InExe(c.Eip) ? Caller(c.Esp) : 0;
        ResumeThread(g_main);
        if (!ok) continue;
        ++total;
        if (InExe(c.Eip)) {
            ++modules["TP.exe"];
            ++exeIps[c.Eip & ~0xfu];
        } else {
            ++modules[ModuleOf(c.Eip)];
            if (caller) ++callers[caller];
        }
        if (GetTickCount() - start >= 5000 && total) {
            g_log("prof: %d samples. modules:%s", total, Top(modules, total, 6, Name).c_str());
            g_log("prof:   in TP.exe:%s", Top(exeIps, total, 6, Addr).c_str());
            g_log("prof:   called from:%s", Top(callers, total, 6, Addr).c_str());
            modules.clear();
            exeIps.clear();
            callers.clear();
            total = 0;
            start = GetTickCount();
        }
    }
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    // DllMain runs on the game's main thread.
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    CloseHandle(CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr));
    log("prof: sampling the main thread every 5 ms");
}

}  // namespace prof
