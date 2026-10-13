// [Audio] LevelMusicFix=1: in-park level music plays.
//
// A re-implementation of the fix in TPW Patcher by TheAussie (MIT,
// https://github.com/TheAussie/TPW-Patcher), as call-site redirects instead
// of an exe edit (re/music-zoom-fps.md §1). Three bugs leave the level-music
// handle g_LevelMusic (0x803aa0) at 0, so parks are silent:
// 1. loading a park restores the saved sound block straight over the handle
//    (Sound_LoadState 0x51c400): start the music again afterwards;
// 2. the once-a-second track selector (0x51e790) stores 0 over a good handle
//    when the command fails: keep the old handle then;
// 3. on a level's first visit the music starts before its MusicHD.sdt is
//    open: start it once more on the next selector tick.
// Skipped when TPW Patcher has already patched the exe (its ".fix" section,
// or changed bytes at its sites).
#include "music.h"

#include <windows.h>

#include <cstring>

#include "hook.h"

namespace music {
namespace {

void (*g_log)(const char*, ...);
bool g_pending;  // start the music again on the next selector tick (game thread only)

constexpr uintptr_t kLevelMusic = 0x803aa0;  // int handle
constexpr uintptr_t kSoundLoadState = 0x51c400, kMusicStartLevel = 0x51e730, kMusicSelectTrack = 0x51e790;
constexpr uintptr_t kSoundCommand = 0x51bc40;  // cdecl int(handle, cmd, arg) -> handle
constexpr uintptr_t kCallLoadState = 0x415ba2, kCallStartLevel = 0x54ec9f, kCallSelectTrack = 0x54f870;

void StartLevel() { reinterpret_cast<void (*)()>(kMusicStartLevel)(); }

int __cdecl h_LoadState(int stream) {
    int r = reinterpret_cast<int(__cdecl*)(int)>(kSoundLoadState)(stream);
    StartLevel();
    g_pending = true;
    return r;
}

void __cdecl h_StartLevel() {
    StartLevel();
    g_pending = true;
}

void __cdecl h_SelectTrack(int track) {
    if (g_pending) {
        g_pending = false;
        StartLevel();
    }
    int& handle = *(int*)kLevelMusic;
    int old = handle;
    int r = reinterpret_cast<int(__cdecl*)(int, int, int)>(kSoundCommand)(old, 4, track);
    handle = (r == 0 && old != 0) ? old : r;
}

bool TpwPatcherPresent() {
    auto base = (const BYTE*)GetModuleHandleA(nullptr);
    auto nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (strncmp((const char*)sec[i].Name, ".fix", 8) == 0) return true;
    return false;
}

}  // namespace

bool Install(void (*log)(const char*, ...)) {
    g_log = log;
    if (TpwPatcherPresent()) {
        log("music: TPW Patcher is applied to this exe; its music fix is used, ours is off");
        return true;
    }
    const uint8_t retNops[12] = {0xc3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    const uint8_t selStart[5] = {0x8b, 0x44, 0x24, 0x04, 0x8b};
    const uint8_t callLoad[5] = {0xe8, 0x59, 0x68, 0x10, 0x00}, callStart[5] = {0xe8, 0x8c, 0xfa, 0xfc, 0xff},
                  callSelect[5] = {0xe8, 0x1b, 0xef, 0xfc, 0xff};
    // All three or none.
    if (!hook::Matches(0x51e764, retNops, 12) || !hook::Matches(kMusicSelectTrack, selStart, 5) ||
        !hook::Matches(kCallLoadState, callLoad, 5) || !hook::Matches(kCallStartLevel, callStart, 5) ||
        !hook::Matches(kCallSelectTrack, callSelect, 5)) {
        log("music: the music code is not as expected (another patch?); fix not applied");
        return false;
    }
    bool ok = hook::Redirect(kCallLoadState, 0xe8, kSoundLoadState, (void*)&h_LoadState) &&
              hook::Redirect(kCallStartLevel, 0xe8, kMusicStartLevel, (void*)&h_StartLevel) &&
              hook::Redirect(kCallSelectTrack, 0xe8, kMusicSelectTrack, (void*)&h_SelectTrack);
    log("music: level music fix (after TPW Patcher by TheAussie) %s", ok ? "on" : "patch FAILED");
    return ok;
}

}  // namespace music
