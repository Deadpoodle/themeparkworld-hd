// The HD Settings page: an "HD Settings" sign under "Quit Game" on the main
// menu (the player-select panel) opens a list of the patch's options, built
// from the game's own widgets (re/frontend-menus.md):
// - the sign is a copy of the Quit Game layout record, one row lower;
// - the page is the engine's TextMenu (the class behind the Esc menus): a
//   dimmed backdrop with centred text rows that fade on hover.
// Clicking a row cycles its value and writes ThemeParkWorldHD.ini at once;
// changes apply at the next start, which the page says.
#include "settings.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game.h"
#include "hook.h"

namespace settings {
namespace {

typedef void* Widget;
void (*g_log)(const char*, ...);
std::string g_ini;

// --- Engine functions (thiscall called as fastcall: ecx = this, edx unused) ---
template <typename T>
T Fn(uintptr_t a) {
    return reinterpret_cast<T>(a);
}
constexpr uintptr_t kPlayerSelectBuild = 0x4a6580;  // void(void)
constexpr uintptr_t kCallBuild1 = 0x5d5bec, kCallBuild2 = 0x48bc5d;
constexpr uintptr_t kPlayerSelect = 0x7cb3f8;  // Widget* g_PlayerSelect

inline Widget LayoutCreate(Widget parent, const uint16_t* layout, void* proc) {
    return Fn<Widget(__fastcall*)(Widget, void*, const uint16_t*, void*)>(0x65fd0e)(parent, nullptr, layout, proc);
}
inline void* New(size_t n) { return Fn<void*(__cdecl*)(size_t)>(0x67acc0)(n); }
inline void TextRendererCtor(void* r) { Fn<void(__fastcall*)(void*, void*, int)>(0x491920)(r, nullptr, 0); }
inline void SetTextRenderer(Widget w, void* r) { Fn<void(__fastcall*)(Widget, void*, void*)>(0x65d1c8)(w, nullptr, r); }
inline void* FontForTier(int id) { return Fn<void*(__cdecl*)(int)>(0x485a70)(id); }
inline void SetFont(Widget w, void* f) { Fn<void(__fastcall*)(Widget, void*, void*)>(0x65c388)(w, nullptr, f); }
inline void SetTextAlign(Widget w, int h, int v) { Fn<void(__fastcall*)(Widget, void*, int, int)>(0x65c428)(w, nullptr, h, v); }
inline void SetText(Widget w, const wchar_t* t) {
    Fn<void(__fastcall*)(Widget, void*, const wchar_t*)>((*(uintptr_t**)w)[0x24 / 4])(w, nullptr, t);
}
inline void SetYellow(Widget w) { Fn<void(__cdecl*)(Widget)>(0x492d20)(w); }
inline void SetColour(Widget w, int r, int g, int b, int a) {
    Fn<void(__fastcall*)(Widget, void*, int, int, int, int)>(0x65c5d5)(w, nullptr, r, g, b, a);
}
inline int DefProc(Widget w, int msg, int a, int b) {
    return Fn<int(__fastcall*)(Widget, void*, int, int, int)>(0x65f66e)(w, nullptr, msg, a, b);
}
inline void PlaySfx(int id) { Fn<void(__cdecl*)(int)>(0x485aa0)(id); }
inline void Show(Widget w, int on) { Fn<void(__fastcall*)(Widget, void*, int)>(0x65d9dd)(w, nullptr, on); }
inline void SetKeyFocus(Widget w) { Fn<void(__fastcall*)(Widget, void*, int)>(0x65e59b)(w, nullptr, 0); }

// TextMenu (0x14-byte object, +0x10 = container widget)
inline void MenuCtor(void* m) { Fn<void(__fastcall*)(void*)>(0x492e40)(m); }
inline void MenuInit(void* m, void* menuProc, uintptr_t itemProc) {
    Fn<void(__fastcall*)(void*, void*, void*, uintptr_t)>(0x492e80)(m, nullptr, menuProc, itemProc);
}
inline int MenuAdd(void* m, int y, const wchar_t* text, int id, int font, int r, int g, int b) {
    return Fn<int(__fastcall*)(void*, void*, int, const wchar_t*, int, int, int, int, int)>(0x492f60)(
        m, nullptr, y, text, id, font, r, g, b);
}
inline void MenuShow(void* m, int keepPopups) { Fn<void(__fastcall*)(void*, void*, int)>(0x493130)(m, nullptr, keepPopups); }
inline void MenuHide(void* m) { Fn<void(__fastcall*)(void*)>(0x4931b0)(m); }
inline void MenuDestroy(void* m) { Fn<void(__fastcall*)(void*)>(0x492e60)(m); }
constexpr uintptr_t kItemProcHover = 0x48b2a0;

// --- The options ---------------------------------------------------------
struct Choice {
    const char* value;
    const wchar_t* text;
};
struct Option {
    const wchar_t* label;
    const char* section;
    const char* key;
    const char* def;
    std::vector<Choice> choices;
};

const std::vector<Option>& Options() {
    static const std::vector<Option> o = {
        {L"Resolution", "Display", "Resolution", "auto",
         {{"auto", L"Auto (desktop)"},
          {"1280x720", L"1280 x 720"},
          {"1600x900", L"1600 x 900"},
          {"1920x1080", L"1920 x 1080"},
          {"2560x1440", L"2560 x 1440"},
          {"3840x2160", L"3840 x 2160"},
          {"original", L"Original (640 x 480)"}}},
        {L"Display", "Display", "Mode", "borderless",
         {{"borderless", L"Borderless"}, {"fullscreen", L"Full screen"}, {"window", L"Window"}}},
        {L"Frame cap", "Display", "MaxFPS", "60",
         {{"60", L"60"}, {"30", L"30"}, {"120", L"120"}, {"144", L"144"}, {"0", L"Off"}}},
        {L"FPS counter (F2)", "Display", "ShowFps", "0", {{"0", L"Off"}, {"1", L"On"}}},
        {L"Sharper UI art", "Display", "SharpUI", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Sharper movies", "Display", "SharpMovies", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Resume after Alt+Tab", "Input", "ResumeOnFocus", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Alt+F4 quits", "Input", "AltF4", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: park music", "Audio", "LevelMusicFix", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: game clock", "Compat", "FixClock", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: intro movie hang", "Compat", "FixMovies", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: full-screen present", "Compat", "FixPresent", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: sign text", "Compat", "FixSignText", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: texture memory x8", "Compat", "TextureMemoryX8", "1", {{"1", L"On"}, {"0", L"Off"}}},
        {L"Fix: focus at start-up", "Compat", "FixModeList", "1", {{"1", L"On"}, {"0", L"Off"}}},
    };
    return o;
}

std::string Get(const Option& o) {
    char buf[64];
    GetPrivateProfileStringA(o.section, o.key, o.def, buf, sizeof buf, g_ini.c_str());
    return buf;
}

std::wstring RowText(const Option& o) {
    std::string v = Get(o);
    std::wstring shown;
    for (const Choice& c : o.choices)
        if (_stricmp(c.value, v.c_str()) == 0) shown = c.text;
    if (shown.empty()) shown = std::wstring(v.begin(), v.end());  // a value typed into the ini
    return std::wstring(o.label) + L": " + shown;
}

void Cycle(const Option& o) {
    std::string v = Get(o);
    size_t i = 0;
    for (size_t k = 0; k < o.choices.size(); ++k)
        if (_stricmp(o.choices[k].value, v.c_str()) == 0) i = k + 1;
    const char* next = o.choices[i % o.choices.size()].value;
    WritePrivateProfileStringA(o.section, o.key, next, g_ini.c_str());
    g_log("settings: [%s] %s=%s", o.section, o.key, next);
}

// --- The page ------------------------------------------------------------
constexpr int kIdTitle = 0x4800, kIdFirst = 0x4801, kIdBack = 0x48f0, kIdNote = 0x48f1;
void* g_menu;

int __cdecl PageProc(Widget w, int msg, int a, int b);

void Open() {
    g_menu = New(0x14);
    MenuCtor(g_menu);
    MenuInit(g_menu, (void*)&PageProc, kItemProcHover);
    // Font ids are the 13 UI fonts of the active tier: 0 is the Esc menus' large
    // one, 5 the smaller sign font.
    int y = MenuAdd(g_menu, 40, L"HD Settings", kIdTitle, 0, 0, 0, 0);
    y += 30;
    for (size_t i = 0; i < Options().size(); ++i)
        y = MenuAdd(g_menu, y + 6, RowText(Options()[i]).c_str(), kIdFirst + (int)i, 5, 0, 0, 0);
    y = MenuAdd(g_menu, y + 40, L"Changes apply the next time the game starts", kIdNote, 5, 0, 0, 0);
    MenuAdd(g_menu, y + 30, L"Back", kIdBack, 0, 0, 0, 0);
    MenuShow(g_menu, 1);
}

void Close() {
    MenuHide(g_menu);
    MenuDestroy(g_menu);
    g_menu = nullptr;
    if (Widget ps = *(Widget*)kPlayerSelect) {
        Show(ps, 1);
        SetKeyFocus(ps);
    }
}

int __cdecl PageProc(Widget w, int msg, int a, int b) {
    if (msg == 0x100001) {
        if (a >= kIdFirst && a < kIdFirst + (int)Options().size()) {
            Cycle(Options()[a - kIdFirst]);
            // Rows keep the width of their first text, so rebuild the page.
            MenuHide(g_menu);
            MenuDestroy(g_menu);
            Open();
            return 1;
        }
        if (a == kIdBack) {
            Close();
            return 1;
        }
        return 1;  // title, note
    }
    if (msg == 0x1000b && a == VK_ESCAPE) {
        Close();
        return 1;
    }
    return DefProc(w, msg, a, b);
}

// --- The main-menu sign --------------------------------------------------
// The stock Quit Game record (layout 0x753c68), one row (199) lower, new id.
const uint16_t kSignLayout[] = {
    0,      1,      1,      0,      0x7a30, 0,      1264,   1094, 2428, 1241,  // window, flags 1, id, rect
    0x11,   0xffff, 0xffff, 0x12,   0xffff, 0xffff,                            // as stock
    3,      1339,   1123,   2039,   1212,                                      // text rect
    1,      0xbe94, 0xee32,                                                    // image "b_login"
    5,      5,                                                                 // end record, end layout
};

int __cdecl SignProc(Widget w, int msg, int a, int b) {
    switch (msg) {
        case 0x10001: SetColour(w, 0xff, 0, 0, 0xff); break;        // hover: red, like Quit Game
        case 0x10002: SetColour(w, 0xec, 0xa2, 0x04, 0xff); break;  // leave: orange
        case 0x10006:
            PlaySfx(0xc1);
            if (!g_menu) {
                if (Widget ps = *(Widget*)kPlayerSelect) Show(ps, 0);
                Open();
            }
            return 1;
    }
    return DefProc(w, msg, a, b);
}

void AddSign() {
    Widget ps = *(Widget*)kPlayerSelect;
    if (!ps) return;
    Widget w = LayoutCreate(ps, kSignLayout, (void*)&SignProc);
    if (!w) {
        g_log("settings: sign not created");
        return;
    }
    void* r = New(0x1a4);
    TextRendererCtor(r);
    SetTextRenderer(w, r);
    SetFont(w, FontForTier(5));
    SetTextAlign(w, 1, 1);
    SetText(w, L"HD Settings");
    SetYellow(w);
    SetColour(w, 0xec, 0xa2, 0x04, 0xff);
}

void h_PlayerSelectBuild() {
    Fn<void (*)()>(kPlayerSelectBuild)();
    AddSign();
}

}  // namespace

bool Install(const std::string& iniPath, void (*log)(const char*, ...)) {
    g_ini = iniPath;
    g_log = log;
    bool ok = hook::Redirect(kCallBuild1, 0xe8, kPlayerSelectBuild, (void*)&h_PlayerSelectBuild) &&
              hook::Redirect(kCallBuild2, 0xe8, kPlayerSelectBuild, (void*)&h_PlayerSelectBuild);
    log("settings: HD Settings on the main menu %s", ok ? "on" : "patch FAILED");
    return ok;
}

}  // namespace settings
