#pragma once

// -----------------------------------------------------------------------------
// SilverConfig - every layout, spacing, border, timing and animation knob lives
// in one JSON file instead of being hard-coded across the UI.
//
//   SilverConfig& cfg = SilverConfig::get();
//   float gap = cfg.num("grid.gap", 10.0f);
//   bool  on  = cfg.flag("animation.enabled", true);
//
// The file is looked up (first hit wins):
//   1. $SILVER_CONFIG
//   2. ./config/silver.json
//   3. <exe dir>/config/silver.json  and  <exe dir>/../config/silver.json
//   4. ~/.config/silver_gallery/silver.json
// If nothing is found the built-in defaults are used and written to (4) so the
// user has a file to edit. Edits are picked up live - reloadIfChanged() polls
// the file's mtime and re-reads it without a restart.
// -----------------------------------------------------------------------------

#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <system_error>
#include <sys/stat.h>
#include <filesystem>
#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#endif

// -----------------------------------------------------------------------------
// Tiny JSON reader (objects, arrays, strings, numbers, bools, null, comments).
// -----------------------------------------------------------------------------
namespace sjson {

struct Value {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
    Type type = NUL;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> fields;

    const Value* member(const std::string& key) const {
        if (type != OBJ) return nullptr;
        for (const auto& kv : fields) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::string& src) : s(src) {}

    bool parse(Value& out) {
        skip();
        if (!parseValue(out)) return false;
        skip();
        return true;
    }

    std::string error;

private:
    const std::string& s;
    size_t i = 0;

    void skip() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
                i++;
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') i++;
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
                i = (i + 2 < s.size()) ? i + 2 : s.size();
            } else {
                break;
            }
        }
    }

    bool fail(const char* msg) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s at offset %zu", msg, i);
        error = buf;
        return false;
    }

    bool parseValue(Value& v) {
        skip();
        if (i >= s.size()) return fail("unexpected end");

        char c = s[i];
        if (c == '{') return parseObject(v);
        if (c == '[') return parseArray(v);
        if (c == '"') {
            v.type = Value::STR;
            return parseString(v.text);
        }
        if (!s.compare(i, 4, "true"))  { v.type = Value::BOOL; v.boolean = true;  i += 4; return true; }
        if (!s.compare(i, 5, "false")) { v.type = Value::BOOL; v.boolean = false; i += 5; return true; }
        if (!s.compare(i, 4, "null"))  { v.type = Value::NUL; i += 4; return true; }

        char* end = nullptr;
        double d = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return fail("bad value");
        i = (size_t)(end - s.c_str());
        v.type = Value::NUM;
        v.number = d;
        return true;
    }

    bool parseString(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        i++;
        out.clear();
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': i += 4; out += '?'; break; // escapes beyond ASCII are not needed here
                    default:  out += e;   break;
                }
            } else {
                out += c;
            }
        }
        if (i >= s.size()) return fail("unterminated string");
        i++;
        return true;
    }

    bool parseObject(Value& v) {
        v.type = Value::OBJ;
        i++; // {
        skip();
        while (i < s.size() && s[i] != '}') {
            std::string key;
            if (!parseString(key)) return false;
            skip();
            if (i >= s.size() || s[i] != ':') return fail("expected ':'");
            i++;
            Value child;
            if (!parseValue(child)) return false;
            v.fields.emplace_back(std::move(key), std::move(child));
            skip();
        }
        if (i >= s.size()) return fail("unterminated object");
        i++; // }
        return true;
    }

    bool parseArray(Value& v) {
        v.type = Value::ARR;
        i++; // [
        skip();
        while (i < s.size() && s[i] != ']') {
            Value child;
            if (!parseValue(child)) return false;
            v.items.push_back(std::move(child));
            skip();
        }
        if (i >= s.size()) return fail("unterminated array");
        i++; // ]
        return true;
    }
};

// Write a value tree back out as readable JSON. Field order is preserved, so a
// file round-trips with its structure (and its "//" documentation members)
// intact.
inline void escapeString(const std::string& in, std::string& out) {
    out += '"';
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    out += '"';
}

inline void serialize(const Value& v, std::string& out, int indent = 0) {
    std::string pad((size_t)indent * 2, ' ');
    std::string padInner((size_t)(indent + 1) * 2, ' ');

    switch (v.type) {
        case Value::NUL:  out += "null"; break;
        case Value::BOOL: out += v.boolean ? "true" : "false"; break;
        case Value::STR:  escapeString(v.text, out); break;
        case Value::NUM: {
            char buf[64];
            double rounded = (double)(long long)v.number;
            if (v.number == rounded && std::abs(v.number) < 1e15) {
                snprintf(buf, sizeof(buf), "%lld", (long long)v.number);
            } else {
                snprintf(buf, sizeof(buf), "%g", v.number);
            }
            out += buf;
            break;
        }
        case Value::ARR: {
            if (v.items.empty()) { out += "[]"; break; }
            out += "[";
            for (size_t i = 0; i < v.items.size(); ++i) {
                if (i) out += ", ";
                serialize(v.items[i], out, indent + 1);
            }
            out += "]";
            break;
        }
        case Value::OBJ: {
            if (v.fields.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < v.fields.size(); ++i) {
                out += padInner;
                escapeString(v.fields[i].first, out);
                out += ": ";
                serialize(v.fields[i].second, out, indent + 1);
                if (i + 1 < v.fields.size()) out += ",";
                out += "\n";
            }
            out += pad + "}";
            break;
        }
    }
}

} // namespace sjson

// -----------------------------------------------------------------------------
// Built-in defaults. Kept in sync with config/silver.json in the repo.
// -----------------------------------------------------------------------------
static const char* kSilverDefaultConfig = R"JSON({
  "//": "SilverGallery configuration. Every value here is live-reloaded on save.",

  "layout": {
    "topBarHeight": 60.0,
    "sidebarWidth": 340.0,
    "sidebarInnerPadding": 16.0,
    "contentTopOffset": 70.0,
    "bottomPadding": 60.0,
    "folderBannerHeight": 56.0,
    "statusPillBottomMargin": 16.0,
    "zoomPillBottomMargin": 18.0,
    "zoomPillRightMargin": 20.0
  },

  "grid": {
    "gap": 10.0,
    "sidePadding": 24.0,
    "sectionHeaderHeight": 46.0,
    "sectionSpacing": 20.0,
    "sectionBottomPadding": 16.0,
    "tileRadius": 8.0,
    "tileImagePadding": 2.0,
    "tileMinSize": 40.0,
    "minItemSize": 70.0,
    "maxItemSize": 440.0,
    "maxColumns": 24,
    "hoverLift": 3.5,
    "shadowOffset": 2.0,
    "shadowAlpha": 0.20,
    "shadowHoverAlpha": 0.16,
    "selectedBorderWidth": 3.0,
    "hoverBorderWidth": 2.0,
    "captionHeight": 30.0,
    "starBadgeSize": 26.0,
    "starBadgeMargin": 6.0
  },

  "folders": {
    "sidePadding": 24.0,
    "gap": 16.0,
    "cardWidth": 280.0,
    "cardHeight": 80.0,
    "cardRadius": 10.0,
    "cardBorderWidth": 1.0,
    "iconSize": 32.0
  },

  "sidebar": {
    "previewHeight": 190.0,
    "previewRadius": 8.0,
    "buttonHeight": 34.0,
    "buttonRadius": 6.0,
    "metaRowHeight": 24.0,
    "pathLineHeight": 20.0,
    "pathMaxLines": 6,
    "sectionSpacing": 14.0,
    "borderWidth": 1.0
  },

  "fullscreen": {
    "topBarHeight": 64.0,
    "imageMargin": 40.0,
    "arrowSize": 48.0,
    "buttonSize": 36.0,
    "minZoom": 0.2,
    "maxZoom": 10.0,
    "zoomStep": 0.15
  },

  "scrollbar": {
    "width": 16.0,
    "handleHeight": 30.0,
    "topMargin": 40.0,
    "bottomMargin": 80.0,
    "radius": 4.0
  },

  "zoom": {
    "wheelStep": 0.04,
    "keyStep": 0.08,
    "minScale": 0.05,
    "maxScale": 1.0,
    "popupWidth": 208.0,
    "popupHeight": 214.0,
    "popupRadius": 10.0,
    "autoCloseSeconds": 3.5,
    "presetXL": 1.0,
    "presetLarge": 0.7,
    "presetMedium": 0.45,
    "presetSmall": 0.2,
    "groupingThresholdDay": 0.30,
    "groupingThresholdWeek": 0.20,
    "groupingThresholdMonth": 0.12,
    "groupingAnchorDay": 0.55,
    "groupingAnchorWeek": 0.25,
    "groupingAnchorMonth": 0.16,
    "groupingAnchorYear": 0.08
  },

  "keys": {
    "//": "GLFW reports physical key POSITIONS, so compositor-level remaps (XKB caps:swapescape, Hyprland kb_options) are invisible to this app. Enable this if your Caps Lock position sends Escape.",
    "capsLockActsAsEscape": false,
    "//escapeRepeat": "false = one Escape press dismisses one thing; true = holding Escape cascades",
    "escapeRepeat": false
  },

  "icons": {
    "//": "Lucide icon weight. 1.0 = upstream 2px stroke; raise for heavier icons.",
    "strokeScale": 1.25
  },

  "text": {
    "//": "Text rendering. hinting: light (GTK/Qt-style, crispest for UI) | normal | none | mono.",
    "hinting": "light",
    "forceAutohint": true,
    "//pixelSize": "Interface text size in points; the whole UI scales with it.",
    "pixelSize": 15
  },

  "gif": {
    "//": "Animated GIF playback and decode caps.",
    "animate": true,
    "maxFrames": 300,
    "maxDecodeMegabytes": 256
  },

  "animation": {
    "//": "enabled:false makes every transition instant.",
    "enabled": true,
    "speedMultiplier": 1.0,
    "scrollRate": 20.0,
    "scrollWheelStep": 64.0,
    "layoutGlideRate": 26.0,
    "hoverRate": 24.0,
    "selectRate": 24.0,
    "sidebarRate": 20.0,
    "fullscreenRate": 20.0,
    "zoomPopupRate": 24.0,
    "themeMenuRate": 24.0,
    "tabIndicatorRate": 24.0,
    "filmstripRate": 15.0,
    "skeletonPulseRate": 4.0,
    "settleEpsilon": 0.2,
    "fadeEpsilon": 0.005,
    "animateOffscreenTiles": false,
    "//layoutMargin": "How far beyond the viewport (points) tiles still animate during a reflow.",
    "layoutMargin": 700,
    "toastSeconds": 2.0,
    "zoomPillSeconds": 2.0,

    "//model": "spring = second-order physics, exponential = plain smoothing",
    "model": "spring",

    "//spring": "f = natural frequency (Hz), z = damping ratio (1.0 critically damped, <1 bouncy), r = initial response (>0 anticipates, <0 winds up)",
    "spring": {
      "tile":         { "frequency": 3.0, "damping": 1.00, "response": 0.0 },
      "scroll":       { "frequency": 2.6, "damping": 1.05, "response": 0.0 },
      "sidebar":      { "frequency": 2.4, "damping": 0.90, "response": 0.2 },
      "fullscreen":   { "frequency": 3.2, "damping": 0.95, "response": 0.1 },
      "hover":        { "frequency": 4.0, "damping": 0.85, "response": 0.0 },
      "select":       { "frequency": 4.0, "damping": 0.90, "response": 0.0 },
      "zoomPopup":    { "frequency": 3.6, "damping": 0.85, "response": 0.3 },
      "themeMenu":    { "frequency": 3.6, "damping": 0.85, "response": 0.3 },
      "tabIndicator": { "frequency": 3.4, "damping": 0.85, "response": 0.2 },
      "filmstrip":    { "frequency": 2.4, "damping": 1.00, "response": 0.0 },

      "viewerTransform":  { "frequency": 3.4, "damping": 1.00, "response": 0.0 },
      "viewerChrome":     { "frequency": 2.6, "damping": 1.00, "response": 0.0 },
      "viewerPopup":      { "frequency": 3.6, "damping": 0.85, "response": 0.3 },
      "viewerGridScroll": { "frequency": 2.8, "damping": 1.05, "response": 0.0 }
    }
  },

  "thumbnails": {
    "//": "Tiers the grid decodes at; the smallest one covering the cell wins.",
    "tiers": [96, 160, 256, 384, 512],
    "tierHeadroom": 1.35,
    "//masterTier": "One cached image per photo at this size; every smaller size is derived from it. 0 = largest tier.",
    "masterTier": 0,
    "//maxResidentMegabytes": "GPU memory budget for thumbnails. Counting textures instead of bytes meant this scaled with the zoom tier.",
    "maxResidentMegabytes": 256,
    "uploadsPerFrame": 32,
    "diskCache": true,
    "//useSharedCache": "read thumbnails already generated by the file manager (~/.cache/thumbnails)",
    "useSharedCache": true,
    "diskCacheQuality": 85,
    "previewMaxEdge": 4096,
    "prewarmLibrary": true,
    "//prewarmWorkers": "0 = auto (worker count minus reservedWorkers)",
    "prewarmWorkers": 0,
    "reservedWorkers": 2,
    "startupPrimeCount": 120,
    "startupPrimeMillis": 900
  },

  "scanner": {
    "batchSize": 400,
    "walkerThreads": 0,
    "inspectorThreads": 0,
    "refreshIntervalSeconds": 0.6,
    "pruneMissing": true
  }
})JSON";

// -----------------------------------------------------------------------------
// UI scale
//
// Interface text size drives the size of everything else. Growing the font
// without growing paddings, row heights and bar heights just makes labels
// collide, so every layout metric is multiplied by this factor.
// -----------------------------------------------------------------------------
inline float silverUiScale(float baseFontPoints = 15.0f);

// -----------------------------------------------------------------------------
// Config accessor
// -----------------------------------------------------------------------------
class SilverConfig {
public:
    static SilverConfig& get() {
        static SilverConfig instance;
        return instance;
    }

    std::string sourcePath;
    bool loadedFromFile = false;
    int revision = 0;    // bumped on every successful (re)load

    void init(const char* argv0 = nullptr) {
        if (argv0) exePath = argv0;

        // Keep the built-in defaults around so settings UI can show what has
        // been changed and offer a revert.
        {
            sjson::Parser parser(kSilverDefaultConfig);
            sjson::Value parsed;
            if (parser.parse(parsed)) defaultRoot = std::move(parsed);
        }

        std::string found = locate();
        if (!found.empty() && loadFile(found)) {
            sourcePath = found;
            loadedFromFile = true;
        } else {
            loadText(kSilverDefaultConfig);
            sourcePath = writeDefaultsToUserDir();
            loadedFromFile = false;
        }
        revision++;
    }

    // Cheap mtime poll; returns true when the file was re-read.
    bool reloadIfChanged() {
        if (sourcePath.empty()) return false;
        struct stat st;
        if (stat(sourcePath.c_str(), &st) != 0) return false;
        if ((int64_t)st.st_mtime == lastMtime) return false;
        lastMtime = (int64_t)st.st_mtime;
        if (!loadFile(sourcePath)) return false;
        loadedFromFile = true;
        revision++;
        return true;
    }

    // ---- typed lookups; `path` is dot separated, e.g. "grid.gap" -------------
    float num(const char* path, float fallback) const {
        const sjson::Value* v = resolve(path);
        if (!v || v->type != sjson::Value::NUM) return fallback;
        return (float)v->number;
    }

    int integer(const char* path, int fallback) const {
        const sjson::Value* v = resolve(path);
        if (!v || v->type != sjson::Value::NUM) return fallback;
        return (int)std::lround(v->number);
    }

    bool flag(const char* path, bool fallback) const {
        const sjson::Value* v = resolve(path);
        if (!v) return fallback;
        if (v->type == sjson::Value::BOOL) return v->boolean;
        if (v->type == sjson::Value::NUM) return v->number != 0.0;
        return fallback;
    }

    std::string text(const char* path, const char* fallback) const {
        const sjson::Value* v = resolve(path);
        if (!v || v->type != sjson::Value::STR) return fallback ? fallback : "";
        return v->text;
    }

    // ---- defaults (for "is this modified?" and revert) ----------------------
    float numDefault(const char* path, float fallback) const {
        const sjson::Value* v = resolveIn(defaultRoot, path);
        return (v && v->type == sjson::Value::NUM) ? (float)v->number : fallback;
    }

    bool flagDefault(const char* path, bool fallback) const {
        const sjson::Value* v = resolveIn(defaultRoot, path);
        if (!v) return fallback;
        if (v->type == sjson::Value::BOOL) return v->boolean;
        if (v->type == sjson::Value::NUM) return v->number != 0.0;
        return fallback;
    }

    std::string textDefault(const char* path, const char* fallback) const {
        const sjson::Value* v = resolveIn(defaultRoot, path);
        return (v && v->type == sjson::Value::STR) ? v->text : (fallback ? fallback : "");
    }

    // ---- mutation -----------------------------------------------------------
    void setNum(const char* path, float value) {
        sjson::Value* slot = ensurePath(path);
        if (!slot) return;
        slot->type = sjson::Value::NUM;
        slot->number = value;
        revision++;
    }

    void setFlag(const char* path, bool value) {
        sjson::Value* slot = ensurePath(path);
        if (!slot) return;
        slot->type = sjson::Value::BOOL;
        slot->boolean = value;
        revision++;
    }

    void setText(const char* path, const std::string& value) {
        sjson::Value* slot = ensurePath(path);
        if (!slot) return;
        slot->type = sjson::Value::STR;
        slot->text = value;
        revision++;
    }

    // Restore one setting to its built-in default.
    void resetToDefault(const char* path) {
        const sjson::Value* def = resolveIn(defaultRoot, path);
        if (!def) return;
        sjson::Value* slot = ensurePath(path);
        if (slot) {
            *slot = *def;
            revision++;
        }
    }

    bool isModified(const char* path) const {
        const sjson::Value* cur = resolve(path);
        const sjson::Value* def = resolveIn(defaultRoot, path);
        if (!cur || !def) return false;
        if (cur->type != def->type) return true;
        switch (cur->type) {
            case sjson::Value::BOOL: return cur->boolean != def->boolean;
            case sjson::Value::NUM:  return std::abs(cur->number - def->number) > 1e-9;
            case sjson::Value::STR:  return cur->text != def->text;
            default: return false;
        }
    }

    // Persist the tree back to the file it came from.
    bool save() {
        if (sourcePath.empty()) return false;

        std::string body;
        sjson::serialize(root, body, 0);
        body += "\n";

        std::string tmp = sourcePath + ".tmp";
        FILE* fp = fopen(tmp.c_str(), "wb");
        if (!fp) return false;
        bool ok = fwrite(body.data(), 1, body.size(), fp) == body.size();
        fclose(fp);

        std::error_code ec;
        if (!ok) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        std::filesystem::rename(tmp, sourcePath, ec);
        if (ec) return false;

        // Adopt our own write so the mtime poll does not re-read it.
        struct stat st;
        if (stat(sourcePath.c_str(), &st) == 0) lastMtime = (int64_t)st.st_mtime;
        return true;
    }

    std::vector<int> intArray(const char* path, const std::vector<int>& fallback) const {
        const sjson::Value* v = resolve(path);
        if (!v || v->type != sjson::Value::ARR) return fallback;
        std::vector<int> out;
        for (const auto& item : v->items) {
            if (item.type == sjson::Value::NUM) out.push_back((int)std::lround(item.number));
        }
        return out.empty() ? fallback : out;
    }

private:
    sjson::Value root;
    sjson::Value defaultRoot;   // parsed from kSilverDefaultConfig
    std::string exePath;
    int64_t lastMtime = 0;

    const sjson::Value* resolve(const char* path) const {
        return resolveIn(root, path);
    }

    static const sjson::Value* resolveIn(const sjson::Value& base, const char* path) {
        const sjson::Value* cur = &base;
        const char* p = path;
        std::string key;
        while (*p) {
            key.clear();
            while (*p && *p != '.') key += *p++;
            if (*p == '.') p++;
            cur = cur->member(key);
            if (!cur) return nullptr;
        }
        return cur;
    }

    // Walk to a leaf, creating intermediate objects as needed.
    sjson::Value* ensurePath(const char* path) {
        sjson::Value* cur = &root;
        const char* p = path;
        std::string key;
        while (*p) {
            key.clear();
            while (*p && *p != '.') key += *p++;
            bool last = (*p != '.');
            if (*p == '.') p++;

            if (cur->type != sjson::Value::OBJ) {
                cur->type = sjson::Value::OBJ;
                cur->fields.clear();
            }

            sjson::Value* child = nullptr;
            for (auto& kv : cur->fields) {
                if (kv.first == key) { child = &kv.second; break; }
            }
            if (!child) {
                cur->fields.emplace_back(key, sjson::Value());
                child = &cur->fields.back().second;
            }
            cur = child;
            if (last) break;
        }
        return cur;
    }

    bool loadText(const std::string& body) {
        sjson::Value parsed;
        sjson::Parser parser(body);
        if (!parser.parse(parsed) || parsed.type != sjson::Value::OBJ) {
            fprintf(stderr, "[SilverConfig] parse error: %s\n", parser.error.c_str());
            return false;
        }
        root = std::move(parsed);
        return true;
    }

    bool loadFile(const std::string& path) {
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (len <= 0) { fclose(fp); return false; }
        std::string body((size_t)len, '\0');
        size_t got = fread(&body[0], 1, (size_t)len, fp);
        fclose(fp);
        if (got != (size_t)len) return false;

        struct stat st;
        if (stat(path.c_str(), &st) == 0) lastMtime = (int64_t)st.st_mtime;
        return loadText(body);
    }

    static std::string userConfigDir() {
#ifdef _WIN32
        const char* appData = getenv("APPDATA");
        std::string base = (appData && appData[0]) ? appData : "C:\\ProgramData";
        return base + "\\SilverGallery";
#else
        const char* xdg = getenv("XDG_CONFIG_HOME");
        std::string base;
        if (xdg && xdg[0]) {
            base = xdg;
        } else {
            const char* home = getenv("HOME");
            if (!home) {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : "/tmp";
            }
            base = std::string(home) + "/.config";
        }
        return base + "/silver_gallery";
#endif
    }

    std::string locate() const {
        std::vector<std::string> candidates;

        const char* env = getenv("SILVER_CONFIG");
        if (env && env[0]) candidates.push_back(env);

        candidates.push_back("config/silver.json");
        candidates.push_back("../config/silver.json");

        if (!exePath.empty()) {
            std::error_code ec;
            std::filesystem::path exe = std::filesystem::absolute(exePath, ec);
            if (!ec) {
                std::filesystem::path dir = exe.parent_path();
                candidates.push_back((dir / "config" / "silver.json").string());
                candidates.push_back((dir.parent_path() / "config" / "silver.json").string());
            }
        }

        candidates.push_back(userConfigDir() + "/silver.json");

        for (const auto& c : candidates) {
            struct stat st;
            if (stat(c.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return c;
        }
        return "";
    }

    std::string writeDefaultsToUserDir() {
        std::string dir = userConfigDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string path = dir + "/silver.json";

        FILE* fp = fopen(path.c_str(), "wb");
        if (!fp) return "";
        fwrite(kSilverDefaultConfig, 1, strlen(kSilverDefaultConfig), fp);
        fclose(fp);

        struct stat st;
        if (stat(path.c_str(), &st) == 0) lastMtime = (int64_t)st.st_mtime;
        return path;
    }
};

inline float silverUiScale(float baseFontPoints) {
    float pts = SilverConfig::get().num("text.pixelSize", 0.0f);
    if (pts <= 0.0f || baseFontPoints <= 0.0f) return 1.0f;
    float scale = pts / baseFontPoints;
    return (scale < 0.6f) ? 0.6f : (scale > 2.0f ? 2.0f : scale);
}
