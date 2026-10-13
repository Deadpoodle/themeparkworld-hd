// Theme Park World HD: entry point. Loaded as wsock32.dll (proxy.cpp) at
// process start, before the game's own code runs.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "compat.h"
#include "ddtrace.h"
#include "display.h"
#include "framecap.h"
#include "game.h"
#include "headless.h"
#include "hook.h"
#include "input.h"
#include "prof.h"
#include "registry.h"
#include "settings.h"
#include "version.h"
#include "watch.h"
#include "window.h"

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
    "; Game resolution: auto (the desktop's; on screens wider than 2048 the largest mode up to 2048\r\n"
    "; wide, because the game's Direct3D stops there), WxH (e.g. 1920x1080; wider than 2048 uses\r\n"
    "; the slower software renderer), or original (640x480, or what data\\Resolution.sam says)\r\n"
    "Resolution=auto\r\n"
    "; borderless (a window covering the screen, no display mode change; needs Resolution at the\r\n"
    "; desktop's size, otherwise full screen is used), fullscreen (the original exclusive mode), or\r\n"
    "; window (the game's own window, not yet scalable)\r\n"
    "Mode=borderless\r\n"
    "; Frame-rate cap, 0 = none. The camera turns and scrolls a fixed step per frame, so above\r\n"
    "; 60 (the vsync rate the game was made for) it moves too fast\r\n"
    "MaxFPS=60\r\n"
    "\r\n"
    "[Input]\r\n"
    "; 1 = Alt+F4 quits the game (the original ignored it)\r\n"
    "AltF4=1\r\n"
    "; 1 = a park paused by switching away (Alt+Tab) carries on when you come back; 0 = it stays\r\n"
    "; paused until you press P, as in the original\r\n"
    "ResumeOnFocus=1\r\n"
    "\r\n"
    "[Compat]\r\n"
    "; 1 = answer the game's registry read (its language) itself, so it needs no registry keys\r\n"
    "Registry=1\r\n"
    "; Language the game is told: auto (the one this copy has), or a Windows language ID such as\r\n"
    "; 0x809 (English: data\\Language\\English) or 0x409 (US English: data\\Language\\American)\r\n"
    "Language=auto\r\n"
    "; 1 = keep the game on one CPU core. Leave at 0: together with the frame cap it starved\r\n"
    "; the game's animation (about 4 fps)\r\n"
    "SingleCPU=0\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Switches for the automated tests. Leave them at 0.\r\n"
    "Headless=0\r\n"
    "ScreenshotEvery=0\r\n"
    "QuitAfter=0\r\n"
    "Script=\r\n"
    "; 1 = write the frame rate to the log every 5 s\r\n"
    "LogFPS=0\r\n";

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

// [Debug] Patch=addr:expect:new; ... (hex bytes) for experiments, e.g.
// 5d8bf5:ffdd44ff:ff00ffff. Each is written only if `expect` is there.
std::vector<uint8_t> Hex(const std::string& s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back((uint8_t)strtoul(s.substr(i, 2).c_str(), nullptr, 16));
    return out;
}

void ApplyDebugPatches() {
    char buf[1024];
    GetPrivateProfileStringA("Debug", "Patch", "", buf, sizeof buf, IniPath().c_str());
    std::string all = buf;
    size_t i = 0;
    while (i < all.size()) {
        size_t end = all.find(';', i);
        std::string item = all.substr(i, end == std::string::npos ? std::string::npos : end - i);
        i = end == std::string::npos ? all.size() : end + 1;
        size_t c1 = item.find(':'), c2 = item.find(':', c1 + 1);
        if (c1 == std::string::npos || c2 == std::string::npos) continue;
        uintptr_t addr = strtoul(item.substr(0, c1).c_str(), nullptr, 16);
        std::vector<uint8_t> expect = Hex(item.substr(c1 + 1, c2 - c1 - 1)), now = Hex(item.substr(c2 + 1));
        bool ok = expect.size() == now.size() && !now.empty() && hook::Patch(addr, expect.data(), now.data(), now.size());
        Log("debug patch %#x (%u bytes): %s", (unsigned)addr, (unsigned)now.size(), ok ? "applied" : "NOT applied");
    }
}

void Init() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
    g_dir.assign(path, exeName - path);
    if (_stricmp(exeName, "TP.exe") != 0) return;  // some other program loaded us: plain wsock32

    g_log = fopen((g_dir + "ThemeParkWorldHD.log").c_str(), "w");
    Log("Theme Park World HD %s", TPWHD_VERSION);
    // The game finds its data from the working directory (Path_SetBaseDir
    // 0x5f5690 via _getcwd) and quits silently elsewhere, e.g. from a shortcut
    // with another "Start in" folder.
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, cwd) && _stricmp((std::string(cwd) + "\\").c_str(), g_dir.c_str()) != 0) {
        SetCurrentDirectoryA(g_dir.c_str());
        Log("working directory was %s; set to the game folder", cwd);
    }
    WriteDefaultIni();

    int headlessSetting = IniInt("Debug", "Headless", 0);  // 1 hidden, 2 on screen (asks first)
    bool headlessMode = headlessSetting == 1;
    bool testMode = headlessSetting != 0;
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
    if (IniInt("Compat", "SingleCPU", 0)) PinToOneCpu();

    compat::Options fixes;
    fixes.clock = IniInt("Compat", "FixClock", 1) != 0;
    fixes.modeList = IniInt("Compat", "FixModeList", 1) != 0;
    fixes.movies = IniInt("Compat", "FixMovies", 1) != 0;
    fixes.present = IniInt("Compat", "FixPresent", 1) != 0;
    fixes.textures32 = IniInt("Display", "Textures32", 0) != 0;  // off: corrupts every texture (tested)
    fixes.textureMemoryX8 = IniInt("Compat", "TextureMemoryX8", 1) != 0;
    compat::Install(fixes, Log);
    display::InstallSoftwareClearFix(Log);  // a bug fix in every mode: never wanted
    display::DetectWrapper(g_dir.c_str(), Log);

    char res[64];
    GetPrivateProfileStringA("Display", "Resolution", "auto", res, sizeof res, IniPath().c_str());
    display::Size size{0, 0};
    if (display::Parse(res, &size)) display::Install(size, Log);
    else Log("display: Resolution=%s not understood; original kept", res);

    // Window mode next: its CreateWindowExA hook must be the innermost.
    char mode[32];
    GetPrivateProfileStringA("Display", "Mode", IniInt("Display", "Windowed", 0) ? "window" : "borderless", mode,
                             sizeof mode, IniPath().c_str());
    if (!window::Install(window::ParseMode(mode), size.w, size.h, headlessMode, Log) && headlessMode) ExitProcess(2);
    input::Install(IniInt("Input", "AltF4", 1) != 0, IniInt("Input", "ResumeOnFocus", 1) != 0,
                   IniInt("Debug", "BlockActivateApp", 0) != 0, Log);

    framecap::Install(IniInt("Display", "MaxFPS", 60),
                      testMode || IniInt("Debug", "LogFPS", 0) || IniInt("Debug", "Profile", 0), Log);

    ApplyDebugPatches();
    if (IniInt("Display", "SettingsPage", 1)) settings::Install(IniPath(), Log);
    if (IniInt("Debug", "DDTrace", 0)) ddtrace::Install(Log);
    if (IniInt("Debug", "Profile", 0)) prof::Install(Log);
    char watchAddr[32];
    GetPrivateProfileStringA("Debug", "Watch", "", watchAddr, sizeof watchAddr, IniPath().c_str());
    if (watchAddr[0]) watch::Install(strtoul(watchAddr, nullptr, 16), Log);

    if (testMode) {
        headless::Options opt;
        opt.visible = !headlessMode;
        opt.noPresent = IniInt("Debug", "NoPresent", 0) != 0;
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
