#pragma once

#include "gl_loader.h"
#include "theme.h"
#include <vector>
#include <cmath>
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
    int slotSize = 64;
    int atlasCols = 6;
    int atlasRows = 6;
    int atlasW = 384;
    int atlasH = 384;

    struct UVRect {
        float u0, v0, u1, v1;
    };
    UVRect icons[ICON_COUNT];

    bool init() {
        atlasW = atlasCols * slotSize;
        atlasH = atlasRows * slotSize;
        std::vector<unsigned char> atlas(atlasW * atlasH * 4, 0);

        for (int i = 0; i < ICON_COUNT; ++i) {
            int col = i % atlasCols;
            int row = i / atlasCols;
            int ox = col * slotSize;
            int oy = row * slotSize;

            icons[i].u0 = (float)ox / (float)atlasW;
            icons[i].v0 = (float)oy / (float)atlasH;
            icons[i].u1 = (float)(ox + slotSize) / (float)atlasW;
            icons[i].v1 = (float)(oy + slotSize) / (float)atlasH;

            renderIcon(atlas.data(), ox, oy, (IconType)i);
        }

        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        return true;
    }

    void renderIcon(unsigned char* dst, int ox, int oy, IconType type) {
        const int ss = 4;
        int subW = slotSize * ss;
        int subH = slotSize * ss;
        std::vector<float> sub(subW * subH, 0.0f);

        float center = subW * 0.5f;
        float radius = subW * 0.40f;

        auto putPix = [&](int x, int y, float a) {
            if (x >= 0 && x < subW && y >= 0 && y < subH) {
                sub[y * subW + x] = std::max(sub[y * subW + x], a);
            }
        };

        auto drawCircleSS = [&](float cx, float cy, float r, float thick, bool filled) {
            if (filled) {
                for (int y = (int)(cy - r - 1.0f); y <= (int)(cy + r + 1.0f); ++y) {
                    for (int x = (int)(cx - r - 1.0f); x <= (int)(cx + r + 1.0f); ++x) {
                        float dist = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
                        if (dist <= r) {
                            float alpha = std::clamp(r - dist + 0.5f, 0.0f, 1.0f);
                            putPix(x, y, alpha);
                        }
                    }
                }
            } else {
                int steps = (int)(2.0f * M_PI * r * 2.5f) + 1;
                for (int s = 0; s <= steps; ++s) {
                    float a = ((float)s / (float)steps) * 2.0f * M_PI;
                    for (float w = -thick * 0.5f; w <= thick * 0.5f; w += 0.5f) {
                        float radW = r + w;
                        putPix((int)(cx + cosf(a) * radW + 0.5f), (int)(cy + sinf(a) * radW + 0.5f), 1.0f);
                    }
                }
            }
        };

        auto drawLineSS = [&](float x1, float y1, float x2, float y2, float thick) {
            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = sqrtf(dx * dx + dy * dy);
            float halfThick = thick * 0.5f;
            if (len <= 0.001f) {
                drawCircleSS(x1, y1, halfThick, 0, true);
                return;
            }
            float nx = -dy / len;
            float ny = dx / len;
            int steps = (int)(len * 2.5f) + 1;

            for (int s = 0; s <= steps; ++s) {
                float t = (float)s / (float)steps;
                float px = x1 + dx * t;
                float py = y1 + dy * t;

                for (float w = -halfThick; w <= halfThick; w += 0.5f) {
                    putPix((int)(px + nx * w + 0.5f), (int)(py + ny * w + 0.5f), 1.0f);
                }
            }
            drawCircleSS(x1, y1, halfThick, 0, true);
            drawCircleSS(x2, y2, halfThick, 0, true);
        };

        auto drawArcSS = [&](float cx, float cy, float r, float startA, float endA, float thick) {
            int steps = (int)(fabs(endA - startA) * r * 2.5f) + 1;
            for (int s = 0; s <= steps; ++s) {
                float a = startA + ((float)s / (float)steps) * (endA - startA);
                for (float w = -thick * 0.5f; w <= thick * 0.5f; w += 0.5f) {
                    float radW = r + w;
                    putPix((int)(cx + cosf(a) * radW + 0.5f), (int)(cy + sinf(a) * radW + 0.5f), 1.0f);
                }
            }
        };

        float th = 8.0f;

        switch (type) {
            case ICON_FIT: {
                // Lucide Maximize (4 corner brackets)
                float d = radius * 0.85f;
                float arm = 24.0f;
                // Top-Left
                drawLineSS(center - d, center - d + arm, center - d, center - d, th);
                drawLineSS(center - d, center - d, center - d + arm, center - d, th);
                // Top-Right
                drawLineSS(center + d - arm, center - d, center + d, center - d, th);
                drawLineSS(center + d, center - d, center + d, center - d + arm, th);
                // Bottom-Left
                drawLineSS(center - d, center + d - arm, center - d, center + d, th);
                drawLineSS(center - d, center + d, center - d + arm, center + d, th);
                // Bottom-Right
                drawLineSS(center + d - arm, center + d, center + d, center + d, th);
                drawLineSS(center + d, center + d, center + d, center + d - arm, th);
                break;
            }
            case ICON_1TO1: {
                float boxR = radius * 0.88f;
                drawLineSS(center - boxR, center - boxR, center + boxR, center - boxR, th * 0.8f);
                drawLineSS(center + boxR, center - boxR, center + boxR, center + boxR, th * 0.8f);
                drawLineSS(center + boxR, center + boxR, center - boxR, center + boxR, th * 0.8f);
                drawLineSS(center - boxR, center + boxR, center - boxR, center - boxR, th * 0.8f);
                // "1"
                drawLineSS(center - 38, center - 24, center - 38, center + 24, th);
                drawLineSS(center - 50, center - 10, center - 38, center - 24, th);
                // ":"
                drawCircleSS(center, center - 12, 5, 0, true);
                drawCircleSS(center, center + 12, 5, 0, true);
                // "1"
                drawLineSS(center + 38, center - 24, center + 38, center + 24, th);
                drawLineSS(center + 26, center - 10, center + 38, center - 24, th);
                break;
            }
            case ICON_ROTATE: {
                // Lucide Rotate-CW
                drawArcSS(center, center, radius * 0.72f, -0.25f * M_PI, 1.40f * M_PI, th);
                float ax = center + cosf(-0.25f * M_PI) * (radius * 0.72f);
                float ay = center + sinf(-0.25f * M_PI) * (radius * 0.72f);
                drawLineSS(ax, ay, ax + 26, ay - 4, th);
                drawLineSS(ax, ay, ax + 4, ay + 26, th);
                break;
            }
            case ICON_GRID: {
                // Lucide LayoutGrid (4 rounded square tiles)
                float sz = radius * 0.65f;
                float gap = 12.0f;
                // Top-Left tile
                drawLineSS(center - gap - sz, center - gap - sz, center - gap, center - gap - sz, th);
                drawLineSS(center - gap, center - gap - sz, center - gap, center - gap, th);
                drawLineSS(center - gap, center - gap, center - gap - sz, center - gap, th);
                drawLineSS(center - gap - sz, center - gap, center - gap - sz, center - gap - sz, th);
                // Top-Right tile
                drawLineSS(center + gap, center - gap - sz, center + gap + sz, center - gap - sz, th);
                drawLineSS(center + gap + sz, center - gap - sz, center + gap + sz, center - gap, th);
                drawLineSS(center + gap + sz, center - gap, center + gap, center - gap, th);
                drawLineSS(center + gap, center - gap, center + gap, center - gap - sz, th);
                // Bottom-Left tile
                drawLineSS(center - gap - sz, center + gap, center - gap, center + gap, th);
                drawLineSS(center - gap, center + gap, center - gap, center + gap + sz, th);
                drawLineSS(center - gap, center + gap + sz, center - gap - sz, center + gap + sz, th);
                drawLineSS(center - gap - sz, center + gap + sz, center - gap - sz, center + gap, th);
                // Bottom-Right tile
                drawLineSS(center + gap, center + gap, center + gap + sz, center + gap, th);
                drawLineSS(center + gap + sz, center + gap, center + gap + sz, center + gap + sz, th);
                drawLineSS(center + gap + sz, center + gap + sz, center + gap, center + gap + sz, th);
                drawLineSS(center + gap, center + gap + sz, center + gap, center + gap, th);
                break;
            }
            case ICON_GRID_CHECK: {
                float sz = radius * 0.65f;
                float gap = 12.0f;
                // Top-Left tile
                drawLineSS(center - gap - sz, center - gap - sz, center - gap, center - gap - sz, th);
                drawLineSS(center - gap, center - gap - sz, center - gap, center - gap, th);
                drawLineSS(center - gap, center - gap, center - gap - sz, center - gap, th);
                drawLineSS(center - gap - sz, center - gap, center - gap - sz, center - gap - sz, th);
                // Top-Right tile
                drawLineSS(center + gap, center - gap - sz, center + gap + sz, center - gap - sz, th);
                drawLineSS(center + gap + sz, center - gap - sz, center + gap + sz, center - gap, th);
                drawLineSS(center + gap + sz, center - gap, center + gap, center - gap, th);
                drawLineSS(center + gap, center - gap, center + gap, center - gap - sz, th);
                // Bottom-Left tile
                drawLineSS(center - gap - sz, center + gap, center - gap, center + gap, th);
                drawLineSS(center - gap, center + gap, center - gap, center + gap + sz, th);
                drawLineSS(center - gap, center + gap + sz, center - gap - sz, center + gap + sz, th);
                drawLineSS(center - gap - sz, center + gap + sz, center - gap - sz, center + gap, th);
                // Checkmark in bottom-right
                drawLineSS(center + gap, center + gap + 16, center + gap + 18, center + gap + 34, th * 1.2f);
                drawLineSS(center + gap + 18, center + gap + 34, center + gap + sz + 6, center + gap - 4, th * 1.2f);
                break;
            }
            case ICON_TARGET: {
                // Lucide Eye / Focus
                drawCircleSS(center, center, radius * 0.35f, 0, true);
                drawArcSS(center, center - radius * 0.45f, radius * 0.95f, 0.25f * M_PI, 0.75f * M_PI, th * 1.1f);
                drawArcSS(center, center + radius * 0.45f, radius * 0.95f, 1.25f * M_PI, 1.75f * M_PI, th * 1.1f);
                break;
            }
            case ICON_INFO: {
                // Lucide Info (Circle + top dot + line)
                drawCircleSS(center, center, radius * 0.82f, th, false);
                drawCircleSS(center, center - radius * 0.36f, 6.0f, 0, true);
                drawLineSS(center, center - radius * 0.08f, center, center + radius * 0.45f, th);
                break;
            }
            case ICON_THEME_DARK: {
                // Lucide Moon (Smooth crescent)
                for (int y = (int)(center - radius * 0.85f); y <= (int)(center + radius * 0.85f); ++y) {
                    for (int x = (int)(center - radius * 0.85f); x <= (int)(center + radius * 0.85f); ++x) {
                        float d1 = sqrtf((x - center) * (x - center) + (y - center) * (y - center));
                        float d2 = sqrtf((x - (center + radius * 0.48f)) * (x - (center + radius * 0.48f)) + (y - (center - radius * 0.10f)) * (y - (center - radius * 0.10f)));
                        if (d1 <= radius * 0.80f && d2 >= radius * 0.75f) {
                            putPix(x, y, 1.0f);
                        }
                    }
                }
                break;
            }
            case ICON_THEME_LIGHT: {
                // Lucide Sun (Center circle + 8 rays)
                drawCircleSS(center, center, radius * 0.45f, 0, true);
                for (int i = 0; i < 8; ++i) {
                    float a = i * (M_PI * 0.25f);
                    float x1 = center + cosf(a) * (radius * 0.65f);
                    float y1 = center + sinf(a) * (radius * 0.65f);
                    float x2 = center + cosf(a) * (radius * 0.95f);
                    float y2 = center + sinf(a) * (radius * 0.95f);
                    drawLineSS(x1, y1, x2, y2, th);
                }
                break;
            }
            case ICON_CLOSE: {
                // Lucide X
                float c = radius * 0.60f;
                drawLineSS(center - c, center - c, center + c, center + c, th * 1.2f);
                drawLineSS(center - c, center + c, center + c, center - c, th * 1.2f);
                break;
            }
            case ICON_CHEVRON_LEFT: {
                // Lucide ChevronLeft (<)
                float cw = radius * 0.45f;
                float ch = radius * 0.65f;
                drawLineSS(center + cw * 0.5f, center - ch, center - cw * 0.5f, center, th * 1.3f);
                drawLineSS(center - cw * 0.5f, center, center + cw * 0.5f, center + ch, th * 1.3f);
                break;
            }
            case ICON_CHEVRON_RIGHT: {
                // Lucide ChevronRight (>)
                float cw = radius * 0.45f;
                float ch = radius * 0.65f;
                drawLineSS(center - cw * 0.5f, center - ch, center + cw * 0.5f, center, th * 1.3f);
                drawLineSS(center + cw * 0.5f, center, center - cw * 0.5f, center + ch, th * 1.3f);
                break;
            }
            case ICON_CHEVRON_DOWN: {
                // Lucide ChevronDown (v)
                float cw = radius * 0.65f;
                float ch = radius * 0.40f;
                drawLineSS(center - cw, center - ch * 0.5f, center, center + ch * 0.5f, th * 1.3f);
                drawLineSS(center, center + ch * 0.5f, center + cw, center - ch * 0.5f, th * 1.3f);
                break;
            }
            case ICON_CHEVRON_UP: {
                // Lucide ChevronUp (^)
                float cw = radius * 0.65f;
                float ch = radius * 0.40f;
                drawLineSS(center - cw, center + ch * 0.5f, center, center - ch * 0.5f, th * 1.3f);
                drawLineSS(center, center - ch * 0.5f, center + cw, center + ch * 0.5f, th * 1.3f);
                break;
            }
            case ICON_DOC: {
                // Lucide FileText
                float w = radius * 1.1f;
                float h = radius * 1.4f;
                float fold = 24.0f;
                drawLineSS(center - w * 0.5f, center - h * 0.5f, center + w * 0.5f - fold, center - h * 0.5f, th);
                drawLineSS(center + w * 0.5f - fold, center - h * 0.5f, center + w * 0.5f, center - h * 0.5f + fold, th);
                drawLineSS(center + w * 0.5f, center - h * 0.5f + fold, center + w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center + h * 0.5f, center - w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center + h * 0.5f, center - w * 0.5f, center - h * 0.5f, th);
                // Fold line
                drawLineSS(center + w * 0.5f - fold, center - h * 0.5f, center + w * 0.5f - fold, center - h * 0.5f + fold, th * 0.8f);
                drawLineSS(center + w * 0.5f - fold, center - h * 0.5f + fold, center + w * 0.5f, center - h * 0.5f + fold, th * 0.8f);
                // Text lines
                drawLineSS(center - w * 0.28f, center - 4, center + w * 0.28f, center - 4, th * 0.9f);
                drawLineSS(center - w * 0.28f, center + 16, center + w * 0.28f, center + 16, th * 0.9f);
                drawLineSS(center - w * 0.28f, center + 36, center + w * 0.06f, center + 36, th * 0.9f);
                break;
            }
            case ICON_CAMERA: {
                // Lucide Camera
                float w = radius * 1.5f;
                float h = radius * 1.1f;
                drawLineSS(center - w * 0.5f, center - h * 0.35f, center + w * 0.5f, center - h * 0.35f, th);
                drawLineSS(center + w * 0.5f, center - h * 0.35f, center + w * 0.5f, center + h * 0.55f, th);
                drawLineSS(center + w * 0.5f, center + h * 0.55f, center - w * 0.5f, center + h * 0.55f, th);
                drawLineSS(center - w * 0.5f, center + h * 0.55f, center - w * 0.5f, center - h * 0.35f, th);
                drawLineSS(center - w * 0.25f, center - h * 0.60f, center + w * 0.05f, center - h * 0.60f, th);
                drawLineSS(center - w * 0.25f, center - h * 0.60f, center - w * 0.25f, center - h * 0.35f, th);
                drawLineSS(center + w * 0.05f, center - h * 0.60f, center + w * 0.05f, center - h * 0.35f, th);
                drawCircleSS(center, center + h * 0.10f, radius * 0.35f, th, false);
                break;
            }
            case ICON_CALENDAR: {
                // Lucide Calendar
                float w = radius * 1.4f;
                float h = radius * 1.3f;
                drawLineSS(center - w * 0.5f, center - h * 0.5f, center + w * 0.5f, center - h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center - h * 0.5f, center + w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center + h * 0.5f, center - w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center + h * 0.5f, center - w * 0.5f, center - h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center - h * 0.15f, center + w * 0.5f, center - h * 0.15f, th);
                // Top binder pins
                drawLineSS(center - w * 0.25f, center - h * 0.65f, center - w * 0.25f, center - h * 0.42f, th);
                drawLineSS(center + w * 0.25f, center - h * 0.65f, center + w * 0.25f, center - h * 0.42f, th);
                break;
            }
            case ICON_DIMENSIONS: {
                // Lucide Scaling / Aspect Ratio
                float w = radius * 1.4f;
                float h = radius * 1.1f;
                drawLineSS(center - w * 0.5f, center - h * 0.5f, center + w * 0.5f, center - h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center - h * 0.5f, center + w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center + h * 0.5f, center - w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center + h * 0.5f, center - w * 0.5f, center - h * 0.5f, th);
                // Diagonal indicator
                drawLineSS(center - w * 0.25f, center + h * 0.25f, center + w * 0.25f, center - h * 0.25f, th * 0.9f);
                break;
            }
            case ICON_LOCATION: {
                // Lucide MapPin
                float lr = radius * 0.55f;
                drawCircleSS(center, center - radius * 0.25f, lr, th, false);
                drawCircleSS(center, center - radius * 0.25f, 5.0f, 0, true);
                drawLineSS(center - lr * 0.75f, center - radius * 0.05f, center, center + radius * 0.75f, th);
                drawLineSS(center + lr * 0.75f, center - radius * 0.05f, center, center + radius * 0.75f, th);
                break;
            }
            case ICON_CHECK: {
                // Lucide Check (✓)
                drawLineSS(center - radius * 0.70f, center + 2, center - radius * 0.15f, center + radius * 0.65f, th * 1.3f);
                drawLineSS(center - radius * 0.15f, center + radius * 0.65f, center + radius * 0.80f, center - radius * 0.65f, th * 1.3f);
                break;
            }
            case ICON_ZOOM_PLUS: {
                // Lucide PlusCircle
                drawCircleSS(center, center, radius * 0.75f, th, false);
                drawLineSS(center - radius * 0.42f, center, center + radius * 0.42f, center, th);
                drawLineSS(center, center - radius * 0.42f, center, center + radius * 0.42f, th);
                break;
            }
            case ICON_FOLDER: {
                // Lucide Folder
                float w = radius * 1.5f;
                float h = radius * 1.1f;
                float tabW = w * 0.45f;
                float tabH = h * 0.25f;
                // Folder top tab
                drawLineSS(center - w * 0.5f, center - h * 0.5f + tabH, center - w * 0.5f + tabW, center - h * 0.5f + tabH, th);
                drawLineSS(center - w * 0.5f, center - h * 0.5f + tabH, center - w * 0.5f, center - h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center - h * 0.5f, center - w * 0.5f + tabW * 0.8f, center - h * 0.5f, th);
                drawLineSS(center - w * 0.5f + tabW * 0.8f, center - h * 0.5f, center - w * 0.5f + tabW, center - h * 0.5f + tabH, th);
                // Folder body
                drawLineSS(center - w * 0.5f, center - h * 0.5f + tabH, center + w * 0.5f, center - h * 0.5f + tabH, th);
                drawLineSS(center + w * 0.5f, center - h * 0.5f + tabH, center + w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center + h * 0.5f, center - w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center + h * 0.5f, center - w * 0.5f, center - h * 0.5f + tabH, th);
                break;
            }
            case ICON_SEARCH: {
                // Lucide Search
                float lensR = radius * 0.45f;
                drawCircleSS(center - radius * 0.18f, center - radius * 0.18f, lensR, th * 1.2f, false);
                float hx0 = (center - radius * 0.18f) + lensR * 0.707f;
                float hy0 = (center - radius * 0.18f) + lensR * 0.707f;
                drawLineSS(hx0, hy0, center + radius * 0.75f, center + radius * 0.75f, th * 1.5f);
                break;
            }
            case ICON_HEART: {
                // Lucide Heart Outline
                float hr = radius * 0.38f;
                drawCircleSS(center - hr * 0.88f, center - hr * 0.35f, hr, th * 1.1f, false);
                drawCircleSS(center + hr * 0.88f, center - hr * 0.35f, hr, th * 1.1f, false);
                drawLineSS(center - hr * 1.68f, center - hr * 0.10f, center, center + radius * 0.80f, th * 1.1f);
                drawLineSS(center + hr * 1.68f, center - hr * 0.10f, center, center + radius * 0.80f, th * 1.1f);
                break;
            }
            case ICON_HEART_FILLED: {
                // Lucide Heart Filled
                float hr = radius * 0.38f;
                drawCircleSS(center - hr * 0.88f, center - hr * 0.35f, hr, 0, true);
                drawCircleSS(center + hr * 0.88f, center - hr * 0.35f, hr, 0, true);
                // Fill lower triangle
                for (int y = (int)(center - hr * 0.35f); y <= (int)(center + radius * 0.82f); ++y) {
                    float t = (float)(y - (center - hr * 0.35f)) / (radius * 0.82f + hr * 0.35f);
                    float curW = (1.0f - t) * (hr * 1.8f);
                    for (int x = (int)(center - curW); x <= (int)(center + curW); ++x) {
                        putPix(x, y, 1.0f);
                    }
                }
                break;
            }
            case ICON_STAR: {
                // Lucide Star Outline
                float rOuter = radius * 0.82f;
                float rInner = radius * 0.36f;
                for (int i = 0; i < 5; ++i) {
                    float a1 = -M_PI * 0.5f + i * (2.0f * M_PI / 5.0f);
                    float a2 = a1 + (M_PI / 5.0f);
                    float a3 = a1 + (2.0f * M_PI / 5.0f);
                    float x1 = center + cosf(a1) * rOuter;
                    float y1 = center + sinf(a1) * rOuter;
                    float x2 = center + cosf(a2) * rInner;
                    float y2 = center + sinf(a2) * rInner;
                    float x3 = center + cosf(a3) * rOuter;
                    float y3 = center + sinf(a3) * rOuter;
                    drawLineSS(x1, y1, x2, y2, th * 1.1f);
                    drawLineSS(x2, y2, x3, y3, th * 1.1f);
                }
                break;
            }
            case ICON_STAR_FILLED: {
                // Lucide Star Solid
                float rOuter = radius * 0.82f;
                float rInner = radius * 0.36f;
                for (int y = (int)(center - rOuter); y <= (int)(center + rOuter); ++y) {
                    for (int x = (int)(center - rOuter); x <= (int)(center + rOuter); ++x) {
                        float dx = x - center;
                        float dy = y - center;
                        float dist = sqrtf(dx * dx + dy * dy);
                        if (dist <= rOuter) {
                            float angle = atan2f(dy, dx) + M_PI * 0.5f;
                            if (angle < 0) angle += 2.0f * M_PI;
                            float segment = fmodf(angle, 2.0f * M_PI / 5.0f);
                            if (segment > M_PI / 5.0f) segment = 2.0f * M_PI / 5.0f - segment;
                            float maxR = rInner / cosf(segment - 0.1f);
                            if (dist <= maxR || dist <= rInner * 1.2f) {
                                putPix(x, y, 1.0f);
                            }
                        }
                    }
                }
                // Outline to ensure crisp boundary
                for (int i = 0; i < 5; ++i) {
                    float a1 = -M_PI * 0.5f + i * (2.0f * M_PI / 5.0f);
                    float a2 = a1 + (M_PI / 5.0f);
                    float a3 = a1 + (2.0f * M_PI / 5.0f);
                    float x1 = center + cosf(a1) * rOuter;
                    float y1 = center + sinf(a1) * rOuter;
                    float x2 = center + cosf(a2) * rInner;
                    float y2 = center + sinf(a2) * rInner;
                    float x3 = center + cosf(a3) * rOuter;
                    float y3 = center + sinf(a3) * rOuter;
                    drawLineSS(x1, y1, x2, y2, th);
                    drawLineSS(x2, y2, x3, y3, th);
                }
                break;
            }
            case ICON_REFRESH: {
                // Lucide Refresh-CW (Dual spinning arrows)
                drawArcSS(center, center, radius * 0.70f, -0.2f * M_PI, 1.4f * M_PI, th * 1.1f);
                float ax = center + cosf(-0.2f * M_PI) * (radius * 0.70f);
                float ay = center + sinf(-0.2f * M_PI) * (radius * 0.70f);
                drawLineSS(ax, ay, ax + 24, ay - 4, th * 1.1f);
                drawLineSS(ax, ay, ax + 4, ay + 24, th * 1.1f);
                break;
            }
            case ICON_PHOTO: {
                // Lucide Image (Rounded photo frame + sun + mountains)
                float w = radius * 1.5f;
                float h = radius * 1.2f;
                drawLineSS(center - w * 0.5f, center - h * 0.5f, center + w * 0.5f, center - h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center - h * 0.5f, center + w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center + w * 0.5f, center + h * 0.5f, center - w * 0.5f, center + h * 0.5f, th);
                drawLineSS(center - w * 0.5f, center + h * 0.5f, center - w * 0.5f, center - h * 0.5f, th);
                // Sun
                drawCircleSS(center - w * 0.22f, center - h * 0.20f, radius * 0.18f, 0, true);
                // Mountains
                drawLineSS(center - w * 0.5f, center + h * 0.35f, center - w * 0.15f, center - h * 0.1f, th);
                drawLineSS(center - w * 0.15f, center - h * 0.1f, center + w * 0.15f, center + h * 0.35f, th);
                drawLineSS(center + w * 0.05f, center + h * 0.20f, center + w * 0.30f, center - h * 0.05f, th);
                drawLineSS(center + w * 0.30f, center - h * 0.05f, center + w * 0.5f, center + h * 0.25f, th);
                break;
            }
            case ICON_COPY: {
                // Lucide Copy (Two overlapping sheets)
                float sz = radius * 0.95f;
                float off = 18.0f;
                // Back sheet (top-left)
                drawLineSS(center - sz * 0.5f - off, center - sz * 0.5f - off, center + sz * 0.5f - off, center - sz * 0.5f - off, th);
                drawLineSS(center - sz * 0.5f - off, center - sz * 0.5f - off, center - sz * 0.5f - off, center + sz * 0.5f - off, th);
                drawLineSS(center + sz * 0.5f - off, center - sz * 0.5f - off, center + sz * 0.5f - off, center - sz * 0.5f, th);
                drawLineSS(center - sz * 0.5f - off, center + sz * 0.5f - off, center - sz * 0.5f, center + sz * 0.5f - off, th);
                // Front sheet
                drawLineSS(center - sz * 0.5f + off, center - sz * 0.5f + off, center + sz * 0.5f + off, center - sz * 0.5f + off, th);
                drawLineSS(center + sz * 0.5f + off, center - sz * 0.5f + off, center + sz * 0.5f + off, center + sz * 0.5f + off, th);
                drawLineSS(center + sz * 0.5f + off, center + sz * 0.5f + off, center - sz * 0.5f + off, center + sz * 0.5f + off, th);
                drawLineSS(center - sz * 0.5f + off, center + sz * 0.5f + off, center - sz * 0.5f + off, center - sz * 0.5f + off, th);
                break;
            }
            case ICON_EXTERNAL_LINK: {
                // Lucide ExternalLink (Box with diagonal arrow out)
                float d = radius * 0.75f;
                // Open box
                drawLineSS(center - d, center - d + 28, center - d, center + d, th);
                drawLineSS(center - d, center + d, center + d, center + d, th);
                drawLineSS(center + d, center + d, center + d, center - d + 28, th);
                drawLineSS(center - d, center - d + 28, center - d + 28, center - d + 28, th);
                // Arrow shaft
                drawLineSS(center - 10, center + 10, center + d, center - d, th * 1.2f);
                // Arrow head
                drawLineSS(center + d - 32, center - d, center + d, center - d, th * 1.2f);
                drawLineSS(center + d, center - d, center + d, center - d + 32, th * 1.2f);
                break;
            }
            case ICON_ARROW_LEFT: {
                // Lucide ArrowLeft (<-)
                drawLineSS(center - radius * 0.75f, center, center + radius * 0.75f, center, th * 1.2f);
                drawLineSS(center - radius * 0.75f, center, center - radius * 0.25f, center - radius * 0.50f, th * 1.2f);
                drawLineSS(center - radius * 0.75f, center, center - radius * 0.25f, center + radius * 0.50f, th * 1.2f);
                break;
            }
            case ICON_ARROW_RIGHT: {
                // Lucide ArrowRight (->)
                drawLineSS(center - radius * 0.75f, center, center + radius * 0.75f, center, th * 1.2f);
                drawLineSS(center + radius * 0.75f, center, center + radius * 0.25f, center - radius * 0.50f, th * 1.2f);
                drawLineSS(center + radius * 0.75f, center, center + radius * 0.25f, center + radius * 0.50f, th * 1.2f);
                break;
            }
            case ICON_LIST: {
                // Lucide List (Bullets + lines)
                float dotR = 4.0f;
                float lx = center - radius * 0.70f;
                float tx0 = center - radius * 0.35f;
                float tx1 = center + radius * 0.75f;
                drawCircleSS(lx, center - radius * 0.50f, dotR, 0, true);
                drawLineSS(tx0, center - radius * 0.50f, tx1, center - radius * 0.50f, th * 1.2f);
                drawCircleSS(lx, center, dotR, 0, true);
                drawLineSS(tx0, center, tx1, center, th * 1.2f);
                drawCircleSS(lx, center + radius * 0.50f, dotR, 0, true);
                drawLineSS(tx0, center + radius * 0.50f, tx1, center + radius * 0.50f, th * 1.2f);
                break;
            }
            case ICON_COLUMNS: {
                // Lucide Columns (3 vertical columns)
                float colW = radius * 0.40f;
                float colH = radius * 1.40f;
                float gap = 8.0f;
                for (int i = -1; i <= 1; ++i) {
                    float colX = center + i * (colW + gap);
                    drawLineSS(colX - colW * 0.5f, center - colH * 0.5f, colX + colW * 0.5f, center - colH * 0.5f, th);
                    drawLineSS(colX + colW * 0.5f, center - colH * 0.5f, colX + colW * 0.5f, center + colH * 0.5f, th);
                    drawLineSS(colX + colW * 0.5f, center + colH * 0.5f, colX - colW * 0.5f, center + colH * 0.5f, th);
                    drawLineSS(colX - colW * 0.5f, center + colH * 0.5f, colX - colW * 0.5f, center - colH * 0.5f, th);
                }
                break;
            }
            case ICON_SLIDERS: {
                // Lucide Sliders
                drawLineSS(center - radius * 0.75f, center - radius * 0.35f, center + radius * 0.75f, center - radius * 0.35f, th);
                drawCircleSS(center - radius * 0.20f, center - radius * 0.35f, 6.0f, 0, true);
                drawLineSS(center - radius * 0.75f, center + radius * 0.35f, center + radius * 0.75f, center + radius * 0.35f, th);
                drawCircleSS(center + radius * 0.20f, center + radius * 0.35f, 6.0f, 0, true);
                break;
            }
            default:
                break;
        }

        for (int py = 0; py < slotSize; ++py) {
            for (int px = 0; px < slotSize; ++px) {
                float totalA = 0.0f;
                for (int sy = 0; sy < ss; ++sy) {
                    for (int sx = 0; sx < ss; ++sx) {
                        totalA += sub[(py * ss + sy) * subW + (px * ss + sx)];
                    }
                }
                float avgA = totalA / (float)(ss * ss);
                int dstIdx = ((oy + py) * atlasW + (ox + px)) * 4;
                dst[dstIdx + 0] = 255;
                dst[dstIdx + 1] = 255;
                dst[dstIdx + 2] = 255;
                dst[dstIdx + 3] = (unsigned char)(std::min(1.0f, avgA) * 255.0f + 0.5f);
            }
        }
    }

    void drawIcon(FontRenderer& r, IconType type, float x, float y, float w, float h, Color4 col) {
        if (type < 0 || type >= ICON_COUNT) return;
        const UVRect& uv = icons[type];
        float x0 = x, y0 = y;
        float x1 = x + w, y1 = y + h;

        UIVertex v[6] = {
            { x0, y0, uv.u0, uv.v0, col.r, col.g, col.b, col.a, 3.0f },
            { x1, y0, uv.u1, uv.v0, col.r, col.g, col.b, col.a, 3.0f },
            { x1, y1, uv.u1, uv.v1, col.r, col.g, col.b, col.a, 3.0f },

            { x0, y0, uv.u0, uv.v0, col.r, col.g, col.b, col.a, 3.0f },
            { x1, y1, uv.u1, uv.v1, col.r, col.g, col.b, col.a, 3.0f },
            { x0, y1, uv.u0, uv.v1, col.r, col.g, col.b, col.a, 3.0f },
        };
        r.vertices.insert(r.vertices.end(), v, v + 6);
    }
};
