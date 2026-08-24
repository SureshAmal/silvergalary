#pragma once

// -----------------------------------------------------------------------------
// SilverText - real text shaping and rasterization.
//
//   FreeType   glyph rasterization (hinting, proper metrics, hinted stems)
//   HarfBuzz   shaping (kerning pairs, ligatures, marks, RTL, Indic/Arabic)
//   fontconfig font discovery + fallback chain (optional)
//
// What this replaces: a stb_truetype bitmap baked for ASCII 32..126 only, with
// no kerning and every non-ASCII byte rendered as a space.
//
// Pipeline for a string:
//   utf8 -> codepoints -> itemize into runs by which font actually has the glyph
//        -> hb_shape() each run -> atlas-cached FreeType bitmaps -> quads
//
// Shaped runs are cached by (string, size), because the UI re-submits the same
// labels every frame and shaping is far too expensive to redo at 60 Hz.
//
// Compiled only when HAVE_HARFBUZZ is defined; otherwise nothing here exists and
// FontRenderer keeps its stb_truetype path.
// -----------------------------------------------------------------------------

#ifdef HAVE_HARFBUZZ

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include "stb_rect_pack.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "silver_config.h"

namespace silvertext {

// A rasterized glyph living in the atlas.
struct Glyph {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    int width = 0;
    int height = 0;
    int bearingX = 0;   // left side bearing, px
    int bearingY = 0;   // top side bearing above baseline, px
    bool valid = false;

    // Source, so a bigger atlas can be repacked in place and every cached
    // ShapedRun keeps pointing at the right glyph.
    int fontIndex = 0;
    unsigned int glyphId = 0;

    // Colour glyphs (emoji) are BGRA bitmaps and cannot live in the single
    // channel coverage atlas, so they get their own RGBA page.
    bool color = false;
};

// One glyph placed on the pen line. `glyphIndex` indexes TextEngine::glyphs,
// which only ever grows, so cached runs survive an atlas rebuild.
struct PositionedGlyph {
    int glyphIndex = -1;
    float x = 0.0f;
    float y = 0.0f;   // offset from the baseline
};

struct ShapedRun {
    std::vector<PositionedGlyph> glyphs;
    float width = 0.0f;
};

class TextEngine {
public:
    // ---- lifecycle ---------------------------------------------------------
    bool init(float pixelSize, const std::vector<std::string>& fontFiles) {
        if (FT_Init_FreeType(&library) != 0) {
            fprintf(stderr, "[SilverText] FreeType init failed\n");
            return false;
        }

        const SilverConfig& cfg = SilverConfig::get();

        // Glyphs are rasterized at whole pixels; a fractional request would be
        // rounded by FreeType anyway, and rounding it here keeps the metrics we
        // lay out with identical to the metrics we rasterized.
        float configured = cfg.num("text.pixelSize", 0.0f);
        if (configured > 0.0f) pixelSize = configured;
        size = std::round(pixelSize);
        if (size < 6.0f) size = 6.0f;

        loadFlags = resolveLoadFlags(cfg);

        // Build a chain, never an exclusive choice: a caller-supplied path is
        // only a preference. If it does not exist (or is not a font) we still
        // fall through to the system font and the emoji/CJK fallbacks, instead
        // of dropping all the way back to the ASCII-only renderer.
        std::vector<std::string> files = fontFiles;
#ifdef HAVE_FONTCONFIG
        for (const std::string& p : discoverFonts()) files.push_back(p);
#endif
        for (const std::string& p : builtinFontCandidates()) files.push_back(p);

        std::vector<std::string> seen;
        for (const std::string& path : files) {
            if (std::find(seen.begin(), seen.end(), path) != seen.end()) continue;
            seen.push_back(path);
            addFont(path);
            if (fonts.size() >= 6) break;   // primary + a few fallbacks is plenty
        }
        if (fonts.empty()) {
            fprintf(stderr, "[SilverText] no usable font found\n");
            return false;
        }

        FT_Face primary = fonts[0].face;
        ascent = (float)(primary->size->metrics.ascender >> 6);
        descent = (float)(-(primary->size->metrics.descender >> 6));
        lineGap = (float)(primary->size->metrics.height >> 6) - (ascent + descent);

        resetAtlas(1024);
        return true;
    }

    // Re-point the engine at a new pixel size and/or hinting mode.
    //
    // This is what makes a live font-size change possible: the atlas is a cache
    // keyed by glyph, so changing size just means the old entries are stale.
    // Faces are resized in place, both caches are dropped, and the atlas is
    // cleared - no GL objects are destroyed, the texture is simply re-uploaded.
    bool reconfigure(float pixelSize) {
        float wanted = std::round(pixelSize);
        if (wanted < 6.0f) wanted = 6.0f;

        int newFlags = resolveLoadFlags(SilverConfig::get());
        if (wanted == size && newFlags == loadFlags) return false;

        size = wanted;
        loadFlags = newFlags;

        for (Font& f : fonts) {
            sizeFace(f.face, size);
            if (f.hb) {
                hb_font_destroy(f.hb);
                f.hb = hb_ft_font_create_referenced(f.face);
            }
        }

        if (!fonts.empty()) {
            FT_Face primary = fonts[0].face;
            ascent = (float)(primary->size->metrics.ascender >> 6);
            descent = (float)(-(primary->size->metrics.descender >> 6));
            lineGap = (float)(primary->size->metrics.height >> 6) - (ascent + descent);
        }

        glyphs.clear();
        glyphLookup.clear();
        runCache.clear();
        resetAtlas(atlasDim > 0 ? atlasDim : 1024);
        return true;
    }

    void shutdown() {
        for (Font& f : fonts) {
            if (f.hb) hb_font_destroy(f.hb);
            if (f.face) FT_Done_Face(f.face);
        }
        fonts.clear();
        if (library) {
            FT_Done_FreeType(library);
            library = nullptr;
        }
    }

    ~TextEngine() { shutdown(); }

    // ---- metrics -----------------------------------------------------------
    float ascentPx() const { return ascent; }
    float descentPx() const { return descent; }
    float lineHeightPx() const { return ascent + descent + lineGap; }
    float pixelSize() const { return size; }

    // ---- shaping -----------------------------------------------------------
    const ShapedRun& shape(const std::string& utf8) {
        auto it = runCache.find(utf8);
        if (it != runCache.end()) return it->second;

        // Unbounded growth would be a slow leak on dynamic strings (clocks,
        // counters), so the cache is simply dropped when it gets large.
        if (runCache.size() > 8192) runCache.clear();

        ShapedRun run;
        buildRun(utf8, run);
        auto res = runCache.emplace(utf8, std::move(run));
        return res.first->second;
    }

    float measure(const std::string& utf8) { return shape(utf8).width; }

    const Glyph* glyphAt(int index) const {
        if (index < 0 || index >= (int)glyphs.size()) return nullptr;
        return &glyphs[(size_t)index];
    }

    // ---- atlas -------------------------------------------------------------
    // Raw 8-bit coverage. The caller owns the GL texture and uploads whenever
    // `dirty` is set.
    const std::vector<unsigned char>& atlasPixels() const { return atlas; }
    int atlasSize() const { return atlasDim; }
    bool dirty = false;
    int generation = 0;   // bumped when the atlas is rebuilt from scratch

    // Colour page, uploaded separately by the renderer.
    const std::vector<unsigned char>& colorAtlasPixels() const { return colorAtlas; }
    int colorAtlasSize() const { return colorDim; }
    bool hasColorGlyphs() const { return colorDim > 0; }
    bool colorDirty = false;
    int colorGeneration = 0;

private:
    struct Font {
        FT_Face face = nullptr;
        hb_font_t* hb = nullptr;
    };

    // Hinting choice matters a lot at UI sizes. FreeType's default for TrueType
    // is the v40 interpreter, which is deliberately *unhinted horizontally* -
    // stems land on fractional pixels and small text reads soft/blurry. Light
    // hinting snaps stems to the pixel grid vertically and is what GTK and Qt
    // use for interface text, so it is the default here too.
    static int resolveLoadFlags(const SilverConfig& cfg) {
        std::string mode = cfg.text("text.hinting", "light");
        int flags = FT_LOAD_RENDER;

        if (mode == "none") {
            flags |= FT_LOAD_NO_HINTING;
        } else if (mode == "mono") {
            flags |= FT_LOAD_TARGET_MONO;          // no antialiasing at all
        } else if (mode == "normal" || mode == "full") {
            flags |= FT_LOAD_TARGET_NORMAL;
        } else {
            flags |= FT_LOAD_TARGET_LIGHT;         // "light" / default
        }

        if (cfg.flag("text.forceAutohint", true) && mode != "none") {
            // The autohinter honours light targets consistently across fonts,
            // whereas a font's own bytecode may ignore them.
            flags |= FT_LOAD_FORCE_AUTOHINT;
        }
        return flags;
    }

    FT_Library library = nullptr;
    std::vector<Font> fonts;
    int loadFlags = FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT;
    float size = 15.0f;
    float ascent = 0.0f, descent = 0.0f, lineGap = 0.0f;

    std::vector<Glyph> glyphs;
    std::unordered_map<uint64_t, int> glyphLookup;   // (font<<32|glyphId) -> index
    std::unordered_map<std::string, ShapedRun> runCache;

    std::vector<unsigned char> atlas;
    int atlasDim = 0;
    stbrp_context packCtx{};
    std::vector<stbrp_node> packNodes;

    // Colour page: RGBA, packed independently of the coverage atlas.
    std::vector<unsigned char> colorAtlas;
    int colorDim = 0;
    stbrp_context colorPackCtx{};
    std::vector<stbrp_node> colorPackNodes;

    // ---- fonts -------------------------------------------------------------
    // Pick a size for a face, coping with bitmap-only fonts.
    //
    // Colour emoji fonts ship fixed strikes and reject FT_Set_Pixel_Sizes
    // outright. That used to make addFont() fail, so the emoji font was dropped
    // from the chain entirely and every emoji fell through to .notdef - which is
    // why they rendered blank.
    static bool sizeFace(FT_Face face, float pixelSize) {
        if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(pixelSize + 0.5f)) == 0) return true;

        if (face->num_fixed_sizes > 0) {
            int best = 0;
            int bestDelta = 1 << 30;
            for (int i = 0; i < face->num_fixed_sizes; ++i) {
                int delta = std::abs((int)(face->available_sizes[i].height) - (int)(pixelSize + 0.5f));
                if (delta < bestDelta) { bestDelta = delta; best = i; }
            }
            return FT_Select_Size(face, best) == 0;
        }
        return false;
    }

    bool addFont(const std::string& path) {
        FT_Face face = nullptr;
        if (FT_New_Face(library, path.c_str(), 0, &face) != 0) return false;
        if (!sizeFace(face, size)) {
            FT_Done_Face(face);
            return false;
        }
        hb_font_t* hb = hb_ft_font_create_referenced(face);
        if (!hb) {
            FT_Done_Face(face);
            return false;
        }
        Font f;
        f.face = face;
        f.hb = hb;
        fonts.push_back(f);
        return true;
    }

    static std::vector<std::string> builtinFontCandidates() {
        static const char* kPaths[] = {
            "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
            "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            // fallback coverage for scripts and emoji
            "/usr/share/fonts/noto/NotoColorEmoji.ttf",
            "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
        };
        std::vector<std::string> out;
        for (const char* p : kPaths) {
            FILE* f = fopen(p, "rb");
            if (f) {
                fclose(f);
                out.push_back(p);
            }
        }
        return out;
    }

#ifdef HAVE_FONTCONFIG
    // Ask fontconfig for the system UI font plus sensible fallbacks, so the
    // chain matches whatever the desktop is actually configured to use.
    static std::vector<std::string> discoverFonts() {
        std::vector<std::string> out;
        if (!FcInit()) return out;

        const char* families[] = { "sans-serif", "Noto Color Emoji", "Noto Sans CJK SC" };
        for (const char* family : families) {
            FcPattern* pattern = FcNameParse((const FcChar8*)family);
            if (!pattern) continue;
            FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
            FcDefaultSubstitute(pattern);

            FcResult result;
            FcPattern* match = FcFontMatch(nullptr, pattern, &result);
            if (match) {
                FcChar8* file = nullptr;
                if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
                    std::string path((const char*)file);
                    if (std::find(out.begin(), out.end(), path) == out.end()) {
                        out.push_back(path);
                    }
                }
                FcPatternDestroy(match);
            }
            FcPatternDestroy(pattern);
        }
        return out;
    }
#endif

    // ---- atlas management --------------------------------------------------
    void resetAtlas(int dim) {
        atlasDim = dim;
        atlas.assign((size_t)dim * dim, 0);
        packNodes.resize((size_t)dim);
        stbrp_init_target(&packCtx, dim, dim, packNodes.data(), (int)packNodes.size());
        generation++;
        dirty = true;

        // The colour page is allocated lazily: most users never render an emoji,
        // and an unused 512x512 RGBA page is 1 MB of nothing.
        if (colorDim > 0) resetColorAtlas(colorDim);
    }

    void resetColorAtlas(int dim) {
        colorDim = dim;
        colorAtlas.assign((size_t)dim * dim * 4, 0);
        colorPackNodes.resize((size_t)dim);
        stbrp_init_target(&colorPackCtx, dim, dim, colorPackNodes.data(), (int)colorPackNodes.size());
        colorGeneration++;
        colorDirty = true;
    }

    // Grow and re-rasterize everything already known. Glyph indices are stable
    // across this, so cached runs stay valid - which matters because a run being
    // built right now may already hold indices.
    bool growAtlas() {
        if (atlasDim >= 4096) return false;
        resetAtlas(atlasDim * 2);

        for (size_t i = 0; i < glyphs.size(); ++i) {
            Glyph& g = glyphs[i];
            if (!g.valid || g.width <= 0 || g.height <= 0) continue;
            if (!rasterizeInto(g)) {
                // Should not happen right after doubling, but never leave a
                // glyph pointing at stale atlas coordinates.
                g.u0 = g.v0 = g.u1 = g.v1 = 0.0f;
                g.width = g.height = 0;
            }
        }
        return true;
    }

    // Render one glyph from its font and pack it into the current atlas.
    bool rasterizeInto(Glyph& g) {
        FT_Face face = fonts[(size_t)g.fontIndex].face;
        // Colour faces are bitmap strikes: hinting targets are meaningless and
        // FT_LOAD_COLOR is required or FreeType will not return the BGRA data.
        int flags = FT_HAS_COLOR(face) ? (FT_LOAD_RENDER | FT_LOAD_COLOR)
                                       : loadFlags;
        if (FT_Load_Glyph(face, g.glyphId, flags) != 0) {
            return false;
        }
        FT_GlyphSlot slot = face->glyph;

        // Colour strikes (CBDT/sbix BGRA emoji) go to the RGBA page.
        if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
            return rasterizeColorInto(g, slot);
        }

        if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY &&
            slot->bitmap.pixel_mode != FT_PIXEL_MODE_MONO) {
            g.width = g.height = 0;
            return true;
        }

        g.color = false;
        int gw = (int)slot->bitmap.width;
        int gh = (int)slot->bitmap.rows;
        if (gw <= 0 || gh <= 0) {
            g.width = g.height = 0;
            return true;
        }

        const int pad = 1;   // keeps bilinear sampling from bleeding
        stbrp_rect rect{};
        rect.w = (stbrp_coord)(gw + pad * 2);
        rect.h = (stbrp_coord)(gh + pad * 2);
        stbrp_pack_rects(&packCtx, &rect, 1);
        if (!rect.was_packed) return false;

        int px = rect.x + pad;
        int py = rect.y + pad;
        blit(slot->bitmap, px, py);

        g.bearingX = slot->bitmap_left;
        g.bearingY = slot->bitmap_top;
        g.width = gw;
        g.height = gh;
        g.u0 = (float)px / (float)atlasDim;
        g.v0 = (float)py / (float)atlasDim;
        g.u1 = (float)(px + gw) / (float)atlasDim;
        g.v1 = (float)(py + gh) / (float)atlasDim;
        dirty = true;
        return true;
    }

    // Look up (or rasterize and cache) a glyph. Returns its stable index.
    int intern(int fontIndex, unsigned int glyphId) {
        uint64_t key = ((uint64_t)(uint32_t)fontIndex << 32) | (uint64_t)glyphId;
        auto it = glyphLookup.find(key);
        if (it != glyphLookup.end()) return it->second;

        Glyph g;
        g.fontIndex = fontIndex;
        g.glyphId = glyphId;
        g.valid = true;

        if (!rasterizeInto(g)) {
            // Atlas is full: grow (which repacks everything already interned)
            // and try this glyph once more.
            if (!growAtlas() || !rasterizeInto(g)) return -1;
        }

        glyphs.push_back(g);
        int idx = (int)glyphs.size() - 1;
        glyphLookup[key] = idx;
        return idx;
    }

    // Pack a BGRA emoji bitmap into the colour page, scaling it down to the
    // current text size (bitmap strikes come at a fixed, usually much larger
    // resolution than the UI font).
    bool rasterizeColorInto(Glyph& g, FT_GlyphSlot slot) {
        int sw = (int)slot->bitmap.width;
        int sh = (int)slot->bitmap.rows;
        if (sw <= 0 || sh <= 0) {
            g.width = g.height = 0;
            g.color = true;
            return true;
        }

        if (colorDim == 0) resetColorAtlas(512);

        // Target box: square-ish, matching the line's ascent.
        int target = (int)(size + 0.5f);
        if (target < 4) target = 4;
        int dw = target;
        int dh = std::max(1, (int)((int64_t)sh * target / sw));
        if (dh > target) {
            dh = target;
            dw = std::max(1, (int)((int64_t)sw * target / sh));
        }

        const int pad = 1;
        stbrp_rect rect{};
        rect.w = (stbrp_coord)(dw + pad * 2);
        rect.h = (stbrp_coord)(dh + pad * 2);
        stbrp_pack_rects(&colorPackCtx, &rect, 1);
        if (!rect.was_packed) {
            if (colorDim >= 2048) { g.width = g.height = 0; g.color = true; return true; }
            resetColorAtlas(colorDim * 2);
            return rasterizeColorInto(g, slot);
        }

        int ox = rect.x + pad;
        int oy = rect.y + pad;

        // Nearest-neighbour is adequate here: emoji strikes are large and the
        // result is a small UI glyph.
        for (int y = 0; y < dh; ++y) {
            int sy = (int)((int64_t)y * sh / dh);
            const unsigned char* srow = slot->bitmap.buffer + (size_t)sy * slot->bitmap.pitch;
            unsigned char* drow = colorAtlas.data() + ((size_t)(oy + y) * colorDim + ox) * 4;
            for (int x = 0; x < dw; ++x) {
                int sx = (int)((int64_t)x * sw / dw);
                const unsigned char* sp = srow + (size_t)sx * 4;
                // FreeType hands back BGRA, premultiplied.
                drow[x * 4 + 0] = sp[2];
                drow[x * 4 + 1] = sp[1];
                drow[x * 4 + 2] = sp[0];
                drow[x * 4 + 3] = sp[3];
            }
        }

        // Scale the placement metrics by the same factor as the bitmap.
        float scale = (sw > 0) ? (float)dw / (float)sw : 1.0f;
        g.color = true;
        g.width = dw;
        g.height = dh;
        g.bearingX = (int)(slot->bitmap_left * scale);
        g.bearingY = (int)(slot->bitmap_top * scale);
        g.u0 = (float)ox / (float)colorDim;
        g.v0 = (float)oy / (float)colorDim;
        g.u1 = (float)(ox + dw) / (float)colorDim;
        g.v1 = (float)(oy + dh) / (float)colorDim;
        colorDirty = true;
        return true;
    }

    void blit(const FT_Bitmap& bmp, int px, int py) {
        for (unsigned int row = 0; row < bmp.rows; ++row) {
            unsigned char* dst = atlas.data() + (size_t)(py + (int)row) * atlasDim + px;
            const unsigned char* src = bmp.buffer + (size_t)row * bmp.pitch;
            if (bmp.pixel_mode == FT_PIXEL_MODE_MONO) {
                for (unsigned int col = 0; col < bmp.width; ++col) {
                    dst[col] = (src[col >> 3] & (0x80 >> (col & 7))) ? 255 : 0;
                }
            } else {
                memcpy(dst, src, bmp.width);
            }
        }
    }

    // ---- itemization + shaping --------------------------------------------
    static void decodeUTF8(const std::string& s, std::vector<uint32_t>& out) {
        out.clear();
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = (unsigned char)s[i];
            uint32_t cp = 0;
            int extra = 0;
            if (c < 0x80)            { cp = c;          extra = 0; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
            else { i++; continue; }   // stray continuation byte

            if (i + (size_t)extra >= s.size()) break;
            bool ok = true;
            for (int k = 1; k <= extra; ++k) {
                unsigned char cc = (unsigned char)s[i + (size_t)k];
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (ok) out.push_back(cp);
            i += (size_t)extra + 1;
        }
    }

    // First font in the chain that actually has this codepoint.
    int fontForCodepoint(uint32_t cp) const {
        for (size_t i = 0; i < fonts.size(); ++i) {
            if (FT_Get_Char_Index(fonts[i].face, (FT_ULong)cp) != 0) return (int)i;
        }
        return 0;
    }

    void buildRun(const std::string& utf8, ShapedRun& out) {
        std::vector<uint32_t> cps;
        decodeUTF8(utf8, cps);
        if (cps.empty()) return;

        float penX = 0.0f;
        size_t start = 0;
        while (start < cps.size()) {
            int fontIndex = fontForCodepoint(cps[start]);
            size_t end = start + 1;
            while (end < cps.size() && fontForCodepoint(cps[end]) == fontIndex) end++;

            shapeSegment(cps, start, end, fontIndex, penX, out);
            start = end;
        }
        out.width = penX;
    }

    void shapeSegment(const std::vector<uint32_t>& cps, size_t start, size_t end,
                      int fontIndex, float& penX, ShapedRun& out) {
        hb_buffer_t* buf = hb_buffer_create();
        if (!buf) return;

        // The whole string is handed over with only [start,end) marked as the
        // item, so joining scripts (Arabic) still see their neighbours.
        hb_buffer_add_codepoints(buf,
                                 (const hb_codepoint_t*)cps.data(), (int)cps.size(),
                                 (unsigned int)start, (int)(end - start));
        // Script, direction and language are inferred from the text itself, so
        // Arabic and Hebrew get RTL ordering without the caller knowing.
        hb_buffer_guess_segment_properties(buf);

        hb_shape(fonts[(size_t)fontIndex].hb, buf, nullptr, 0);

        unsigned int count = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &count);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);

        for (unsigned int i = 0; i < count; ++i) {
            int glyphIdx = intern(fontIndex, info[i].codepoint);
            if (glyphIdx >= 0) {
                PositionedGlyph pg;
                pg.glyphIndex = glyphIdx;
                pg.x = penX + (float)pos[i].x_offset / 64.0f;
                pg.y = (float)pos[i].y_offset / 64.0f;
                out.glyphs.push_back(pg);
            }
            penX += (float)pos[i].x_advance / 64.0f;
        }

        hb_buffer_destroy(buf);
    }
};

} // namespace silvertext

#endif // HAVE_HARFBUZZ
