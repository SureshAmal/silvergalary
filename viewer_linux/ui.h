#pragma once

#include "font.h"
#include "icons.h"
#include "theme.h"
#include "image_loader.h"
#include "thumbnails.h"
#include "silver_anim.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

struct UIAction {
    enum Type {
        NONE,
        TOGGLE_METADATA_STATE,
        TOGGLE_THEME,
        TOGGLE_FIT,
        SET_1TO1,
        CENTER_VIEW,
        ROTATE,
        TOGGLE_GRID_VIEW,
        TOGGLE_PIXEL_GRID,
        TOGGLE_ZOOM_MENU,
        SET_ZOOM_SCALE,
        SELECT_IMAGE_INDEX,
        OPEN_IMAGE_INDEX,
        PAN_TO_MINIMAP_NORM,
        CLOSE_OVERLAYS
    } type = NONE;
    int index = -1;
    float zoomScaleTarget = 1.0f;
    float normX = 0.0f;
    float normY = 0.0f;
};

struct ZoomMenuItem {
    const char* label;
    const char* shortcut;
    float targetScale; // if < 0: special code (-1 = Fit)
    bool isFit;
};

static const ZoomMenuItem kZoomMenuItems[6] = {
    { "300 %", "Ctrl+3", 3.0f, false },
    { "200 %", "Ctrl+2", 2.0f, false },
    { "100 %", "Ctrl+1", 1.0f, false },
    { "66 %",  "",       0.66f, false },
    { "50 %",  "",       0.50f, false },
    { "Fit",   "Ctrl+0", -1.0f, true }
};

class FilePilotUI {
public:
    FontRenderer font;
    ThemeManager theme;
    ThumbnailManager thumbs;
    IconAtlas iconAtlas;

    // Metadata Viewer States: 0 = Collapsed, 1 = Compact Popup, 2 = Full Expanded
    int metadataState = 0;
    float metaPopupAnim = 0.0f;

    // Zoom Menu State
    bool showZoomMenu = false;
    float zoomMenuAnim = 0.0f;

    // View Modes
    bool isGridView = false;
    float gridScrollOffset = 0.0f;
    float gridTargetScroll = 0.0f;

    bool showThumbnails = true;
    std::string toastMsg = "";
    float toastTimer = 0.0f;

    // Dedicated Presentation Mode (P key)
    bool presentationMode = false;

    // Scrollbar / Scroll Indicator State
    float scrollBarAlpha = 0.0f;
    float scrollActivityTimer = 0.0f;

    // Inactivity Auto-Hide
    float inactivityTimer = 0.0f;
    float uiVisibilityAlpha = 1.0f;
    bool isFullscreenMode = false;

    // Shimmer / Loading Animation
    float shimmerPhase = 0.0f;

    // Smooth Animations
    float minimapAlpha = 0.0f;

    // Layout Constants
    // Interface scale, driven by the configured text size exactly as in the
    // gallery. Every chrome metric below is expressed at scale 1.0 and
    // multiplied in applyScale().
    float uiScale = 1.0f;

    float topBarH = 46.0f;
    float bottomStripH = 80.0f;
    float thumbW = 60.0f;
    float thumbH = 60.0f;
    float thumbGap = 10.0f;
    float cachedWindowW = 1280.0f;
    float cachedWindowH = 800.0f;

    // Minimap Geometry
    float miniX = 0, miniY = 0, miniW = 160, miniH = 100;
    bool isDraggingMinimap = false;

    // Hit Testing Rectangles
    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool isHovered = false;
    };
    Rect headerLabelRect;
    Rect popupCardRect;
    Rect btnPopupClose;
    Rect btnPopupExpand;

    Rect btnFit;
    Rect btn1to1;
    Rect btnCenter;
    Rect btnRotate;
    Rect btnGridView;
    Rect btnTheme;

    // Theme Popover Menu State
    bool showThemeMenu = false;
    float themeMenuAnim = 0.0f;
    Rect themeMenuRect;
    Rect themeSystemOptRect;
    Rect themeDarkOptRect;
    Rect themeLightOptRect;

    Rect zoomBadgeRect;
    Rect zoomMenuRect;
    std::vector<Rect> zoomMenuItemRects;

    // No font path by default - the text engine resolves the system UI font
    // through fontconfig. Passing one here only expresses a preference.
    // Re-derive chrome metrics from the configured text size.
    void applyScale() {
        float k = silverUiScale(14.0f);   // the viewer's base text size
        if (std::abs(k - uiScale) < 0.001f) return;

        float rel = k / uiScale;
        uiScale = k;

        topBarH *= rel;
        bottomStripH *= rel;
        thumbW *= rel;
        thumbH *= rel;
        thumbGap *= rel;
    }

    bool init(const char* fontPath = nullptr) {
        applyScale();
        theme.init();
        thumbs.init();
        iconAtlas.init();
        return font.init(fontPath, 14.0f);   // whole pixels: FreeType rounds anyway
    }

    void notifyUserActivity() {
        inactivityTimer = 0.0f;
    }

    void notifyScrollActivity() {
        inactivityTimer = 0.0f;
        scrollActivityTimer = 1.8f;
    }

    void showToast(const std::string& msg, float duration = 2.0f) {
        toastMsg = msg;
        toastTimer = duration;
        notifyUserActivity();
    }

    // Spring velocities for the viewer's animated chrome.
    float scrollBarAlphaVel = 0.0f;
    float uiVisibilityVel = 0.0f;
    float metaPopupVel = 0.0f, metaPopupPrevTarget = 0.0f;
    float zoomMenuVel = 0.0f, zoomMenuPrevTarget = 0.0f;
    float themeMenuVel = 0.0f, themeMenuPrevTarget = 0.0f;
    float minimapVel = 0.0f;
    float gridScrollVel = 0.0f;

    // Set at the end of update(): true while any chrome animation is still
    // settling. The main loop uses it to decide whether to draw another frame.
    bool animating = true;

    void update(float dt, bool isZoomedIn, int totalFiles, bool isFullscreen) {
        const silveranim::Rates& anim = silveranim::rates();
        theme.update(dt);
        isFullscreenMode = isFullscreen;

        inactivityTimer += dt;
        shimmerPhase += dt * 3.0f;

        // Scrollbar Fade
        if (scrollActivityTimer > 0.0f) silveranim::tickTimer(scrollActivityTimer, dt);
        silveranim::driveFade(scrollBarAlpha, scrollBarAlphaVel,
                              scrollActivityTimer > 0.0f ? 1.0f : 0.0f, anim.chViewerChrome, dt);

        // Overall UI Visibility (auto-fade in fullscreen inactivity, completely 0 in presentation mode)
        float targetUIVisibility = 1.0f;
        if (presentationMode) {
            targetUIVisibility = 0.0f;
        } else if (isFullscreen && !isGridView && metadataState == 0 && !showZoomMenu && inactivityTimer > 2.4f) {
            targetUIVisibility = 0.0f;
        }
        silveranim::driveFade(uiVisibilityAlpha, uiVisibilityVel, targetUIVisibility, anim.chViewerChrome, dt);

        silveranim::tickTimer(toastTimer, dt);
        if (toastTimer == 0.0f && !toastMsg.empty()) toastMsg = "";
        thumbs.updateGL(dt);

        // Smooth Metadata Popup Transition
        float targetAnim = (metadataState > 0 && !presentationMode) ? (float)metadataState : 0.0f;
        silveranim::driveTrackedFade(metaPopupAnim, metaPopupVel, metaPopupPrevTarget, targetAnim, anim.chViewerPopup, dt);

        // Smooth Zoom Menu Transition
        float targetZoomMenu = (showZoomMenu && !presentationMode) ? 1.0f : 0.0f;
        silveranim::driveTrackedFade(zoomMenuAnim, zoomMenuVel, zoomMenuPrevTarget, targetZoomMenu, anim.chViewerPopup, dt);

        // Smooth Theme Menu Transition
        float targetThemeMenu = (showThemeMenu && !presentationMode) ? 1.0f : 0.0f;
        silveranim::driveTrackedFade(themeMenuAnim, themeMenuVel, themeMenuPrevTarget, targetThemeMenu, anim.chViewerPopup, dt);

        // Smooth Minimap Fade
        float targetMinimap = (!presentationMode && !isGridView && isZoomedIn && uiVisibilityAlpha > 0.2f) ? 1.0f : 0.0f;
        silveranim::driveFade(minimapAlpha, minimapVel, targetMinimap, anim.chViewerChrome, dt);

        // Clamp & Smooth Grid Scroll
        if (totalFiles > 0) {
            float itemH = 140.0f + 14.0f;
            int cols = std::max(1, (int)((cachedWindowW - 32.0f) / 164.0f));
            int rows = (totalFiles + cols - 1) / cols;
            float maxScroll = std::max(0.0f, rows * itemH - (cachedWindowH - topBarH - 24.0f));
            gridTargetScroll = std::clamp(gridTargetScroll, 0.0f, maxScroll);
        } else {
            gridTargetScroll = 0.0f;
        }
        silveranim::drivePos(gridScrollOffset, gridScrollVel, gridTargetScroll, anim.chViewerGridScroll, dt);

        // Clamp Filmstrip Scroll
        float maxFilmScroll = std::max(0.0f, totalFiles * (thumbW + thumbGap) - cachedWindowW + 32.0f);
        thumbs.targetScrollOffset = std::clamp(thumbs.targetScrollOffset, 0.0f, maxFilmScroll);

        // Compare each animated value against the target it was just driven
        // toward. Anything unsettled means the screen is still changing.
        const float eps = 0.002f;
        auto moving = [&](float value, float target) { return std::abs(value - target) > eps; };

        animating =
            moving(scrollBarAlpha, scrollActivityTimer > 0.0f ? 1.0f : 0.0f) ||
            moving(uiVisibilityAlpha, targetUIVisibility) ||
            moving(metaPopupAnim, targetAnim) ||
            moving(zoomMenuAnim, targetZoomMenu) ||
            moving(themeMenuAnim, targetThemeMenu) ||
            moving(minimapAlpha, targetMinimap) ||
            std::abs(gridScrollOffset - gridTargetScroll) > 0.05f ||
            std::abs(thumbs.scrollOffset - thumbs.targetScrollOffset) > 0.05f ||
            toastTimer > 0.0f ||
            scrollActivityTimer > 0.0f ||
            (isFullscreen && !presentationMode && inactivityTimer < 2.6f);
    }

    bool isInside(float mx, float my, float x, float y, float w, float h) {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }

    std::string truncateText(const std::string& str, float maxW) {
        return font.fitWithEllipsis(str, maxW);
    }

    UIAction handleMouseDown(float mx, float my, int totalFiles, int currentIdx,
                             float imgW, float imgH) {
        notifyUserActivity();
        UIAction act;

        if (presentationMode) {
            return act;
        }

        // 0. Theme Menu Popover Click Handling (Highest Priority)
        if (showThemeMenu) {
            if (isInside(mx, my, themeMenuRect.x, themeMenuRect.y, themeMenuRect.w, themeMenuRect.h)) {
                if (isInside(mx, my, themeSystemOptRect.x, themeSystemOptRect.y, themeSystemOptRect.w, themeSystemOptRect.h)) {
                    theme.setThemeMode(THEME_SYSTEM);
                    showToast("Theme: " + theme.getThemeModeName());
                    showThemeMenu = false;
                    act.type = UIAction::TOGGLE_THEME;
                    return act;
                }
                if (isInside(mx, my, themeDarkOptRect.x, themeDarkOptRect.y, themeDarkOptRect.w, themeDarkOptRect.h)) {
                    theme.setThemeMode(THEME_DARK);
                    showToast("Theme: Dark Mode");
                    showThemeMenu = false;
                    act.type = UIAction::TOGGLE_THEME;
                    return act;
                }
                if (isInside(mx, my, themeLightOptRect.x, themeLightOptRect.y, themeLightOptRect.w, themeLightOptRect.h)) {
                    theme.setThemeMode(THEME_LIGHT);
                    showToast("Theme: Light Mode");
                    showThemeMenu = false;
                    act.type = UIAction::TOGGLE_THEME;
                    return act;
                }
                return act; // Absorbed by popup
            } else if (!isInside(mx, my, btnTheme.x, btnTheme.y, btnTheme.w, btnTheme.h)) {
                showThemeMenu = false;
            }
        }

        // 1. Zoom Dropdown Menu Clicks
        if (showZoomMenu && zoomMenuAnim > 0.1f) {
            for (size_t i = 0; i < zoomMenuItemRects.size() && i < 6; ++i) {
                const auto& r = zoomMenuItemRects[i];
                if (isInside(mx, my, r.x, r.y, r.w, r.h)) {
                    showZoomMenu = false;
                    if (kZoomMenuItems[i].isFit) {
                        act.type = UIAction::TOGGLE_FIT;
                    } else {
                        act.type = UIAction::SET_ZOOM_SCALE;
                        act.zoomScaleTarget = kZoomMenuItems[i].targetScale;
                    }
                    return act;
                }
            }

            if (isInside(mx, my, zoomMenuRect.x, zoomMenuRect.y, zoomMenuRect.w, zoomMenuRect.h)) {
                return act; // Click inside menu background
            } else if (!isInside(mx, my, zoomBadgeRect.x, zoomBadgeRect.y, zoomBadgeRect.w, zoomBadgeRect.h)) {
                showZoomMenu = false; // Click outside closes menu
            }
        }

        // 2. Zoom Badge / Button Click (Toggle Menu)
        if (!isGridView && isInside(mx, my, zoomBadgeRect.x, zoomBadgeRect.y, zoomBadgeRect.w, zoomBadgeRect.h)) {
            showZoomMenu = !showZoomMenu;
            if (showZoomMenu) {
                showThemeMenu = false;
            }
            act.type = UIAction::TOGGLE_ZOOM_MENU;
            return act;
        }

        // 3. Grid View Tile Click
        if (isGridView && totalFiles > 0) {
            float gridTop = topBarH + 12.0f;
            if (my >= gridTop) {
                float itemW = 150.0f;
                float itemH = 140.0f;
                float gap = 14.0f;
                int cols = std::max(1, (int)((cachedWindowW - 32.0f) / (itemW + gap)));
                float startX = 16.0f + ((cachedWindowW - 32.0f) - (cols * (itemW + gap) - gap)) * 0.5f;

                for (int i = 0; i < totalFiles; ++i) {
                    int c = i % cols;
                    int r = i / cols;
                    float tx = startX + c * (itemW + gap);
                    float ty = gridTop - gridScrollOffset + r * (itemH + gap);

                    if (isInside(mx, my, tx, ty, itemW, itemH)) {
                        act.type = UIAction::OPEN_IMAGE_INDEX;
                        act.index = i;
                        return act;
                    }
                }
            }
        }

        // 4. Metadata Popup Interactions
        if (metadataState > 0) {
            if (isInside(mx, my, btnPopupClose.x, btnPopupClose.y, btnPopupClose.w, btnPopupClose.h)) {
                metadataState = 0;
                act.type = UIAction::CLOSE_OVERLAYS;
                return act;
            }
            if (isInside(mx, my, btnPopupExpand.x, btnPopupExpand.y, btnPopupExpand.w, btnPopupExpand.h)) {
                metadataState = (metadataState == 1) ? 2 : 1;
                act.type = UIAction::TOGGLE_METADATA_STATE;
                return act;
            }
            if (isInside(mx, my, popupCardRect.x, popupCardRect.y, popupCardRect.w, popupCardRect.h)) {
                return act;
            }
        }

        // 5. Top Right Breadcrumb Header Label Click (Toggle State 0 -> 1)
        if (isInside(mx, my, headerLabelRect.x, headerLabelRect.y, headerLabelRect.w, headerLabelRect.h)) {
            metadataState = (metadataState == 0) ? 1 : 0;
            if (metadataState > 0) {
                showThemeMenu = false;
                showZoomMenu = false;
            }
            act.type = UIAction::TOGGLE_METADATA_STATE;
            return act;
        }

        // 6. Minimap Click & Pan
        if (!isGridView && minimapAlpha > 0.1f && isInside(mx, my, miniX, miniY, miniW, miniH)) {
            isDraggingMinimap = true;
            act.type = UIAction::PAN_TO_MINIMAP_NORM;
            act.normX = (mx - miniX) / miniW;
            act.normY = (my - miniY) / miniH;
            return act;
        }

        // 7. Toolbar Buttons (Left Group)
        if (isInside(mx, my, btnFit.x, btnFit.y, btnFit.w, btnFit.h)) {
            act.type = UIAction::TOGGLE_FIT;
            return act;
        }
        if (isInside(mx, my, btn1to1.x, btn1to1.y, btn1to1.w, btn1to1.h)) {
            act.type = UIAction::SET_1TO1;
            return act;
        }
        if (isInside(mx, my, btnCenter.x, btnCenter.y, btnCenter.w, btnCenter.h)) {
            act.type = UIAction::CENTER_VIEW;
            return act;
        }
        if (isInside(mx, my, btnRotate.x, btnRotate.y, btnRotate.w, btnRotate.h)) {
            act.type = UIAction::ROTATE;
            return act;
        }
        if (isInside(mx, my, btnGridView.x, btnGridView.y, btnGridView.w, btnGridView.h)) {
            isGridView = !isGridView;
            act.type = UIAction::TOGGLE_GRID_VIEW;
            return act;
        }
        if (isInside(mx, my, btnTheme.x, btnTheme.y, btnTheme.w, btnTheme.h)) {
            showThemeMenu = !showThemeMenu;
            if (showThemeMenu) {
                showZoomMenu = false;
                metadataState = 0;
            }
            return act;
        }

        // 8. Bottom Filmstrip Click (in Single View)
        if (!isGridView && showThumbnails && totalFiles > 0) {
            float stripY = cachedWindowH - bottomStripH;
            float thumbY = stripY + (bottomStripH - thumbH) * 0.5f;

            if (my >= stripY && my <= cachedWindowH) {
                float itemStep = thumbW + thumbGap;
                float startX = 16.0f - thumbs.scrollOffset;
                for (int i = 0; i < totalFiles; ++i) {
                    float tx = startX + i * itemStep;
                    if (isInside(mx, my, tx, thumbY, thumbW, thumbH)) {
                        act.type = UIAction::SELECT_IMAGE_INDEX;
                        act.index = i;
                        return act;
                    }
                }
            }
        }

        return act;
    }

    UIAction handleMouseMove(float mx, float my) {
        notifyUserActivity();
        UIAction act;
        if (!presentationMode && isDraggingMinimap && minimapAlpha > 0.1f) {
            act.type = UIAction::PAN_TO_MINIMAP_NORM;
            float nx = std::clamp((mx - miniX) / miniW, 0.0f, 1.0f);
            float ny = std::clamp((my - miniY) / miniH, 0.0f, 1.0f);
            act.normX = nx;
            act.normY = ny;
            return act;
        }
        return act;
    }

    void handleMouseUp() {
        isDraggingMinimap = false;
    }

    void handleScroll(double yoffset, int totalFiles) {
        notifyScrollActivity();
        if (isGridView && totalFiles > 0) {
            // Smooth, linear pixel scrolling without jumping
            float step = 42.0f;
            gridTargetScroll -= (float)yoffset * step;

            int cols = std::max(1, (int)((cachedWindowW - 32.0f) / 164.0f));
            int rows = (totalFiles + cols - 1) / cols;
            float itemH = 140.0f + 14.0f;
            float maxScroll = std::max(0.0f, rows * itemH - (cachedWindowH - topBarH - 24.0f));
            gridTargetScroll = std::clamp(gridTargetScroll, 0.0f, maxScroll);
        }
    }

    void render(int windowW, int windowH,
                float mouseX, float mouseY,
                const ImageMetadata& meta,
                const std::vector<std::string>& fileList,
                int currentFileIdx,
                float zoomScale, float fitScale,
                float imgPosX, float imgPosY,
                int currentRotation,
                bool nearestFilter, bool pixelGrid,
                bool isImageDecoding = false,
                GLuint currentTextureId = 0) {

        cachedWindowW = (float)windowW;
        cachedWindowH = (float)windowH;
        const ThemePalette& pal = theme.current;
        float uiAlpha = uiVisibilityAlpha;

        if (presentationMode) {
            return; // In presentation mode, view ONLY the clean image canvas
        }

        // =============================================================
        // A. FULL THUMBNAIL GRID GALLERY VIEW
        // =============================================================
        if (isGridView) {
            font.beginBatch();
            float gridTop = topBarH + 12.0f;
            float itemW = 150.0f;
            float itemH = 140.0f;
            float gap = 14.0f;
            int cols = std::max(1, (int)((cachedWindowW - 32.0f) / (itemW + gap)));
            float startX = 16.0f + ((cachedWindowW - 32.0f) - (cols * (itemW + gap) - gap)) * 0.5f;

            for (size_t i = 0; i < fileList.size(); ++i) {
                int c = (int)i % cols;
                int r = (int)i / cols;
                float tx = startX + c * (itemW + gap);
                float ty = gridTop - gridScrollOffset + r * (itemH + gap);

                if (ty + itemH < topBarH || ty > windowH) continue;

                bool isActive = ((int)i == currentFileIdx);
                bool isHov = isInside(mouseX, mouseY, tx, ty, itemW, itemH);

                thumbs.requestThumbnail(fileList[i], true);
                font.addRoundedRect(tx, ty, itemW, itemH, 8.0f, pal.btnBg);

                if (isActive) {
                    font.addRoundedBorder(tx - 2, ty - 2, itemW + 4, itemH + 4, 10.0f, 2.5f, pal.accent);
                } else if (isHov) {
                    font.addRoundedBorder(tx - 1, ty - 1, itemW + 2, itemH + 2, 9.0f, 1.5f, pal.btnBorder);
                } else {
                    font.addRoundedBorder(tx, ty, itemW, itemH, 8.0f, 1.0f, pal.barBorder);
                }
            }
            font.render(windowW, windowH, 0, iconAtlas.textureId);

            // Thumbnail Image Tiles
            for (size_t i = 0; i < fileList.size(); ++i) {
                int c = (int)i % cols;
                int r = (int)i / cols;
                float tx = startX + c * (itemW + gap);
                float ty = gridTop - gridScrollOffset + r * (itemH + gap);

                if (ty + itemH < topBarH || ty > windowH) continue;

                auto it = thumbs.cache.find(fileList[i]);
                if (it != thumbs.cache.end() && it->second.ready && it->second.texId) {
                    font.beginBatch();
                    float pad = 6.0f;
                    float iw = itemW - 2 * pad;
                    float ih = itemH - 32.0f - pad;
                    float ix = tx + pad;
                    float iy = ty + pad;

                    UIVertex v[6] = {
                        { ix, iy, 0, 0, 1, 1, 1, 1, 2.0f },
                        { ix + iw, iy, 1, 0, 1, 1, 1, 1, 2.0f },
                        { ix + iw, iy + ih, 1, 1, 1, 1, 1, 1, 2.0f },

                        { ix, iy, 0, 0, 1, 1, 1, 1, 2.0f },
                        { ix + iw, iy + ih, 1, 1, 1, 1, 1, 1, 2.0f },
                        { ix, iy + ih, 0, 1, 1, 1, 1, 1, 2.0f },
                    };
                    font.vertices.insert(font.vertices.end(), v, v + 6);
                    font.render(windowW, windowH, it->second.texId);
                }
            }

            // Tile Filenames
            font.beginBatch();
            for (size_t i = 0; i < fileList.size(); ++i) {
                int c = (int)i % cols;
                int r = (int)i / cols;
                float tx = startX + c * (itemW + gap);
                float ty = gridTop - gridScrollOffset + r * (itemH + gap);

                if (ty + itemH < topBarH || ty > windowH) continue;

                std::string fname = fileList[i];
                size_t ls = fname.find_last_of("/\\");
                if (ls != std::string::npos) fname = fname.substr(ls + 1);

                std::string shortName = truncateText(fname, itemW - 14.0f * uiScale);
                float tw = font.measureText(shortName);
                float fx = tx + (itemW - tw) * 0.5f;
                float fy = ty + itemH - 24.0f;
                font.addText(fx, fy, shortName, ((int)i == currentFileIdx) ? pal.textAccent : pal.textPrimary);
            }

            // Modern Floating Scrollbar Pill on the Right Side
            if (scrollBarAlpha > 0.01f && fileList.size() > 0) {
                int rows = ((int)fileList.size() + cols - 1) / cols;
                float totalContentH = rows * (itemH + gap);
                float viewH = (float)windowH - topBarH - 24.0f;
                if (totalContentH > viewH) {
                    float thumbRatio = std::clamp(viewH / totalContentH, 0.08f, 0.90f);
                    float scrollbarH = viewH * thumbRatio;
                    float maxScroll = totalContentH - viewH;
                    float scrollRatio = std::clamp(gridScrollOffset / maxScroll, 0.0f, 1.0f);
                    float scrollbarY = topBarH + 12.0f + (viewH - scrollbarH) * scrollRatio;
                    float scrollbarX = (float)windowW - 7.0f;
                    float scrollbarW = 4.0f;

                    Color4 barCol = pal.accent;
                    barCol.a = 0.65f * scrollBarAlpha;
                    font.addRoundedRect(scrollbarX, scrollbarY, scrollbarW, scrollbarH, 2.0f, barCol);
                }
            }

            font.render(windowW, windowH);
        }

        // =============================================================
        // B. LOADING SHIMMER / SKELETON INDICATOR (for 60+ MP images)
        // =============================================================
        if (isImageDecoding && !isGridView) {
            font.beginBatch();
            float sw = 220.0f;
            float sh = 36.0f;
            float sx = ((float)windowW - sw) * 0.5f;
            float sy = topBarH + 20.0f;

            Color4 sBg = pal.cardBg;
            sBg.a = 0.94f;
            font.addRoundedRect(sx + 3, sy + 3, sw, sh, 18.0f, Color4(0, 0, 0, 0.35f));
            font.addRoundedRect(sx, sy, sw, sh, 18.0f, sBg);
            font.addRoundedBorder(sx, sy, sw, sh, 18.0f, 1.0f, pal.cardBorder);

            float pulse = 0.5f + 0.5f * sinf(shimmerPhase);
            Color4 pulseCol = pal.accent;
            pulseCol.a = 0.4f + 0.5f * pulse;
            font.addRoundedRect(sx + 14.0f, sy + 16.0f, 16.0f, 4.0f, 2.0f, pulseCol);

            font.render(windowW, windowH);

            font.beginBatch();
            font.addTextVCentered(sx + 40.0f * uiScale, sy, sh, "Decoding Full Resolution...", pal.textPrimary);
            font.render(windowW, windowH);
        }

        // =============================================================
        // C. TOP TOOLBAR & CONTROLS (With Presentation Fade)
        // =============================================================
        if (uiAlpha > 0.01f) {
            font.beginBatch();
            Color4 topBg = pal.barBg;
            topBg.a *= uiAlpha;
            Color4 topBorder = pal.barBorder;
            topBorder.a *= uiAlpha;

            font.addRect(0, 0, (float)windowW, topBarH, topBg);
            font.addRect(0, topBarH - 1.0f, (float)windowW, 1.0f, topBorder);

            float btnW = 32.0f;
            float btnH = std::max(26.0f * uiScale, font.textHeight() + 12.0f * uiScale);
            float btnY = (topBarH - btnH) * 0.5f;
            float curBtnX = 12.0f;
            float gap = 4.0f;

            auto setupBtn = [&](Rect& btn, float x, bool active = false) {
                btn.x = x; btn.y = btnY; btn.w = btnW; btn.h = btnH;
                btn.isHovered = isInside(mouseX, mouseY, x, btnY, btnW, btnH);
                Color4 bg = active ? pal.accent : (btn.isHovered ? pal.btnHover : Color4(0,0,0,0));
                bg.a *= uiAlpha;
                font.addRoundedRect(x, btnY, btnW, btnH, 5.0f, bg);
                if (active) {
                    Color4 ah = pal.accentHover; ah.a *= uiAlpha;
                    font.addRoundedBorder(x, btnY, btnW, btnH, 5.0f, 1.0f, ah);
                } else if (btn.isHovered) {
                    Color4 bb = pal.btnBorder; bb.a *= uiAlpha;
                    font.addRoundedBorder(x, btnY, btnW, btnH, 5.0f, 1.0f, bb);
                }
            };

            // [ ⤢ Fit ]
            bool isFit = (!isGridView && fabs(zoomScale - fitScale) < 0.01f);
            setupBtn(btnFit, curBtnX, isFit);
            Color4 cFit = isFit ? Color4(1,1,1,1) : pal.textPrimary; cFit.a *= uiAlpha;
            iconAtlas.drawIcon(font, ICON_FIT, curBtnX + 6, btnY + 5, 20, 20, cFit);
            curBtnX += btnW + gap;

            // [ 1:1 ]
            bool is1to1 = (!isGridView && fabs(zoomScale - 1.0f) < 0.01f);
            setupBtn(btn1to1, curBtnX, is1to1);
            Color4 c1 = is1to1 ? Color4(1,1,1,1) : pal.textPrimary; c1.a *= uiAlpha;
            iconAtlas.drawIcon(font, ICON_1TO1, curBtnX + 6, btnY + 5, 20, 20, c1);
            curBtnX += btnW + gap;

            // [ ⌖ Center View ]
            setupBtn(btnCenter, curBtnX);
            Color4 cC = pal.textPrimary; cC.a *= uiAlpha;
            iconAtlas.drawIcon(font, ICON_TARGET, curBtnX + 6, btnY + 5, 20, 20, cC);
            curBtnX += btnW + gap;

            // [ ⟳ Rotate ]
            setupBtn(btnRotate, curBtnX);
            Color4 cR = pal.textPrimary; cR.a *= uiAlpha;
            iconAtlas.drawIcon(font, ICON_ROTATE, curBtnX + 6, btnY + 5, 20, 20, cR);
            curBtnX += btnW + gap;

            // [ #✓ Grid Gallery View Toggle ]
            setupBtn(btnGridView, curBtnX, isGridView);
            Color4 cG = isGridView ? Color4(1,1,1,1) : pal.textPrimary; cG.a *= uiAlpha;
            iconAtlas.drawIcon(font, ICON_GRID_CHECK, curBtnX + 6, btnY + 5, 20, 20, cG);
            curBtnX += btnW + gap + 8.0f;

            // Separator
            font.addLine(curBtnX, btnY + 4, curBtnX, btnY + btnH - 4, 1.0f, topBorder);
            curBtnX += 9.0f;

            // [ ◐ Theme ]
            setupBtn(btnTheme, curBtnX, showThemeMenu);
            Color4 cT = showThemeMenu ? Color4(1, 1, 1, 1) : pal.textPrimary; cT.a *= uiAlpha;
            iconAtlas.drawIcon(font, theme.isDarkMode ? ICON_THEME_DARK : ICON_THEME_LIGHT, curBtnX + 6, btnY + 5, 20, 20, cT);
            curBtnX += btnW + gap;

            // -------------------------------------------------------------
            // D. TOP RIGHT METADATA HEADER BUTTON (State 0, Responsive)
            // -------------------------------------------------------------
            float availHeaderSpace = std::max(36.0f, (float)windowW - curBtnX - 24.0f);
            float maxTitleW = std::clamp(availHeaderSpace - 40.0f, 0.0f, 260.0f);
            std::string headerTitle = meta.fileName.empty() ? "No File" : meta.fileName;
            std::string headerTrunc = (maxTitleW > 24.0f) ? truncateText(headerTitle, maxTitleW) : "";
            float htw = headerTrunc.empty() ? 0.0f : font.measureText(headerTrunc);
            float hbw = headerTrunc.empty() ? 32.0f : (htw + 34.0f);
            float hbx = (float)windowW - hbw - 14.0f;
            float hby = (topBarH - btnH) * 0.5f;

            headerLabelRect.x = hbx; headerLabelRect.y = hby; headerLabelRect.w = hbw; headerLabelRect.h = btnH;
            headerLabelRect.isHovered = isInside(mouseX, mouseY, hbx, hby, hbw, btnH);

            Color4 headBg = (metadataState > 0) ? pal.cardHeaderBg : (headerLabelRect.isHovered ? pal.btnHover : Color4(0,0,0,0));
            headBg.a *= uiAlpha;
            font.addRoundedRect(hbx, hby, hbw, btnH, 5.0f, headBg);
            if (metadataState > 0 || headerLabelRect.isHovered) {
                Color4 hb = pal.btnBorder; hb.a *= uiAlpha;
                font.addRoundedBorder(hbx, hby, hbw, btnH, 5.0f, 1.0f, hb);
            }
            if (!headerTrunc.empty()) {
                Color4 headText = pal.textPrimary; headText.a *= uiAlpha;
                font.addTextVCentered(hbx + 10.0f * uiScale, hby, btnH, headerTrunc, headText);
            }
            Color4 cArr = pal.textSecondary; cArr.a *= uiAlpha;
            iconAtlas.drawIcon(font, (metadataState > 0) ? ICON_CHEVRON_DOWN : (headerTrunc.empty() ? ICON_INFO : ICON_CHEVRON_RIGHT),
                               headerTrunc.empty() ? (hbx + 8.0f) : (hbx + hbw - 20.0f), hby + 7.0f, 16, 16, cArr);

            // -------------------------------------------------------------
            // E. BOTTOM FILMSTRIP THUMBNAILS (Single View)
            // -------------------------------------------------------------
            float stripY = (float)windowH - bottomStripH;
            if (!isGridView && showThumbnails && !fileList.empty()) {
                Color4 sBg = pal.barBg; sBg.a *= uiAlpha;
                Color4 sBorder = pal.barBorder; sBorder.a *= uiAlpha;
                font.addRect(0, stripY, (float)windowW, bottomStripH, sBg);
                font.addRect(0, stripY, (float)windowW, 1.0f, sBorder);

                float itemStep = thumbW + thumbGap;
                float startX = 16.0f - thumbs.scrollOffset;
                float thumbY = stripY + (bottomStripH - thumbH) * 0.5f;

                for (size_t i = 0; i < fileList.size(); ++i) {
                    float tx = startX + i * itemStep;
                    if (tx + thumbW < 0 || tx > windowW) continue;

                    bool isActive = ((int)i == currentFileIdx);
                    bool isHov = isInside(mouseX, mouseY, tx, thumbY, thumbW, thumbH);

                    thumbs.requestThumbnail(fileList[i]);
                    Color4 tBg = pal.thumbBg; tBg.a *= uiAlpha;
                    font.addRoundedRect(tx, thumbY, thumbW, thumbH, 6.0f, tBg);

                    if (isActive) {
                        Color4 ab = pal.thumbActiveBorder; ab.a *= uiAlpha;
                        font.addRoundedBorder(tx - 2, thumbY - 2, thumbW + 4, thumbH + 4, 8.0f, 2.5f, ab);
                    } else if (isHov) {
                        Color4 ac = pal.accent; ac.a *= uiAlpha;
                        font.addRoundedBorder(tx - 1, thumbY - 1, thumbW + 2, thumbH + 2, 7.0f, 1.5f, ac);
                    } else {
                        Color4 bb = pal.barBorder; bb.a *= uiAlpha;
                        font.addRoundedBorder(tx, thumbY, thumbW, thumbH, 6.0f, 1.0f, bb);
                    }
                }

                // Modern Floating Scrollbar Pill at Bottom of Filmstrip
                if (scrollBarAlpha > 0.01f && fileList.size() > 0) {
                    float totalFilmW = fileList.size() * itemStep;
                    float viewW = (float)windowW - 32.0f;
                    if (totalFilmW > viewW) {
                        float thumbRatio = std::clamp(viewW / totalFilmW, 0.08f, 0.90f);
                        float scrollbarW = viewW * thumbRatio;
                        float maxScroll = totalFilmW - viewW;
                        float scrollRatio = std::clamp(thumbs.scrollOffset / maxScroll, 0.0f, 1.0f);
                        float scrollbarX = 16.0f + (viewW - scrollbarW) * scrollRatio;
                        float scrollbarY = stripY + bottomStripH - 5.0f;

                        Color4 barCol = pal.accent;
                        barCol.a = 0.65f * scrollBarAlpha * uiAlpha;
                        font.addRoundedRect(scrollbarX, scrollbarY, scrollbarW, 3.0f, 1.5f, barCol);
                    }
                }
            }

            font.render(windowW, windowH, 0, iconAtlas.textureId);

            // Render Header Text
            font.beginBatch();
            Color4 hCol = pal.textPrimary; hCol.a *= uiAlpha;
            font.addTextVCentered(hbx + 10.0f * uiScale, hby, btnH, headerTrunc, hCol);
            font.render(windowW, windowH);
        }

        // -------------------------------------------------------------
        // F. MINIMAP / NAVIGATOR GEOMETRY
        // -------------------------------------------------------------
        float boxX = 0, boxY = 0, boxW = 0, boxH = 0;
        bool hasMinimap = (!isGridView && minimapAlpha > 0.01f && meta.width > 0 && meta.height > 0);
        float stripY = (float)windowH - bottomStripH;

        if (hasMinimap) {
            float imgW = (float)meta.width;
            float imgH = (float)meta.height;
            int rotMod = ((currentRotation % 360) + 360) % 360;
            if (rotMod == 90 || rotMod == 270) std::swap(imgW, imgH);

            miniW = 164.0f;
            miniH = (miniW * imgH) / imgW;
            if (miniH > 115.0f) {
                miniH = 115.0f;
                miniW = (miniH * imgW) / imgH;
            }

            miniX = (float)windowW - miniW - 16.0f;
            miniY = stripY - miniH - 16.0f;

            Color4 mBg = pal.minimapBg;
            mBg.a *= minimapAlpha;
            Color4 mBorder = pal.minimapBorder;
            mBorder.a *= minimapAlpha;

            font.beginBatch();
            font.addRoundedRect(miniX + 3, miniY + 3, miniW, miniH, 4.0f, Color4(0, 0, 0, 0.45f * minimapAlpha));
            font.addRoundedRect(miniX, miniY, miniW, miniH, 4.0f, mBg);
            font.addRoundedBorder(miniX, miniY, miniW, miniH, 4.0f, 1.0f, mBorder);
            font.render(windowW, windowH);

            // Compute Viewport Box
            float curRenderW = imgW * zoomScale;
            float curRenderH = imgH * zoomScale;
            float imgScreenX0 = imgPosX - curRenderW * 0.5f;
            float imgScreenY0 = imgPosY - curRenderH * 0.5f;

            float normX0 = std::clamp((0.0f - imgScreenX0) / curRenderW, 0.0f, 1.0f);
            float normY0 = std::clamp((topBarH - imgScreenY0) / curRenderH, 0.0f, 1.0f);
            float normX1 = std::clamp(((float)windowW - imgScreenX0) / curRenderW, 0.0f, 1.0f);
            float normY1 = std::clamp(((float)windowH - (showThumbnails ? bottomStripH : 0.0f) - imgScreenY0) / curRenderH, 0.0f, 1.0f);

            boxX = miniX + normX0 * miniW;
            boxY = miniY + normY0 * miniH;
            boxW = std::clamp((normX1 - normX0) * miniW, 6.0f, miniW - (boxX - miniX));
            boxH = std::clamp((normY1 - normY0) * miniH, 6.0f, miniH - (boxY - miniY));

            if (currentTextureId > 0) {
                font.beginBatch();
                float ix = miniX;
                float iy = miniY;
                float iw = miniW;
                float ih = miniH;
                float imgAlpha = 0.70f * minimapAlpha;

                float u0 = 0.0f, v0 = 0.0f;
                float u1 = 1.0f, v0_1 = 0.0f;
                float u2 = 1.0f, v2 = 1.0f;
                float u3 = 0.0f, v3 = 1.0f;

                if (rotMod == 90) {
                    u0 = 0.0f; v0 = 1.0f; u1 = 0.0f; v0_1 = 0.0f; u2 = 1.0f; v2 = 0.0f; u3 = 1.0f; v3 = 1.0f;
                } else if (rotMod == 180) {
                    u0 = 1.0f; v0 = 1.0f; u1 = 0.0f; v0_1 = 1.0f; u2 = 0.0f; v2 = 0.0f; u3 = 1.0f; v3 = 0.0f;
                } else if (rotMod == 270) {
                    u0 = 1.0f; v0 = 0.0f; u1 = 1.0f; v0_1 = 1.0f; u2 = 0.0f; v2 = 1.0f; u3 = 0.0f; v3 = 0.0f;
                }

                UIVertex mv[6] = {
                    { ix,      iy,      u0, v0,   1, 1, 1, imgAlpha, 2.0f },
                    { ix + iw, iy,      u1, v0_1, 1, 1, 1, imgAlpha, 2.0f },
                    { ix + iw, iy + ih, u2, v2,   1, 1, 1, imgAlpha, 2.0f },

                    { ix,      iy,      u0, v0,   1, 1, 1, imgAlpha, 2.0f },
                    { ix + iw, iy + ih, u2, v2,   1, 1, 1, imgAlpha, 2.0f },
                    { ix,      iy + ih, u3, v3,   1, 1, 1, imgAlpha, 2.0f },
                };
                font.vertices.insert(font.vertices.end(), mv, mv + 6);
                font.render(windowW, windowH, currentTextureId);

                // Overlay Viewport Highlight Box
                font.beginBatch();
                Color4 boxBg = pal.minimapViewportBox;
                boxBg.a = 0.20f * minimapAlpha;
                font.addRect(boxX, boxY, boxW, boxH, boxBg);

                Color4 boxBorder = pal.minimapViewportBox;
                boxBorder.a = 0.95f * minimapAlpha;
                font.addBorder(boxX, boxY, boxW, boxH, 1.5f, boxBorder);
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // G. ZOOM BADGE (Click to Open Zoom Dropdown Menu)
        // -------------------------------------------------------------
        if (!isGridView && uiAlpha > 0.01f) {
            char zoomBuf[32];
            float zoomPercentExact = zoomScale * 100.0f;
            if (fabs(zoomPercentExact - roundf(zoomPercentExact)) < 0.05f) {
                snprintf(zoomBuf, sizeof(zoomBuf), "%d%%", (int)(zoomPercentExact + 0.5f));
            } else {
                snprintf(zoomBuf, sizeof(zoomBuf), "%.1f%%", zoomPercentExact);
            }
            std::string zoomStr = zoomBuf;

            float zw = font.measureText(zoomStr) + 38.0f;
            float zh = 28.0f;
            float zx = 0, zy = 0;

            if (hasMinimap) {
                zx = (float)windowW - zw - 16.0f;
                zy = miniY - zh - 6.0f;
            } else {
                zx = (float)windowW - zw - 16.0f;
                zy = stripY - zh - 10.0f;
            }

            zoomBadgeRect.x = zx; zoomBadgeRect.y = zy; zoomBadgeRect.w = zw; zoomBadgeRect.h = zh;
            zoomBadgeRect.isHovered = isInside(mouseX, mouseY, zx, zy, zw, zh);

            font.beginBatch();
            Color4 zBg = (zoomBadgeRect.isHovered || showZoomMenu) ? pal.btnHover : pal.cardBg;
            zBg.a *= uiAlpha;
            Color4 zBorder = (zoomBadgeRect.isHovered || showZoomMenu) ? pal.accent : pal.cardBorder;
            zBorder.a *= uiAlpha;

            font.addRoundedRect(zx, zy, zw, zh, 6.0f, zBg);
            font.addRoundedBorder(zx, zy, zw, zh, 6.0f, 1.0f, zBorder);
            Color4 cArr = pal.textSecondary; cArr.a *= uiAlpha;
            iconAtlas.drawIcon(font, showZoomMenu ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN,
                               zx + zw - 18.0f, zy + 6.0f, 14, 14, cArr);
            font.render(windowW, windowH, 0, iconAtlas.textureId);

            font.beginBatch();
            Color4 zText = pal.textPrimary; zText.a *= uiAlpha;
            font.addTextVCentered(zx + 9.0f * uiScale, zy, zh, zoomStr, zText);
            font.render(windowW, windowH);

            // =========================================================
            // ZOOM DROPDOWN MENU (Direct Zoom Selection)
            // =========================================================
            if (zoomMenuAnim > 0.01f) {
                float menuW = 168.0f;
                float rowH = std::max(28.0f * uiScale, font.textHeight() + 14.0f * uiScale);
                float menuH = 6 * rowH + 16.0f;

                float menuX = zx + zw - menuW;
                float menuY = zy - menuH - 8.0f;

                zoomMenuRect.x = menuX; zoomMenuRect.y = menuY; zoomMenuRect.w = menuW; zoomMenuRect.h = menuH;
                zoomMenuItemRects.clear();

                font.beginBatch();
                Color4 mCardBg = pal.isDark ? Color4::Hex(0x18191E, 0.98f) : Color4::Hex(0xFFFFFF, 0.98f);
                mCardBg.a *= zoomMenuAnim;
                Color4 mCardBorder = pal.isDark ? Color4::Hex(0x2E303C, 1.0f) : Color4::Hex(0xD0D7DE, 1.0f);
                mCardBorder.a *= zoomMenuAnim;

                // Menu Background & Shadow
                font.addRoundedRect(menuX + 3, menuY + 3, menuW, menuH, 12.0f, Color4(0, 0, 0, 0.40f * zoomMenuAnim));
                font.addRoundedRect(menuX, menuY, menuW, menuH, 12.0f, mCardBg);
                font.addRoundedBorder(menuX, menuY, menuW, menuH, 12.0f, 1.0f, mCardBorder);

                float curItemY = menuY + 8.0f;
                for (int i = 0; i < 6; ++i) {
                    Rect itemR;
                    itemR.x = menuX + 6.0f;
                    itemR.y = curItemY;
                    itemR.w = menuW - 12.0f;
                    itemR.h = rowH;
                    itemR.isHovered = isInside(mouseX, mouseY, itemR.x, itemR.y, itemR.w, itemR.h);
                    zoomMenuItemRects.push_back(itemR);

                    if (itemR.isHovered) {
                        Color4 hBg = pal.btnHover; hBg.a *= zoomMenuAnim;
                        font.addRoundedRect(itemR.x, itemR.y, itemR.w, itemR.h, 6.0f, hBg);
                    }
                    curItemY += rowH;
                }
                font.render(windowW, windowH);

                // Menu Text Labels
                font.beginBatch();
                curItemY = menuY + 8.0f;
                for (int i = 0; i < 6; ++i) {
                    Color4 tLabel = pal.textPrimary; tLabel.a *= zoomMenuAnim;
                    Color4 tShortcut = pal.textSecondary; tShortcut.a *= zoomMenuAnim;

                    font.addTextVCentered(menuX + 16.0f * uiScale, curItemY, rowH, kZoomMenuItems[i].label, tLabel);
                    if (kZoomMenuItems[i].shortcut && kZoomMenuItems[i].shortcut[0] != '\0') {
                        float sw = font.measureText(kZoomMenuItems[i].shortcut);
                        font.addTextVCentered(menuX + menuW - sw - 16.0f * uiScale, curItemY, rowH, kZoomMenuItems[i].shortcut, tShortcut);
                    }
                    curItemY += rowH;
                }
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // H. METADATA POPUP CARDS WITH FULL MULTI-LINE PATH SUPPORT
        // -------------------------------------------------------------
        if (metaPopupAnim > 0.01f) {
            float cardW = 320.0f * uiScale;

            // The value column starts after the widest label, so the wrap width
            // used for measuring has to be derived the same way the drawing code
            // derives it - otherwise the card is sized for a different layout
            // than the one that gets painted.
            float metaPad = 12.0f * uiScale;
            float metaLabelColW = font.measureText("Perceived type") + 16.0f * uiScale;
            float availValW = cardW - metaPad * 2.0f - metaLabelColW;

            int pathLines = 1;
            if (!meta.filePath.empty()) {
                std::string testLine = "";
                pathLines = 0;
                for (char c : meta.filePath) {
                    if (font.measureText(testLine + c) > availValW) {
                        pathLines++;
                        testLine = std::string(1, c);
                    } else {
                        testLine += c;
                    }
                }
                if (!testLine.empty()) pathLines++;
                if (pathLines < 1) pathLines = 1;
            }

            // Exactly compute how many rows are in the full expanded view
            int expandedRowCount = 12; // Base expanded attributes
            if (meta.hasExif) {
                if (!meta.exif.Make.empty() || !meta.exif.Model.empty()) expandedRowCount++;
                if (meta.exif.FocalLength > 0.0f) expandedRowCount++;
                if (meta.exif.FNumber > 0.0f) expandedRowCount++;
                if (meta.exif.ISOSpeedRatings > 0) expandedRowCount++;
                if (meta.exif.ExposureTime > 0.0) expandedRowCount++;
            }

            float rowH = font.lineHeight();
            float pathBlockH = std::max(1, pathLines) * 18.0f + 4.0f;
            float compactContentH = pathBlockH + 4 * rowH;
            float fullContentH = compactContentH + expandedRowCount * rowH;

            float compactH = 34.0f + 10.0f + compactContentH + 10.0f;
            float fullH = 34.0f + 10.0f + fullContentH + 10.0f;

            float curH = (metaPopupAnim <= 1.0f)
                ? (compactH * metaPopupAnim)
                : (compactH + (fullH - compactH) * (metaPopupAnim - 1.0f));

            float cardX = (float)windowW - cardW - 14.0f;
            float cardY = topBarH + 6.0f;
            popupCardRect.x = cardX; popupCardRect.y = cardY; popupCardRect.w = cardW; popupCardRect.h = curH;

            font.beginBatch();
            font.addRoundedRect(cardX + 4, cardY + 4, cardW, curH, 8.0f, Color4(0, 0, 0, 0.40f * std::min(1.0f, metaPopupAnim)));
            font.addRoundedRect(cardX, cardY, cardW, curH, 8.0f, pal.cardBg);
            font.addRoundedBorder(cardX, cardY, cardW, curH, 8.0f, 1.0f, pal.cardBorder);

            // Header Bar
            float hdrH = 34.0f;
            Color4 hdrBg = pal.isDark ? Color4::Hex(0x133038, 0.95f) : Color4::Hex(0xDBEAFE, 0.95f);
            font.addRoundedRect(cardX, cardY, cardW, hdrH, 8.0f, hdrBg);
            font.addRect(cardX, cardY + hdrH - 6.0f, cardW, 6.0f, hdrBg);
            font.addRect(cardX, cardY + hdrH, cardW, 1.0f, pal.cardBorder);

            float bx1 = cardX + cardW - 28.0f;
            float bx2 = cardX + cardW - 54.0f;
            btnPopupClose.x = bx1; btnPopupClose.y = cardY + 4.0f; btnPopupClose.w = 24.0f; btnPopupClose.h = 24.0f;
            btnPopupExpand.x = bx2; btnPopupExpand.y = cardY + 4.0f; btnPopupExpand.w = 24.0f; btnPopupExpand.h = 24.0f;

            btnPopupClose.isHovered = isInside(mouseX, mouseY, btnPopupClose.x, btnPopupClose.y, btnPopupClose.w, btnPopupClose.h);
            btnPopupExpand.isHovered = isInside(mouseX, mouseY, btnPopupExpand.x, btnPopupExpand.y, btnPopupExpand.w, btnPopupExpand.h);

            if (btnPopupClose.isHovered) font.addRoundedRect(btnPopupClose.x, btnPopupClose.y, 24.0f, 24.0f, 4.0f, pal.btnHover);
            if (btnPopupExpand.isHovered) font.addRoundedRect(btnPopupExpand.x, btnPopupExpand.y, 24.0f, 24.0f, 4.0f, pal.btnHover);

            iconAtlas.drawIcon(font, ICON_CLOSE, btnPopupClose.x + 5, btnPopupClose.y + 5, 14, 14, pal.textSecondary);
            iconAtlas.drawIcon(font, (metadataState == 2) ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN,
                               btnPopupExpand.x + 5, btnPopupExpand.y + 5, 14, 14, pal.textSecondary);

            font.render(windowW, windowH, 0, iconAtlas.textureId);

            // Text Rows
            font.beginBatch();
            // All of these were fixed pixel values that only lined up at the
            // original font size. Derive them from the text metrics instead.
            float pad = metaPad;
            float lineH = font.lineHeight();
            float labelColW = metaLabelColW;

            std::string pTitle = truncateText(meta.fileName.empty() ? "Properties" : meta.fileName,
                                              popupCardRect.w - pad * 2.0f);
            font.addText(cardX + pad, cardY + pad * 0.6f, pTitle, pal.textPrimary);

            float rowX = cardX + pad;
            float valX = cardX + pad + labelColW;
            float rowY = cardY + pad * 0.6f + lineH * 1.6f;
            float cardBottom = cardY + popupCardRect.h;

            // 1. Path (Multi-line full display)
            if (rowY + lineH <= cardBottom) {
                font.addText(rowX, rowY, "Path", pal.textPrimary);
                int lines = font.addWrappedText(valX, rowY, meta.filePath, availValW, lineH, pal.textSecondary);
                rowY += std::max(1, lines) * lineH + 4.0f * uiScale;
            }

            auto drawRow = [&](const std::string& label, const std::string& val) {
                if (rowY + lineH > cardBottom) return;
                font.addText(rowX, rowY, label, pal.textPrimary);
                std::string tVal = truncateText(val, popupCardRect.w - (valX - cardX) - pad);
                font.addText(valX, rowY, tVal, pal.textSecondary);
                rowY += rowH;
            };

            drawRow("Type", meta.fileTypeStr);
            drawRow("Size", meta.fileSizeFormatted);
            drawRow("Created", meta.createdTime.empty() ? "Unknown" : meta.createdTime);
            drawRow("Modified", meta.modifiedTime.empty() ? "Unknown" : meta.modifiedTime);

            if (metaPopupAnim > 1.05f) {
                drawRow("Attributes", meta.attributesStr);
                drawRow("Date accessed", meta.accessedTime.empty() ? "Unknown" : meta.accessedTime);
                drawRow("Content type", meta.mimeType);
                drawRow("Width", std::to_string(meta.width) + " pixels");
                drawRow("Height", std::to_string(meta.height) + " pixels");
                drawRow("Horizontal res", std::to_string(meta.dpiX) + " dpi");
                drawRow("Vertical res", std::to_string(meta.dpiY) + " dpi");
                drawRow("Bit depth", std::to_string(meta.bitDepth));
                drawRow("Dimensions", meta.dimensionsStr);
                drawRow("Owner", meta.ownerStr);
                drawRow("Computer", meta.computerStr);
                drawRow("Perceived type", meta.perceivedType);

                if (meta.hasExif) {
                    if (!meta.exif.Make.empty() || !meta.exif.Model.empty()) {
                        drawRow("Camera", meta.exif.Make + " " + meta.exif.Model);
                    }
                    if (meta.exif.FocalLength > 0.0f) {
                        char fBuf[32]; snprintf(fBuf, sizeof(fBuf), "%.1f mm", meta.exif.FocalLength);
                        drawRow("Focal length", fBuf);
                    }
                    if (meta.exif.FNumber > 0.0f) {
                        char aBuf[32]; snprintf(aBuf, sizeof(aBuf), "f/%.1f", meta.exif.FNumber);
                        drawRow("Aperture", aBuf);
                    }
                    if (meta.exif.ISOSpeedRatings > 0) {
                        drawRow("ISO speed", "ISO " + std::to_string(meta.exif.ISOSpeedRatings));
                    }
                    if (meta.exif.ExposureTime > 0.0) {
                        char eBuf[32];
                        if (meta.exif.ExposureTime < 1.0) snprintf(eBuf, sizeof(eBuf), "1/%d s", (int)(1.0 / meta.exif.ExposureTime + 0.5));
                        else snprintf(eBuf, sizeof(eBuf), "%.2f s", meta.exif.ExposureTime);
                        drawRow("Exposure", eBuf);
                    }
                }
            }
            font.render(windowW, windowH);
        }

        // -------------------------------------------------------------
        // I. MODERN FLOATING TOAST CAPSULE (Anti-Aliased Rounded Pill)
        // -------------------------------------------------------------
        if (!toastMsg.empty() && !presentationMode) {
            float tw = font.measureText(toastMsg) + 36.0f;
            float th = 34.0f;
            float tx = ((float)windowW - tw) * 0.5f;
            float ty = topBarH + 16.0f;
            float pillRad = 17.0f;

            font.beginBatch();
            // Soft drop shadow
            font.addRoundedRect(tx + 2, ty + 2, tw, th, pillRad, Color4(0, 0, 0, 0.35f));
            // Pill background
            font.addRoundedRect(tx, ty, tw, th, pillRad, pal.toastBg);
            // Smooth rounded border matching pill curve (NO sharp corners!)
            font.addRoundedBorder(tx, ty, tw, th, pillRad, 1.0f, pal.toastBorder);
            font.render(windowW, windowH);

            font.beginBatch();
            font.addTextVCentered(tx + 18.0f * uiScale, ty, th, toastMsg, pal.textAccent);
            font.render(windowW, windowH);
        }

        // -------------------------------------------------------------
        // J. BOTTOM FILMSTRIP THUMBNAILS (Single View)
        // -------------------------------------------------------------
        if (!isGridView && showThumbnails && uiAlpha > 0.01f && !fileList.empty()) {
            float itemStep = thumbW + thumbGap;
            float startX = 16.0f - thumbs.scrollOffset;
            float thumbY = stripY + (bottomStripH - thumbH) * 0.5f;

            for (size_t i = 0; i < fileList.size(); ++i) {
                float tx = startX + i * itemStep;
                if (tx + thumbW < 0 || tx > windowW) continue;

                auto it = thumbs.cache.find(fileList[i]);
                if (it != thumbs.cache.end() && it->second.ready && it->second.texId) {
                    font.beginBatch();
                    float pad = 3.0f;
                    float iw = thumbW - 2 * pad;
                    float ih = thumbH - 2 * pad;
                    float ix = tx + pad;
                    float iy = thumbY + pad;

                    UIVertex v[6] = {
                        { ix, iy, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, uiAlpha, 2.0f },
                        { ix + iw, iy, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, uiAlpha, 2.0f },
                        { ix + iw, iy + ih, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, uiAlpha, 2.0f },

                        { ix, iy, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, uiAlpha, 2.0f },
                        { ix + iw, iy + ih, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, uiAlpha, 2.0f },
                        { ix, iy + ih, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, uiAlpha, 2.0f },
                    };
                    font.vertices.insert(font.vertices.end(), v, v + 6);
                    font.render(windowW, windowH, it->second.texId);
                }
            }
        }

        // -------------------------------------------------------------
        // K. THEME MENU POPOVER (Auto System, Dark, Light)
        // -------------------------------------------------------------
        if (themeMenuAnim > 0.002f) {
            float tmW = 160.0f;
            float tmH = 118.0f;
            float tmX = std::clamp(btnTheme.x + btnTheme.w - tmW, 12.0f, (float)windowW - tmW - 14.0f);
            float tmY = topBarH + 6.0f + (1.0f - themeMenuAnim) * (-8.0f);

            themeMenuRect.x = tmX;
            themeMenuRect.y = topBarH + 6.0f;
            themeMenuRect.w = tmW;
            themeMenuRect.h = tmH;

            float tmAlpha = themeMenuAnim * uiAlpha;

            // Background & Shadow
            font.beginBatch();
            font.addRoundedRect(tmX + 3, tmY + 4, tmW, tmH, 8.0f, Color4(0, 0, 0, 0.50f * tmAlpha));
            font.addRoundedRect(tmX, tmY, tmW, tmH, 8.0f, pal.isDark ? Color4::Hex(0x181A20, 0.98f * tmAlpha) : Color4::Hex(0xFFFFFF, 0.98f * tmAlpha));
            font.addRoundedBorder(tmX, tmY, tmW, tmH, 8.0f, 1.0f, pal.isDark ? Color4::Hex(0x2D323E, tmAlpha) : Color4::Hex(0xDFE2E8, tmAlpha));
            font.render(windowW, windowH);

            struct ThemeOpt {
                ThemeMode m;
                IconType icon;
                const char* label;
                Rect* rect;
            };
            ThemeOpt opts[3] = {
                { THEME_SYSTEM, ICON_REFRESH, "System (Auto)", &themeSystemOptRect },
                { THEME_DARK, ICON_THEME_DARK, "Dark Mode", &themeDarkOptRect },
                { THEME_LIGHT, ICON_THEME_LIGHT, "Light Mode", &themeLightOptRect }
            };

            float optX = tmX + 6.0f;
            float optW = tmW - 12.0f;
            float optH = 32.0f;
            float optY = tmY + 8.0f;

            for (int oi = 0; oi < 3; ++oi) {
                ThemeOpt& to = opts[oi];
                to.rect->x = optX;
                to.rect->y = optY;
                to.rect->w = optW;
                to.rect->h = optH;
                to.rect->isHovered = isInside(mouseX, mouseY, optX, optY, optW, optH);

                bool isActive = (theme.mode.load() == to.m);

                if (isActive || to.rect->isHovered) {
                    font.beginBatch();
                    Color4 bgCol = isActive ? pal.accent : (pal.isDark ? Color4::Hex(0x282B34, 0.85f * tmAlpha) : Color4::Hex(0xEEF0F4, 0.85f * tmAlpha));
                    font.addRoundedRect(optX, optY, optW, optH, 6.0f, bgCol);
                    font.render(windowW, windowH);
                }

                Color4 iconCol = isActive ? Color4(1, 1, 1, tmAlpha) : (to.rect->isHovered ? pal.textPrimary : pal.textSecondary);
                font.beginBatch();
                iconAtlas.drawIcon(font, to.icon, optX + 8.0f, optY + 7.0f, 18.0f, 18.0f, iconCol);
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                Color4 textCol = isActive ? Color4(1, 1, 1, tmAlpha) : pal.textPrimary;
                font.beginBatch();
                font.addTextVCentered(optX + 32.0f * uiScale, optY, optH, to.label, textCol);
                font.render(windowW, windowH);

                optY += optH + 3.0f;
            }
        }
    }
};
