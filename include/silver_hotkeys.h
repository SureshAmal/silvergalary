#pragma once

// Config-driven hotkeys.
//
// Bindings were a long if-chain inside the key callback, which made a shortcut's
// meaning depend on which block it happened to sit in - nest one a level too
// deep and it silently only works in that mode. A table maps a key combination
// to an action name once, up front, so a handler answers "what did the user
// ask for" instead of "which branch am I in".
//
// Combos are written the way people write them: "Ctrl+K", "Shift+Delete",
// "Ctrl+Shift+S", "F5", "Space".

#include "silver_config.h"
#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cctype>
#include <algorithm>

namespace silverhotkeys {

struct Combo {
    int key = -1;
    int mods = 0;      // GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT

    bool valid() const { return key >= 0; }
    bool operator==(const Combo& o) const { return key == o.key && mods == o.mods; }
};

inline int keyFromName(const std::string& raw) {
    std::string n;
    for (char c : raw) if (!isspace((unsigned char)c)) n.push_back((char)toupper((unsigned char)c));
    if (n.empty()) return -1;

    if (n.size() == 1) {
        char c = n[0];
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }
    if (n.size() >= 2 && n[0] == 'F') {
        bool digits = true;
        for (size_t i = 1; i < n.size(); ++i) if (!isdigit((unsigned char)n[i])) digits = false;
        if (digits) {
            int idx = atoi(n.c_str() + 1);
            if (idx >= 1 && idx <= 25) return GLFW_KEY_F1 + (idx - 1);
        }
    }

    static const std::unordered_map<std::string, int> kNamed = {
        { "SPACE", GLFW_KEY_SPACE }, { "ESC", GLFW_KEY_ESCAPE }, { "ESCAPE", GLFW_KEY_ESCAPE },
        { "ENTER", GLFW_KEY_ENTER }, { "RETURN", GLFW_KEY_ENTER }, { "TAB", GLFW_KEY_TAB },
        { "BACKSPACE", GLFW_KEY_BACKSPACE }, { "DELETE", GLFW_KEY_DELETE }, { "DEL", GLFW_KEY_DELETE },
        { "INSERT", GLFW_KEY_INSERT }, { "HOME", GLFW_KEY_HOME }, { "END", GLFW_KEY_END },
        { "PAGEUP", GLFW_KEY_PAGE_UP }, { "PAGEDOWN", GLFW_KEY_PAGE_DOWN },
        { "LEFT", GLFW_KEY_LEFT }, { "RIGHT", GLFW_KEY_RIGHT },
        { "UP", GLFW_KEY_UP }, { "DOWN", GLFW_KEY_DOWN },
        { "PLUS", GLFW_KEY_EQUAL }, { "EQUAL", GLFW_KEY_EQUAL }, { "MINUS", GLFW_KEY_MINUS },
        { "COMMA", GLFW_KEY_COMMA }, { "PERIOD", GLFW_KEY_PERIOD }, { "SLASH", GLFW_KEY_SLASH },
    };
    auto it = kNamed.find(n);
    return (it != kNamed.end()) ? it->second : -1;
}

// "Ctrl+Shift+K" -> { GLFW_KEY_K, CONTROL|SHIFT }
inline Combo parse(const std::string& spec) {
    Combo c;
    size_t start = 0;
    std::vector<std::string> parts;
    while (start <= spec.size()) {
        size_t plus = spec.find('+', start);
        // A trailing "+" is the key itself, not a separator.
        if (plus == std::string::npos || plus + 1 >= spec.size()) {
            parts.push_back(spec.substr(start));
            break;
        }
        parts.push_back(spec.substr(start, plus - start));
        start = plus + 1;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string p;
        for (char ch : parts[i]) if (!isspace((unsigned char)ch)) p.push_back((char)toupper((unsigned char)ch));
        if (p == "CTRL" || p == "CONTROL")   { c.mods |= GLFW_MOD_CONTROL; continue; }
        if (p == "SHIFT")                    { c.mods |= GLFW_MOD_SHIFT;   continue; }
        if (p == "ALT")                      { c.mods |= GLFW_MOD_ALT;     continue; }
        if (p == "SUPER" || p == "META")     { c.mods |= GLFW_MOD_SUPER;   continue; }
        c.key = keyFromName(parts[i]);
    }
    return c;
}

inline std::string format(const Combo& c) {
    if (!c.valid()) return "";
    std::string out;
    if (c.mods & GLFW_MOD_CONTROL) out += "Ctrl+";
    if (c.mods & GLFW_MOD_ALT)     out += "Alt+";
    if (c.mods & GLFW_MOD_SHIFT)   out += "Shift+";
    const char* name = glfwGetKeyName(c.key, 0);
    if (name && *name) {
        std::string n(name);
        if (n.size() == 1) n[0] = (char)toupper((unsigned char)n[0]);
        out += n;
    } else {
        out += "Key" + std::to_string(c.key);
    }
    return out;
}

class Map {
public:
    // Reads every string under a config section as action -> combo. Unknown or
    // unparseable values are skipped rather than silently binding nothing.
    void load(const SilverConfig& cfg, const char* section,
              const std::vector<std::pair<const char*, const char*>>& defaults) {
        byAction.clear();
        for (const auto& kv : defaults) {
            std::string path = std::string(section) + "." + kv.first;
            std::string spec = cfg.text(path.c_str(), kv.second);
            Combo c = parse(spec);
            if (c.valid()) byAction[kv.first] = c;
        }
    }

    // Only relevant modifiers are compared: CapsLock and NumLock arrive in mods
    // too, and matching on them would make a binding stop working with CapsLock
    // on.
    std::string actionFor(int key, int mods) const {
        const int kMask = GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER;
        for (const auto& kv : byAction)
            if (kv.second.key == key && kv.second.mods == (mods & kMask)) return kv.first;
        return "";
    }

    Combo comboFor(const std::string& action) const {
        auto it = byAction.find(action);
        return (it != byAction.end()) ? it->second : Combo{};
    }

    std::string label(const std::string& action) const { return format(comboFor(action)); }

private:
    std::unordered_map<std::string, Combo> byAction;
};

} // namespace silverhotkeys
