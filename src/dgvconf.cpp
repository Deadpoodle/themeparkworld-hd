// [Display] Antialiasing / Anisotropic: forwarded to dgVoodoo2, which renders
// the game's Direct3D on Direct3D 11 and can force multisampling and
// anisotropic filtering (dgVoodoo.conf [DirectX] Antialiasing, Filtering).
//
// The two keys in the [DirectX] section of dgVoodoo.conf next to TP.exe are
// rewritten when they differ from our settings; everything else in the file is
// left as it is. Without dgVoodoo2 nothing happens (the game's own Direct3D
// has neither).
#include "dgvconf.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace dgvconf {
namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

}  // namespace

std::string AntialiasingValue(const char* setting) {
    std::string s = Lower(setting);
    if (s == "2x" || s == "4x" || s == "8x" || s == "16x") return s;
    return "appdriven";  // off: as the game asks (it never asks for multisampling)
}

std::string FilteringValue(int anisotropic) {
    if (anisotropic >= 2 && anisotropic <= 16) return std::to_string(anisotropic);
    return "appdriven";
}

bool Sync(const std::string& gameDir, const std::string& aa, const std::string& filtering,
          void (*log)(const char*, ...)) {
    std::string path = gameDir + "dgVoodoo.conf";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        log("dgvconf: no dgVoodoo.conf next to TP.exe; anti-aliasing and filtering left to dgVoodoo2's defaults");
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);

    bool crlf = text.find("\r\n") != std::string::npos;
    std::vector<std::string> lines;
    for (size_t i = 0; i <= text.size();) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(i, e - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        i = e + 1;
    }
    std::string section;
    bool haveSection = false, setAa = false, setFilter = false, changed = false;
    size_t sectionEnd = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = Trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            if (section == "[directx]") sectionEnd = i;
            section = Lower(t);
            if (section == "[directx]") haveSection = true, sectionEnd = lines.size();
            continue;
        }
        if (section != "[directx]" || t.empty() || t[0] == ';') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Lower(Trim(t.substr(0, eq))), value = Trim(t.substr(eq + 1));
        const std::string* want = key == "antialiasing" ? &aa : key == "filtering" ? &filtering : nullptr;
        if (!want) continue;
        (key == "antialiasing" ? setAa : setFilter) = true;
        if (Lower(value) != Lower(*want)) {
            std::string name = key == "antialiasing" ? "Antialiasing" : "Filtering";
            lines[i] = name + std::string(36 - name.size(), ' ') + "= " + *want;
            changed = true;
        }
    }
    if (section == "[directx]") sectionEnd = lines.size();
    // Keys missing (a short conf, like ours): add them to [DirectX], or add the section.
    std::vector<std::string> add;
    if (!setAa) add.push_back("Antialiasing = " + aa);
    if (!setFilter) add.push_back("Filtering = " + filtering);
    if (!add.empty()) {
        changed = true;
        if (!haveSection) {
            lines.push_back("");
            lines.push_back("[DirectX]");
            sectionEnd = lines.size();
        }
        while (sectionEnd > 0 && Trim(lines[sectionEnd - 1]).empty()) --sectionEnd;
        lines.insert(lines.begin() + sectionEnd, add.begin(), add.end());
    }
    if (changed) {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i) out += lines[i] + (i + 1 < lines.size() ? (crlf ? "\r\n" : "\n") : "");
        if (FILE* w = fopen(path.c_str(), "wb")) {
            fwrite(out.data(), 1, out.size(), w);
            fclose(w);
        }
    }
    log("dgvconf: anti-aliasing %s, texture filtering %s%s", aa.c_str(), filtering.c_str(),
        changed ? " (dgVoodoo.conf updated)" : "");
    return changed;
}

}  // namespace dgvconf
