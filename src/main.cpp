// Theme Park World HD: entry point. Loaded as wsock32.dll (proxy.cpp) at
// process start, before the game's own code runs.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game.h"
#include "headless.h"
#include "hook.h"
#include "registry.h"
#include "version.h"

namespace {

std::string g_dir;  // game folder, with trailing backslash
FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// Settings

std::string IniPath() { return g_dir + "ThemeParkWorldHD.ini"; }

int IniInt(const char* sec, const char* key, int def) {
    char buf[64];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof buf, IniPath().c_str());
    return buf[0] ? (int)strtoul(buf, nullptr, 0) : def;  // accepts 0x... too
}

const char kDefaultIni[] =
    "; Theme Park World HD settings. Delete this file to get the defaults back.\r\n"
    "\r\n"
    "[Display]\r\n"
    "; 1 = run in a window instead of full screen (the game's own window, not yet scaled)\r\n"
    "Windowed=0\r\n"
    "\r\n"
    "[Compat]\r\n"
    "; 1 = answer the game's registry read (its language) itself, so it needs no registry keys\r\n"
    "Registry=1\r\n"
    "; Language the game is told: auto (the one this copy has), or a Windows language ID such as\r\n"
    "; 0x809 (English: data\\Language\\English) or 0x409 (US English: data\\Language\\American)\r\n"
    "Language=auto\r\n"
    "; 1 = keep the game on one CPU core (old timer and audio-thread code)\r\n"
    "SingleCPU=1\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Switches for the automated tests. Leave them at 0.\r\n"
    "Headless=0\r\n"
    "ScreenshotEvery=0\r\n"
    "QuitAfter=0\r\n";

void WriteDefaultIni() {
    if (GetFileAttributesA(IniPath().c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (FILE* f = fopen(IniPath().c_str(), "wb")) {
        fwrite(kDefaultIni, 1, sizeof kDefaultIni - 1, f);
        fclose(f);
        Log("wrote default ThemeParkWorldHD.ini");
    }
}

// ---------------------------------------------------------------------------

DWORD WINAPI ShowError(LPVOID text) {
    MessageBoxA(nullptr, (const char*)text, "Theme Park World HD", MB_OK | MB_ICONWARNING);
    return 0;
}

bool Recognised() {
    return hook::Matches(game::DisplayFlagsInit, game::DisplayFlagsInitBytes, sizeof game::DisplayFlagsInitBytes) &&
           hook::Matches(game::FontSelectTier, game::FontSelectTierBytes, sizeof game::FontSelectTierBytes);
}

bool PatchWindowed() {
    const uint8_t from = 0x15, to = 0x14;  // display flags bit 0 = fullscreen
    return hook::Patch(game::DisplayFlagsInitLowByte, &from, &to, 1);
}

bool DirExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// The registry LCID picks the folder data\Language\<name> (0x5c0d00); a missing
// folder makes the game quit silently. Unknown LCIDs fall back to 0x809 there.
DWORD GameLanguage() {
    char buf[64];
    GetPrivateProfileStringA("Compat", "Language", "auto", buf, sizeof buf, IniPath().c_str());
    if (_stricmp(buf, "auto") != 0) return strtoul(buf, nullptr, 0);
    if (!DirExists(g_dir + "data\\Language\\English") && DirExists(g_dir + "data\\Language\\American")) return 0x409;
    return 0x809;
}

// Each copy takes the first core no other copy holds, so parallel test copies
// don't share one core.
void PinToOneCpu() {
    DWORD_PTR proc, sys;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) || !proc) return;
    DWORD_PTR one = proc & (~proc + 1);
    for (DWORD_PTR m = proc, i = 0; m; m &= m - 1, ++i) {
        char name[64];
        snprintf(name, sizeof name, "Local\\ThemeParkWorldHD-CPU-%u", (unsigned)i);
        HANDLE claim = CreateMutexA(nullptr, FALSE, name);  // held until exit
        if (claim && GetLastError() != ERROR_ALREADY_EXISTS) {
            one = m & (~m + 1);
            break;
        }
        if (claim) CloseHandle(claim);
    }
    SetProcessAffinityMask(GetCurrentProcess(), one);
    Log("pinned to one CPU (mask %#lx)", (unsigned long)one);
}

void Init() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
    g_dir.assign(path, exeName - path);
    if (_stricmp(exeName, "TP.exe") != 0) return;  // some other program loaded us: plain wsock32

    g_log = fopen((g_dir + "ThemeParkWorldHD.log").c_str(), "w");
    Log("Theme Park World HD %s", TPWHD_VERSION);
    WriteDefaultIni();

    bool headlessMode = IniInt("Debug", "Headless", 0) != 0;
    if (!Recognised()) {
        Log("unrecognised TP.exe; no patches applied");
        if (headlessMode) ExitProcess(2);  // a test must never fall back to full screen
        CloseHandle(CreateThread(nullptr, 0, ShowError,
                                 (LPVOID) "This version of TP.exe is not recognised, so Theme Park World HD has "
                                          "not changed anything. The game will run as normal.\n\n"
                                          "It needs the v2.0 TP.exe without SafeDisc. See ThemeParkWorldHD.log.",
                                 0, nullptr));
        return;
    }

    if (IniInt("Compat", "Registry", 1)) registry::Install(GameLanguage(), Log);
    if (IniInt("Compat", "SingleCPU", 1)) PinToOneCpu();

    if (headlessMode || IniInt("Display", "Windowed", 0)) {
        bool ok = PatchWindowed();
        Log("display: windowed %s", ok ? "on" : "patch FAILED");
        if (!ok && headlessMode) ExitProcess(2);
    }
    if (headlessMode) {
        headless::Options opt;
        opt.screenshotEvery = IniInt("Debug", "ScreenshotEvery", 0);
        opt.quitAfter = IniInt("Debug", "QuitAfter", 0);
        char script[1024];
        GetPrivateProfileStringA("Debug", "Script", "", script, sizeof script, IniPath().c_str());
        opt.script = script;
        if (!headless::Install(opt, Log)) ExitProcess(2);
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init();
    }
    return TRUE;
}
