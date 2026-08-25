#pragma once

// -----------------------------------------------------------------------------
// IconAtlas - real SVG icons, rasterized at the size they are drawn.
//
// The previous implementation plotted every icon by hand into a fixed 64px
// atlas slot and let the GPU scale it to 14-32px, which is why icons looked
// soft and pixelated. Now each icon is a genuine SVG (Lucide, ISC licensed)
// rasterized through nanosvg at the exact integer pixel size requested, cached
// per (icon, size) in a packed atlas. A 16px icon is rasterized at 16px, so it
// is always crisp and never resampled.
//
// silver_codec.h owns the single nanosvg implementation - including it here
// keeps that the only inclusion site.
// -----------------------------------------------------------------------------

#include "gl_loader.h"
#include "theme.h"
#include "silver_codec.h"
#include "silver_config.h"
#include "silver_icons_data.h"

#ifndef STB_INCLUDE_STB_RECT_PACK_H
#include <stb_rect_pack.h>
#endif

#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <algorithm>

enum IconType {
    ICON_FIT = 0,
    ICON_1TO1,
    ICON_ROTATE,
    ICON_GRID,
    ICON_GRID_CHECK,
    ICON_TARGET,
    ICON_INFO,
    ICON_THEME_DARK,
    ICON_THEME_LIGHT,
    ICON_CLOSE,
    ICON_CHEVRON_LEFT,
    ICON_CHEVRON_RIGHT,
    ICON_CHEVRON_DOWN,
    ICON_CHEVRON_UP,
    ICON_DOC,
    ICON_CAMERA,
    ICON_CALENDAR,
    ICON_DIMENSIONS,
    ICON_LOCATION,
    ICON_CHECK,
    ICON_ZOOM_PLUS,
    ICON_FOLDER,
    ICON_SEARCH,
    ICON_HEART,
    ICON_HEART_FILLED,
    ICON_STAR,
    ICON_STAR_FILLED,
    ICON_REFRESH,
    ICON_LOADER,
    ICON_PHOTO,
    ICON_COPY,
    ICON_EXTERNAL_LINK,
    ICON_ARROW_LEFT,
    ICON_ARROW_RIGHT,
    ICON_LIST,
    ICON_COLUMNS,
    ICON_SLIDERS,
    ICON_COUNT
};

class IconAtlas {
public:
    GLuint textureId = 0;
    int atlasDim = 1024;

    // Framebuffer pixels per layout point, same idea as FontRenderer: an icon
    // asked for at 16pt on a 1.25x display is rasterized at 20px so the GPU
    // never has to upscale it.
    float pixelScale = 1.0f;
    float strokeScale = 1.25f;

    void setPixelScale(float scale) {
        if (scale < 0.5f) scale = 1.0f;
        if (std::abs(scale - pixelScale) < 0.01f) return;
        pixelScale = scale;
        resetAtlas();   // every cached size is now the wrong resolution
    }

    // Re-read icon settings; re-parses the artwork when the weight changed.
    void applyConfig() {
        float wanted = SilverConfig::get().num("icons.strokeScale", 1.25f);
        if (std::abs(wanted - strokeScale) < 0.001f) return;
        strokeScale = wanted;
        reparse();
        resetAtlas();
    }

    bool init() {
        // Parse every icon once. nsvgParse mutates its input, so each SVG is
        // copied into a scratch buffer first.
        // Lucide draws a 2px stroke on a 24px grid, which lands at ~1.2px once
        // scaled to a 14-16px UI icon and reads too light next to text. The
        // weight is scaled at parse time so it stays tunable without
        // regenerating the artwork.
        strokeScale = SilverConfig::get().num("icons.strokeScale", 1.25f);
        reparse();

        rasterizer = nsvgCreateRasterizer();
        if (!rasterizer) return false;

        pixels.assign((size_t)atlasDim * atlasDim * 4, 0);
        nodes.resize((size_t)atlasDim);
        stbrp_init_target(&packCtx, atlasDim, atlasDim, nodes.data(), (int)nodes.size());

        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasDim, atlasDim, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    void reparse() {
        for (NSVGimage* img : parsed) {
            if (img) nsvgDelete(img);
        }
        parsed.assign(ICON_COUNT, nullptr);

        int count = std::min(kIconSvgCount, (int)ICON_COUNT);
        for (int i = 0; i < count; ++i) {
            std::string scratch(kIconSvgs[i].svg);
            NSVGimage* img = nsvgParse(&scratch[0], "px", 96.0f);
            if (img && strokeScale != 1.0f) {
                for (NSVGshape* shape = img->shapes; shape; shape = shape->next) {
                    if (shape->strokeWidth > 0.0f) shape->strokeWidth *= strokeScale;
                }
            }
            parsed[i] = img;
        }
    }

    // Drop every rasterized size and start the packer over. The GL texture is
    // reused; only its contents are invalidated.
    void resetAtlas() {
        cache.clear();
        std::fill(pixels.begin(), pixels.end(), (unsigned char)0);
        nodes.resize((size_t)atlasDim);
        stbrp_init_target(&packCtx, atlasDim, atlasDim, nodes.data(), (int)nodes.size());

        if (textureId) {
            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasDim, atlasDim, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    ~IconAtlas() {
        for (NSVGimage* img : parsed) {
            if (img) nsvgDelete(img);
        }
        if (rasterizer) nsvgDeleteRasterizer(rasterizer);
        if (textureId) glDeleteTextures(1, &textureId);
    }

    void drawIcon(FontRenderer& r, IconType type, float x, float y, float w, float h, Color4 col) {
        if (type < 0 || type >= ICON_COUNT) return;

        // Rasterize at framebuffer resolution, lay out in points. Both the size
        // and the position are snapped to whole physical pixels.
        float sizePts = std::max(w, h);
        int px = (int)std::lround(sizePts * pixelScale);
        if (px < 4) px = 4;
        if (px > 512) px = 512;

        const Entry* e = ensure(type, px);
        if (!e) return;

        const float inv = 1.0f / pixelScale;
        float x0 = std::round(x * pixelScale) * inv;
        float y0 = std::round(y * pixelScale) * inv;
        float x1 = x0 + (float)e->w * inv;
        float y1 = y0 + (float)e->h * inv;

        UIVertex v[6] = {
            { x0, y0, e->u0, e->v0, col.r, col.g, col.b, col.a, 3.0f },
            { x1, y0, e->u1, e->v0, col.r, col.g, col.b, col.a, 3.0f },
            { x1, y1, e->u1, e->v1, col.r, col.g, col.b, col.a, 3.0f },

            { x0, y0, e->u0, e->v0, col.r, col.g, col.b, col.a, 3.0f },
            { x1, y1, e->u1, e->v1, col.r, col.g, col.b, col.a, 3.0f },
            { x0, y1, e->u0, e->v1, col.r, col.g, col.b, col.a, 3.0f },
        };
        r.vertices.insert(r.vertices.end(), v, v + 6);
    }

    void drawIconRotated(FontRenderer& r, IconType type, float x, float y,
                         float w, float h, float radians, Color4 col) {
        if (type < 0 || type >= ICON_COUNT) return;
        int px = (int)std::lround(std::max(w, h) * pixelScale);
        px = std::clamp(px, 4, 512);
        const Entry* e = ensure(type, px);
        if (!e) return;

        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        float cs = std::cos(radians), sn = std::sin(radians);
        auto point = [&](float px0, float py0, float& ox, float& oy) {
            float dx = px0 - cx, dy = py0 - cy;
            ox = cx + dx * cs - dy * sn;
            oy = cy + dx * sn + dy * cs;
        };
        float x0, y0, x1, y1, x2, y2, x3, y3;
        point(x,     y,     x0, y0);
        point(x + w, y,     x1, y1);
        point(x + w, y + h, x2, y2);
        point(x,     y + h, x3, y3);
        UIVertex v[6] = {
            {x0,y0,e->u0,e->v0,col.r,col.g,col.b,col.a,3.0f},
            {x1,y1,e->u1,e->v0,col.r,col.g,col.b,col.a,3.0f},
            {x2,y2,e->u1,e->v1,col.r,col.g,col.b,col.a,3.0f},
            {x0,y0,e->u0,e->v0,col.r,col.g,col.b,col.a,3.0f},
            {x2,y2,e->u1,e->v1,col.r,col.g,col.b,col.a,3.0f},
            {x3,y3,e->u0,e->v1,col.r,col.g,col.b,col.a,3.0f},
        };
        r.vertices.insert(r.vertices.end(), v, v + 6);
    }

private:
    struct Entry {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        int w = 0, h = 0;
    };

    std::vector<NSVGimage*> parsed;
    NSVGrasterizer* rasterizer = nullptr;
    std::unordered_map<uint32_t, Entry> cache;   // (icon << 16) | size

    std::vector<unsigned char> pixels;
    stbrp_context packCtx{};
    std::vector<stbrp_node> nodes;

    const Entry* ensure(IconType type, int px) {
        uint32_t key = ((uint32_t)type << 16) | (uint32_t)px;
        auto it = cache.find(key);
        if (it != cache.end()) return &it->second;

        NSVGimage* img = (type < (int)parsed.size()) ? parsed[type] : nullptr;
        if (!img || img->width <= 0.0f || img->height <= 0.0f) return nullptr;

        const int pad = 1;
        stbrp_rect rect{};
        rect.w = (stbrp_coord)(px + pad * 2);
        rect.h = (stbrp_coord)(px + pad * 2);
        stbrp_pack_rects(&packCtx, &rect, 1);
        if (!rect.was_packed) return nullptr;   // atlas exhausted; icon is skipped

        int ox = rect.x + pad;
        int oy = rect.y + pad;

        // Rasterize straight into the atlas row stride - nanosvg writes RGBA
        // with the stroke colour in RGB and coverage in alpha, which is exactly
        // what the UI shader samples for icons.
        unsigned char* dst = pixels.data() + ((size_t)oy * atlasDim + ox) * 4;
        float scale = (float)px / std::max(img->width, img->height);
        nsvgRasterize(rasterizer, img, 0.0f, 0.0f, scale, dst, px, px, atlasDim * 4);

        Entry e;
        e.w = px;
        e.h = px;
        e.u0 = (float)ox / (float)atlasDim;
        e.v0 = (float)oy / (float)atlasDim;
        e.u1 = (float)(ox + px) / (float)atlasDim;
        e.v1 = (float)(oy + px) / (float)atlasDim;

        // Upload just this icon. New sizes appear rarely (once per size per
        // icon), and we are between frames here, never mid-draw.
        glBindTexture(GL_TEXTURE_2D, textureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, atlasDim);
        glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, px, px, GL_RGBA, GL_UNSIGNED_BYTE, dst);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        auto res = cache.emplace(key, e);
        return &res.first->second;
    }
};
