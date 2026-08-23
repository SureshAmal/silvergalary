#pragma once

#include "db.h"
#include "scanner.h"
#include "timeline.h"
#include "../viewer_linux/theme.h"
#include "../viewer_linux/font.h"
#include "../viewer_linux/icons.h"
#include "../viewer_linux/thumbnails.h"
#include "../viewer_linux/image_loader.h"
#include "../viewer_linux/async_loader.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <iostream>

enum GalleryTab {
    TAB_ALL = 0,
    TAB_TIMELINE,
    TAB_FAVORITES,
    TAB_FOLDERS
};

struct GalleryUIAction {
    enum Type {
        NONE = 0,
        SELECT_IMAGE,
        OPEN_FULLSCREEN,
        CLOSE_FULLSCREEN,
        PREV_IMAGE,
        NEXT_IMAGE,
        OPEN_IN_VIEWER,
        TOGGLE_STAR,
        CLOSE_SIDEBAR,
        START_SCAN,
        TOGGLE_THEME,
        SWITCH_TAB,
        SELECT_FOLDER,
        CLEAR_FOLDER_FILTER,
        COPY_PATH
    };
    Type type = NONE;
    std::string targetPath;
    std::string folderFilter;
    int tabIndex = 0;
};

class GalleryUI {
public:
    GalleryTab currentTab = TAB_ALL;
    std::string searchQuery;
    std::string activeFolderFilter;
    bool isSearching = false;

    // Selection & Sidebar
    bool showSidebar = false;
    float sidebarWidth = 340.0f;
    std::string selectedPath;
    GalleryRecord selectedRecord;

    // Mobile / Desktop In-Gallery Full-Screen Mode
    bool isFullScreenView = false;
    int fullScreenIndex = 0;
    float fsZoom = 1.0f;
    float fsPanX = 0.0f;
    float fsPanY = 0.0f;
    bool showFsInfo = false;
    bool isFsDragging = false;
    float fsDragStartX = 0.0f;
    float fsDragStartY = 0.0f;
    float fsOrigPanX = 0.0f;
    float fsOrigPanY = 0.0f;

    // Scrolling & Momentum
    float scrollY = 0.0f;
    float targetScrollY = 0.0f;
    float scrollVelocity = 0.0f;
    float scrollbarAlpha = 0.0f;

    // Interactive Rectangles
    struct UIRect {
        float x = 0, y = 0, w = 0, h = 0;
        bool isHovered = false;
    };

    UIRect tabRects[4];
    UIRect scanBtnRect;
    UIRect themeBtnRect;
    UIRect scrubberRect;
    UIRect breadcrumbClearRect;

    // Sidebar Rectangles
    UIRect sbCloseRect;
    UIRect sbPreviewRect;
    UIRect sbFavBtnRect;
    UIRect sbFullscreenBtnRect;
    UIRect sbViewerBtnRect;
    UIRect sbCopyPathRect;

    // FullScreen Mode Rectangles
    UIRect fsBackBtnRect;
    UIRect fsFavBtnRect;
    UIRect fsInfoBtnRect;
    UIRect fsViewerBtnRect;
    UIRect fsCloseBtnRect;
    UIRect fsPrevBtnRect;
    UIRect fsNextBtnRect;

    std::vector<UIRect> folderItemRects;
    std::vector<GalleryDatabase::FolderStats> folderList;

    // Zoom Popup & Vertical Slider Rectangles
    bool showZoomPopup = false;
    bool isDraggingZoomSlider = false;
    float zoomPopupAutoCloseTimer = 0.0f;
    UIRect zoomPillBtnRect;
    UIRect zoomPopupRect;
    UIRect presetXLRect;
    UIRect presetLargeRect;
    UIRect presetMediumRect;
    UIRect presetSmallRect;
    UIRect zoomSliderTrackRect;
    UIRect zoomSliderThumbRect;

    // Theme Mode Selector Menu & Toast
    bool showThemeMenu = false;
    float themeMenuAnim = 0.0f;
    UIRect themeMenuRect;
    UIRect themeSystemOptRect;
    UIRect themeDarkOptRect;
    UIRect themeLightOptRect;
    float themeToastTimer = 0.0f;
    std::string themeToastText = "";

    bool isDraggingScrubber = false;
    float refreshAnim = 0.0f;
    ThemeManager theme;
    ThumbnailManager thumbs;
    AsyncImagePreloader fullResLoader;

    // Smooth Animation States
    float sidebarAnim = 0.0f;       // 0.0 = closed, 1.0 = open
    float tabAnimX = 0.0f;          // Smooth sliding active tab indicator X
    float tabAnimW = 0.0f;          // Smooth sliding active tab indicator W
    float fsAnim = 0.0f;            // 0.0 = grid view, 1.0 = fullscreen lightbox
    float zoomPopupAnim = 0.0f;     // 0.0 = hidden, 1.0 = popup visible

    // High resolution preview texture for sidebar and fullscreen
    ImageTexture highResPreview;

    // Double-click detection
    int lastClickIndex = -1;
    double lastClickTimeSec = 0.0;

    static std::string fitTextWithEllipsis(FontRenderer& font, const std::string& text, float maxW) {
        if (text.empty() || font.measureText(text) <= maxW) return text;

        // If it's a filename with an extension, preserve the extension and ellipsize the middle
        size_t dotPos = text.find_last_of('.');
        if (dotPos != std::string::npos && dotPos > 0 && (text.length() - dotPos) <= 8) {
            std::string ext = text.substr(dotPos); // e.g. ".png"
            std::string stem = text.substr(0, dotPos);
            std::string ell = "...";
            float extW = font.measureText(ell + ext);
            if (extW < maxW) {
                int stemLen = (int)stem.length();
                while (stemLen > 1) {
                    std::string candidate = stem.substr(0, stemLen) + ell + ext;
                    if (font.measureText(candidate) <= maxW) {
                        return candidate;
                    }
                    stemLen--;
                }
                return stem.substr(0, 1) + ell + ext;
            }
        }

        // Standard end truncation with ellipsis
        std::string ell = "...";
        std::string res = text;
        while (!res.empty()) {
            if (font.measureText(res + ell) <= maxW) {
                return res + ell;
            }
            res.pop_back();
        }
        return ell;
    }

    GalleryUI() {
        theme.init();
        thumbs.init();
        fullResLoader.init();
    }

    void update(float dt, float totalContentHeight, float windowH, bool isScanning) {
        theme.update(dt);
        thumbs.updateGL();

        // Check if full resolution image is ready to upload to OpenGL without blocking main thread
        if (!selectedPath.empty()) {
            auto preloaded = fullResLoader.getIfReady(selectedPath);
            if (preloaded && preloaded->data && (!highResPreview.id || highResPreview.meta.filePath != selectedPath)) {
                highResPreview.uploadPixels(preloaded->data, preloaded->width, preloaded->height, preloaded->meta);
            }
        }

        if (isScanning) {
            refreshAnim += dt * 6.0f;
        }

        // 1. Smooth scroll interpolation
        float scrollLerp = 1.0f - std::exp(-20.0f * dt);
        float diff = targetScrollY - scrollY;
        scrollY += diff * scrollLerp;

        // Clamp scroll bounds
        float maxScroll = std::max(0.0f, totalContentHeight - windowH);
        if (targetScrollY < 0.0f) targetScrollY = 0.0f;
        if (targetScrollY > maxScroll) targetScrollY = maxScroll;
        if (scrollY < 0.0f) scrollY = 0.0f;
        if (scrollY > maxScroll) scrollY = maxScroll;

        // Fade scrollbar
        if (std::abs(diff) > 1.0f || isDraggingScrubber) {
            scrollbarAlpha = std::min(1.0f, scrollbarAlpha + dt * 8.0f);
        } else {
            scrollbarAlpha = std::max(0.0f, scrollbarAlpha - dt * 2.0f);
        }

        // 2. Sidebar slide animation
        float targetSb = (showSidebar && !selectedPath.empty()) ? 1.0f : 0.0f;
        sidebarAnim += (targetSb - sidebarAnim) * (1.0f - std::exp(-20.0f * dt));
        if (std::abs(sidebarAnim - targetSb) < 0.001f) {
            sidebarAnim = targetSb;
            if (!showSidebar) {
                selectedPath.clear();
            }
        }

        // 3. Fullscreen lightbox transition animation
        float targetFs = isFullScreenView ? 1.0f : 0.0f;
        fsAnim += (targetFs - fsAnim) * (1.0f - std::exp(-20.0f * dt));
        if (std::abs(fsAnim - targetFs) < 0.001f) fsAnim = targetFs;

        // 4. Zoom Popup HUD animation & auto-close timer
        if (zoomPopupAutoCloseTimer > 0.0f) {
            zoomPopupAutoCloseTimer -= dt;
            if (zoomPopupAutoCloseTimer <= 0.0f) {
                zoomPopupAutoCloseTimer = 0.0f;
                if (!isDraggingZoomSlider) {
                    showZoomPopup = false;
                }
            }
        }
        float targetZoom = (showZoomPopup || isDraggingZoomSlider) ? 1.0f : 0.0f;
        zoomPopupAnim += (targetZoom - zoomPopupAnim) * (1.0f - std::exp(-24.0f * dt));
        if (std::abs(zoomPopupAnim - targetZoom) < 0.001f) zoomPopupAnim = targetZoom;

        // 5. Theme Mode Menu animation & toast timer
        float targetThemeMenu = showThemeMenu ? 1.0f : 0.0f;
        themeMenuAnim += (targetThemeMenu - themeMenuAnim) * (1.0f - std::exp(-24.0f * dt));
        if (std::abs(themeMenuAnim - targetThemeMenu) < 0.001f) themeMenuAnim = targetThemeMenu;

        if (themeToastTimer > 0.0f) {
            themeToastTimer -= dt;
            if (themeToastTimer < 0.0f) themeToastTimer = 0.0f;
        }

        // 6. Sliding Tab Indicator
        if (currentTab >= 0 && currentTab < 4 && tabRects[currentTab].w > 0.0f) {
            float targetTabX = tabRects[currentTab].x;
            float targetTabW = tabRects[currentTab].w;
            if (tabAnimW <= 0.0f) {
                tabAnimX = targetTabX;
                tabAnimW = targetTabW;
            } else {
                float tabLerp = 1.0f - std::exp(-24.0f * dt);
                tabAnimX += (targetTabX - tabAnimX) * tabLerp;
                tabAnimW += (targetTabW - tabAnimW) * tabLerp;
            }
        }
    }

    void handleScroll(float dy, float totalContentHeight, float windowH) {
        if (isFullScreenView) {
            // Zoom in/out in fullscreen
            float zoomDelta = dy * 0.15f;
            float newZoom = std::clamp(fsZoom + zoomDelta, 0.2f, 10.0f);
            fsZoom = newZoom;
            return;
        }

        float maxScroll = std::max(0.0f, totalContentHeight - windowH);
        float step = 64.0f;
        targetScrollY -= dy * step;
        targetScrollY = std::clamp(targetScrollY, 0.0f, maxScroll);
        scrollbarAlpha = 1.0f;
    }

    void scrollToItem(float itemY, float itemH, float totalContentHeight, float windowH) {
        float topBarH = 64.0f;
        float bottomMargin = 28.0f;
        float maxScroll = std::max(0.0f, totalContentHeight - windowH);

        // If item is above visible viewport
        if (itemY < targetScrollY + topBarH + 8.0f) {
            targetScrollY = std::max(0.0f, itemY - topBarH - 16.0f);
        }
        // If item is below visible viewport
        else if (itemY + itemH > targetScrollY + windowH - bottomMargin) {
            targetScrollY = std::min(maxScroll, (itemY + itemH + bottomMargin) - windowH);
        }
        targetScrollY = std::clamp(targetScrollY, 0.0f, maxScroll);
        scrollbarAlpha = 1.0f;
    }

    static bool isInside(float mx, float my, float x, float y, float w, float h) {
        return (mx >= x && mx <= x + w && my >= y && my <= y + h);
    }

    void selectPhoto(const GalleryRecord& rec, TimelineManager& timeline, float windowH = 0.0f) {
        selectedPath = rec.path;
        selectedRecord = rec;
        showSidebar = true;
        timeline.selectItem(rec.path);
        thumbs.requestThumbnail(rec.path, true);
        fullResLoader.requestPrimaryImage(rec.path);

        if (windowH > 0.0f && timeline.selectedFlatIndex >= 0 && timeline.selectedFlatIndex < (int)timeline.flatAllItems.size()) {
            const auto* itm = timeline.flatAllItems[timeline.selectedFlatIndex];
            scrollToItem(itm->y, itm->h, timeline.totalContentHeight, windowH);
        }
    }

    void openFullScreen(int index, const std::vector<GalleryRecord>& records, TimelineManager& timeline) {
        if (index < 0 || index >= (int)records.size()) return;
        fullScreenIndex = index;
        isFullScreenView = true;
        fsZoom = 1.0f;
        fsPanX = 0.0f;
        fsPanY = 0.0f;
        selectedPath = records[index].path;
        selectedRecord = records[index];
        timeline.selectItem(selectedPath);
        thumbs.requestThumbnail(selectedPath, true);
        fullResLoader.requestPrimaryImage(selectedPath);
    }

    GalleryUIAction handleMouseDown(float mx, float my, TimelineManager& timeline,
                                   const std::vector<GalleryRecord>& records,
                                   float windowW, float windowH) {
        GalleryUIAction act;

        // -------------------------------------------------------------
        // 1. FULL-SCREEN IN-GALLERY VIEW CLICKS
        // -------------------------------------------------------------
        if (isFullScreenView) {
            // Back Button (← Back)
            if (isInside(mx, my, fsBackBtnRect.x, fsBackBtnRect.y, fsBackBtnRect.w, fsBackBtnRect.h)) {
                isFullScreenView = false;
                act.type = GalleryUIAction::CLOSE_FULLSCREEN;
                return act;
            }
            // Close Button (✕)
            if (isInside(mx, my, fsCloseBtnRect.x, fsCloseBtnRect.y, fsCloseBtnRect.w, fsCloseBtnRect.h)) {
                isFullScreenView = false;
                act.type = GalleryUIAction::CLOSE_FULLSCREEN;
                return act;
            }
            // Favorite Toggle
            if (isInside(mx, my, fsFavBtnRect.x, fsFavBtnRect.y, fsFavBtnRect.w, fsFavBtnRect.h)) {
                act.type = GalleryUIAction::TOGGLE_STAR;
                act.targetPath = selectedPath;
                selectedRecord.starred = 1 - selectedRecord.starred;
                return act;
            }
            // Info Toggle
            if (isInside(mx, my, fsInfoBtnRect.x, fsInfoBtnRect.y, fsInfoBtnRect.w, fsInfoBtnRect.h)) {
                showFsInfo = !showFsInfo;
                return act;
            }
            // Standalone Viewer Button
            if (isInside(mx, my, fsViewerBtnRect.x, fsViewerBtnRect.y, fsViewerBtnRect.w, fsViewerBtnRect.h)) {
                act.type = GalleryUIAction::OPEN_IN_VIEWER;
                act.targetPath = selectedPath;
                return act;
            }
            // Prev Button (<)
            if (isInside(mx, my, fsPrevBtnRect.x, fsPrevBtnRect.y, fsPrevBtnRect.w, fsPrevBtnRect.h)) {
                if (fullScreenIndex > 0) {
                    openFullScreen(fullScreenIndex - 1, records, timeline);
                }
                return act;
            }
            // Next Button (>)
            if (isInside(mx, my, fsNextBtnRect.x, fsNextBtnRect.y, fsNextBtnRect.w, fsNextBtnRect.h)) {
                if (fullScreenIndex + 1 < (int)records.size()) {
                    openFullScreen(fullScreenIndex + 1, records, timeline);
                }
                return act;
            }

            // Start Dragging Pan in FullScreen
            isFsDragging = true;
            fsDragStartX = mx;
            fsDragStartY = my;
            fsOrigPanX = fsPanX;
            fsOrigPanY = fsPanY;
            return act;
        }

        // -------------------------------------------------------------
        // 1.5 THEME MENU OPTION CLICKS & POPOVER (Highest Priority)
        // -------------------------------------------------------------
        if (showThemeMenu) {
            if (isInside(mx, my, themeMenuRect.x, themeMenuRect.y, themeMenuRect.w, themeMenuRect.h)) {
                if (isInside(mx, my, themeSystemOptRect.x, themeSystemOptRect.y, themeSystemOptRect.w, themeSystemOptRect.h)) {
                    theme.setThemeMode(THEME_SYSTEM);
                    themeToastText = "Theme: " + theme.getThemeModeName();
                    themeToastTimer = 2.0f;
                    showThemeMenu = false;
                    act.type = GalleryUIAction::TOGGLE_THEME;
                    return act;
                }
                if (isInside(mx, my, themeDarkOptRect.x, themeDarkOptRect.y, themeDarkOptRect.w, themeDarkOptRect.h)) {
                    theme.setThemeMode(THEME_DARK);
                    themeToastText = "Theme: Dark Mode";
                    themeToastTimer = 2.0f;
                    showThemeMenu = false;
                    act.type = GalleryUIAction::TOGGLE_THEME;
                    return act;
                }
                if (isInside(mx, my, themeLightOptRect.x, themeLightOptRect.y, themeLightOptRect.w, themeLightOptRect.h)) {
                    theme.setThemeMode(THEME_LIGHT);
                    themeToastText = "Theme: Light Mode";
                    themeToastTimer = 2.0f;
                    showThemeMenu = false;
                    act.type = GalleryUIAction::TOGGLE_THEME;
                    return act;
                }
                return act; // Absorbed by popup
            } else if (!isInside(mx, my, themeBtnRect.x, themeBtnRect.y, themeBtnRect.w, themeBtnRect.h)) {
                showThemeMenu = false;
            }
        }

        // -------------------------------------------------------------
        // 2. RIGHT SIDEBAR CLICKS
        // -------------------------------------------------------------
        if (showSidebar && !selectedPath.empty()) {
            float sbX = windowW - sidebarWidth;
            if (mx >= sbX) {
                // Close Sidebar Button
                if (isInside(mx, my, sbCloseRect.x, sbCloseRect.y, sbCloseRect.w, sbCloseRect.h)) {
                    showSidebar = false;
                    timeline.clearSelection();
                    float gridW = (float)windowW;
                    bool hasBanner = (!activeFolderFilter.empty() && currentTab != TAB_FOLDERS);
                    timeline.buildTimeline(records, gridW, hasBanner);
                    act.type = GalleryUIAction::CLOSE_SIDEBAR;
                    return act;
                }
                // Preview Card Click -> Open In-App Fullscreen
                if (isInside(mx, my, sbPreviewRect.x, sbPreviewRect.y, sbPreviewRect.w, sbPreviewRect.h) ||
                    isInside(mx, my, sbFullscreenBtnRect.x, sbFullscreenBtnRect.y, sbFullscreenBtnRect.w, sbFullscreenBtnRect.h)) {
                    int idx = timeline.selectedFlatIndex;
                    if (idx < 0) {
                        for (size_t i = 0; i < records.size(); ++i) {
                            if (records[i].path == selectedPath) { idx = (int)i; break; }
                        }
                    }
                    openFullScreen(idx >= 0 ? idx : 0, records, timeline);
                    act.type = GalleryUIAction::OPEN_FULLSCREEN;
                    return act;
                }
                // Favorite Toggle
                if (isInside(mx, my, sbFavBtnRect.x, sbFavBtnRect.y, sbFavBtnRect.w, sbFavBtnRect.h)) {
                    act.type = GalleryUIAction::TOGGLE_STAR;
                    act.targetPath = selectedPath;
                    selectedRecord.starred = 1 - selectedRecord.starred;
                    return act;
                }
                // Standalone Viewer Button
                if (isInside(mx, my, sbViewerBtnRect.x, sbViewerBtnRect.y, sbViewerBtnRect.w, sbViewerBtnRect.h)) {
                    act.type = GalleryUIAction::OPEN_IN_VIEWER;
                    act.targetPath = selectedPath;
                    return act;
                }
                // Copy Path Button
                if (isInside(mx, my, sbCopyPathRect.x, sbCopyPathRect.y, sbCopyPathRect.w, sbCopyPathRect.h)) {
                    act.type = GalleryUIAction::COPY_PATH;
                    act.targetPath = selectedPath;
                    return act;
                }
                return act; // Absorbed by sidebar
            }
        }

        // -------------------------------------------------------------
        // 2.5. ZOOM PILL BUTTON & POPUP MENU / VERTICAL SLIDER
        // -------------------------------------------------------------
        if (isInside(mx, my, zoomPillBtnRect.x, zoomPillBtnRect.y, zoomPillBtnRect.w, zoomPillBtnRect.h)) {
            showZoomPopup = !showZoomPopup;
            if (showZoomPopup) {
                showThemeMenu = false;
                zoomPopupAutoCloseTimer = 3.5f;
            }
            return act;
        }

        if (showZoomPopup) {
            if (isInside(mx, my, zoomPopupRect.x, zoomPopupRect.y, zoomPopupRect.w, zoomPopupRect.h)) {
                zoomPopupAutoCloseTimer = 3.5f;
                float gridW = (showSidebar && windowW >= 750) ? (windowW - sidebarWidth) : windowW;
                bool hasBanner = (!activeFolderFilter.empty() && currentTab != TAB_FOLDERS);

                // Slider Track Drag
                if (isInside(mx, my, zoomSliderTrackRect.x, zoomSliderTrackRect.y, zoomSliderTrackRect.w, zoomSliderTrackRect.h)) {
                    isDraggingZoomSlider = true;
                    float ratio = 1.0f - std::clamp((my - zoomSliderTrackRect.y) / zoomSliderTrackRect.h, 0.0f, 1.0f);
                    timeline.setZoomScale(ratio, gridW, records, hasBanner);
                    return act;
                }

                // Check Presets
                if (isInside(mx, my, presetXLRect.x, presetXLRect.y, presetXLRect.w, presetXLRect.h)) {
                    timeline.setPreset(PRESET_XL, gridW, records, hasBanner);
                    return act;
                }
                if (isInside(mx, my, presetLargeRect.x, presetLargeRect.y, presetLargeRect.w, presetLargeRect.h)) {
                    timeline.setPreset(PRESET_LARGE, gridW, records, hasBanner);
                    return act;
                }
                if (isInside(mx, my, presetMediumRect.x, presetMediumRect.y, presetMediumRect.w, presetMediumRect.h)) {
                    timeline.setPreset(PRESET_MEDIUM, gridW, records, hasBanner);
                    return act;
                }
                if (isInside(mx, my, presetSmallRect.x, presetSmallRect.y, presetSmallRect.w, presetSmallRect.h)) {
                    timeline.setPreset(PRESET_SMALL, gridW, records, hasBanner);
                    return act;
                }
                return act; // Event absorbed by popup
            } else {
                showZoomPopup = false;
            }
        }

        // -------------------------------------------------------------
        // 3. TOP BAR TAB & BUTTON CLICKS
        // -------------------------------------------------------------
        for (int i = 0; i < 4; ++i) {
            if (isInside(mx, my, tabRects[i].x, tabRects[i].y, tabRects[i].w, tabRects[i].h)) {
                currentTab = (GalleryTab)i;
                showSidebar = false;
                timeline.clearSelection();
                act.type = GalleryUIAction::SWITCH_TAB;
                act.tabIndex = i;
                targetScrollY = 0.0f;
                return act;
            }
        }

        // Scan / Refresh Button
        if (isInside(mx, my, scanBtnRect.x, scanBtnRect.y, scanBtnRect.w, scanBtnRect.h)) {
            act.type = GalleryUIAction::START_SCAN;
            return act;
        }

        // Theme Toggle Click (Opens Menu)
        if (isInside(mx, my, themeBtnRect.x, themeBtnRect.y, themeBtnRect.w, themeBtnRect.h)) {
            showThemeMenu = !showThemeMenu;
            if (showThemeMenu) {
                showZoomPopup = false;
            }
            return act;
        }

        // Folder Breadcrumb Clear Filter Button
        if (!activeFolderFilter.empty() && isInside(mx, my, breadcrumbClearRect.x, breadcrumbClearRect.y, breadcrumbClearRect.w, breadcrumbClearRect.h)) {
            activeFolderFilter.clear();
            currentTab = TAB_FOLDERS;
            showSidebar = false;
            timeline.clearSelection();
            act.type = GalleryUIAction::CLEAR_FOLDER_FILTER;
            targetScrollY = 0.0f;
            return act;
        }

        // -------------------------------------------------------------
        // 4. TIMELINE SCRUBBER DRAG
        // -------------------------------------------------------------
        if (isInside(mx, my, scrubberRect.x - 10, scrubberRect.y, scrubberRect.w + 20, scrubberRect.h)) {
            isDraggingScrubber = true;
            float ratio = std::clamp((my - scrubberRect.y) / scrubberRect.h, 0.0f, 1.0f);
            targetScrollY = timeline.getScrollForScrubRatio(ratio, windowH);
            return act;
        }

        // -------------------------------------------------------------
        // 5. FOLDERS TAB ITEM CLICKS
        // -------------------------------------------------------------
        if (currentTab == TAB_FOLDERS) {
            for (size_t i = 0; i < folderItemRects.size() && i < folderList.size(); ++i) {
                if (isInside(mx, my, folderItemRects[i].x, folderItemRects[i].y, folderItemRects[i].w, folderItemRects[i].h)) {
                    activeFolderFilter = folderList[i].folder;
                    currentTab = TAB_ALL;
                    showSidebar = false;
                    timeline.clearSelection();
                    act.type = GalleryUIAction::SELECT_FOLDER;
                    act.folderFilter = folderList[i].folder;
                    targetScrollY = 0.0f;
                    return act;
                }
            }
            return act;
        }

        // -------------------------------------------------------------
        // 6. PHOTO TILE CLICKS
        // -------------------------------------------------------------
        TimelineItem* itm = timeline.getItemAt(mx, my, scrollY);
        if (itm) {
            float curW = (itm->animW > 0.0f) ? itm->animW : itm->w;
            float curX = (itm->animW > 0.0f) ? itm->animX : itm->x;
            float curY = (itm->animH > 0.0f) ? itm->animY : itm->y;

            float starSize = 28.0f;
            float starX = curX + curW - starSize - 6.0f;
            float starY = (curY - scrollY) + 6.0f;

            if (isInside(mx, my, starX, starY, starSize, starSize)) {
                act.type = GalleryUIAction::TOGGLE_STAR;
                act.targetPath = itm->record.path;
                itm->record.starred = 1 - itm->record.starred;
                if (selectedPath == itm->record.path) {
                    selectedRecord.starred = itm->record.starred;
                }
                return act;
            } else {
                auto now = std::chrono::steady_clock::now();
                double nowSec = std::chrono::duration<double>(now.time_since_epoch()).count();
                if (itm->flatIndex == lastClickIndex && (nowSec - lastClickTimeSec) < 0.35) {
                    // Double click: open directly in full-screen in-gallery lightbox!
                    openFullScreen(itm->flatIndex, records, timeline);
                    act.type = GalleryUIAction::OPEN_FULLSCREEN;
                    lastClickIndex = -1;
                    lastClickTimeSec = 0.0;
                    return act;
                }
                lastClickIndex = itm->flatIndex;
                lastClickTimeSec = nowSec;

                // Single click: Select and open in FilePilot right sidebar (or fullscreen on narrow screens)
                if (windowW < 750.0f) {
                    openFullScreen(itm->flatIndex, records, timeline);
                    act.type = GalleryUIAction::OPEN_FULLSCREEN;
                } else {
                    selectPhoto(itm->record, timeline);
                    act.type = GalleryUIAction::SELECT_IMAGE;
                    act.targetPath = itm->record.path;
                }
                return act;
            }
        }

        // Click on background closes sidebar and expands grid to full window width smoothly
        if (showSidebar && mx < windowW - sidebarWidth && my > 70.0f) {
            showSidebar = false;
            timeline.clearSelection();
            float gridW = (float)windowW;
            bool hasBanner = (!activeFolderFilter.empty() && currentTab != TAB_FOLDERS);
            timeline.buildTimeline(records, gridW, hasBanner);
            act.type = GalleryUIAction::CLOSE_SIDEBAR;
            return act;
        }

        return act;
    }

    void handleMouseDrag(float mx, float my, TimelineManager& timeline, float windowW, float windowH,
                         const std::vector<GalleryRecord>& records) {
        if (isFullScreenView && isFsDragging) {
            fsPanX = fsOrigPanX + (mx - fsDragStartX);
            fsPanY = fsOrigPanY + (my - fsDragStartY);
            return;
        }

        if (isDraggingZoomSlider && zoomSliderTrackRect.h > 0.0f) {
            float gridW = (showSidebar && windowW >= 750) ? (windowW - sidebarWidth) : windowW;
            bool hasBanner = (!activeFolderFilter.empty() && currentTab != TAB_FOLDERS);
            float ratio = 1.0f - std::clamp((my - zoomSliderTrackRect.y) / zoomSliderTrackRect.h, 0.0f, 1.0f);
            timeline.setZoomScale(ratio, gridW, records, hasBanner);
            return;
        }

        if (isDraggingScrubber) {
            float ratio = std::clamp((my - scrubberRect.y) / scrubberRect.h, 0.0f, 1.0f);
            targetScrollY = timeline.getScrollForScrubRatio(ratio, windowH);
        }
    }

    void handleMouseUp() {
        isDraggingScrubber = false;
        isFsDragging = false;
        isDraggingZoomSlider = false;
    }

    void render(int windowW, int windowH, float mouseX, float mouseY,
                TimelineManager& timeline, GalleryDatabase& db, GalleryScanner& scanner,
                FontRenderer& font, IconAtlas& iconAtlas,
                const std::vector<GalleryRecord>& records) {
        const ThemePalette& pal = theme.current;

        // -------------------------------------------------------------
        // ================= FULLSCREEN IN-GALLERY VIEW ================
        // -------------------------------------------------------------
        if (fsAnim >= 0.999f) {
            renderFullScreenView(windowW, windowH, mouseX, mouseY, records, font, iconAtlas, 1.0f);
            return;
        }

        float topBarH = 60.0f;
        float curGridAreaW = (windowW >= 750) ? ((float)windowW - sidebarAnim * sidebarWidth) : (float)windowW;

        // -------------------------------------------------------------
        // A. TIMELINE PHOTO TILES
        // -------------------------------------------------------------
        if (currentTab != TAB_FOLDERS) {
            // Folder Breadcrumb banner if folder filter is active
            if (!activeFolderFilter.empty()) {
                float bcY = topBarH + 8.0f - scrollY;
                float bcW = curGridAreaW - timeline.sidePadding * 2.0f;
                float bcH = 40.0f;

                float btnW = font.measureText("Back to Folders") + 40.0f;
                float btnH = 30.0f;
                breadcrumbClearRect.x = timeline.sidePadding + bcW - btnW - 6.0f;
                breadcrumbClearRect.y = bcY + (bcH - btnH) * 0.5f;
                breadcrumbClearRect.w = btnW;
                breadcrumbClearRect.h = btnH;
                breadcrumbClearRect.isHovered = isInside(mouseX, mouseY, breadcrumbClearRect.x, breadcrumbClearRect.y, breadcrumbClearRect.w, breadcrumbClearRect.h);

                font.beginBatch();
                font.addRoundedRect(timeline.sidePadding, bcY, bcW, bcH, 8.0f, pal.cardBg);
                font.addRoundedBorder(timeline.sidePadding, bcY, bcW, bcH, 8.0f, 1.0f, pal.accent);
                if (breadcrumbClearRect.isHovered) {
                    font.addRoundedRect(breadcrumbClearRect.x, breadcrumbClearRect.y, breadcrumbClearRect.w, breadcrumbClearRect.h, 6.0f, pal.btnHover);
                } else {
                    font.addRoundedRect(breadcrumbClearRect.x, breadcrumbClearRect.y, breadcrumbClearRect.w, breadcrumbClearRect.h, 6.0f, pal.toastBg);
                    font.addRoundedBorder(breadcrumbClearRect.x, breadcrumbClearRect.y, breadcrumbClearRect.w, breadcrumbClearRect.h, 6.0f, 1.0f, pal.toastBorder);
                }
                font.render(windowW, windowH);

                font.beginBatch();
                std::string fName = activeFolderFilter;
                size_t lastSlash = fName.find_last_of("/\\");
                if (lastSlash != std::string::npos) fName = fName.substr(lastSlash + 1);
                std::string bcLabel = "Folder: " + fName + " (" + std::to_string(timeline.totalPhotoCount) + (timeline.totalPhotoCount == 1 ? " photo)" : " photos)");
                font.addText(timeline.sidePadding + 14.0f, bcY + 11.0f, bcLabel, pal.textPrimary);
                font.render(windowW, windowH);

                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_ARROW_LEFT, breadcrumbClearRect.x + 8.0f, breadcrumbClearRect.y + 8.0f, 14.0f, 14.0f, pal.accent);
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                font.addText(breadcrumbClearRect.x + 26.0f, breadcrumbClearRect.y + 6.5f, "Back to Folders", pal.accent);
                font.render(windowW, windowH);
            }

            std::vector<std::string> visiblePaths;
            for (auto* itm : timeline.flatVisibleItems) {
                if (!itm->isVisible) continue;
                visiblePaths.push_back(itm->record.path);
            }

            // Preload visible thumbnails
            if (!visiblePaths.empty()) {
                thumbs.preloadFolder(visiblePaths, 0);
            }

            // 1. Draw Section Headers
            for (const auto& sec : timeline.sections) {
                float secY = sec.startY - scrollY;
                if (secY + sec.height < topBarH || secY > windowH) continue;

                font.beginBatch();
                font.addText(timeline.sidePadding, secY + 12.0f, sec.title, pal.textPrimary);
                float titleW = font.measureText(sec.title);
                font.addText(timeline.sidePadding + titleW + 16.0f, secY + 13.0f, sec.subtitle, pal.textSecondary);
                font.render(windowW, windowH);
            }

            // 2. Draw Photo Tiles (With Aspect-Fill Center-Crop & Smooth Motion Physics!)
            for (auto* itm : timeline.flatVisibleItems) {
                if (!itm->isVisible) continue;

                float rx = (itm->animW > 0.0f) ? itm->animX : itm->x;
                float ry = ((itm->animH > 0.0f) ? itm->animY : itm->y) - scrollY;
                float rw = (itm->animW > 0.0f) ? itm->animW : itm->w;
                float rh = (itm->animH > 0.0f) ? itm->animH : itm->h;

                if (ry + rh < topBarH || ry > windowH) continue;

                // Smooth hover lift motion
                float lift = itm->hoverAnim * 3.5f;
                rx -= lift * 0.5f;
                ry -= lift * 0.5f;
                rw += lift;
                rh += lift;

                // Tile Background / Animated Elevation Shadow
                font.beginBatch();
                float shadowAlpha = 0.20f + 0.16f * itm->hoverAnim;
                font.addRoundedRect(rx + 2.0f, ry + 2.0f + lift, rw, rh, 8.0f, Color4(0, 0, 0, shadowAlpha));
                font.addRoundedRect(rx, ry, rw, rh, 8.0f, pal.cardBg);
                font.render(windowW, windowH);

                // Thumbnail Texture with Aspect-Fill UV Calculation
                auto it = thumbs.cache.find(itm->record.path);
                if (it != thumbs.cache.end() && it->second.ready && it->second.texId) {
                    font.beginBatch();
                    float pad = 2.0f;
                    float ix = rx + pad;
                    float iy = ry + pad;
                    float iw = rw - pad * 2.0f;
                    float ih = rh - pad * 2.0f;

                    // Perfect center-crop UV mapping without distortion
                    float tw = (float)it->second.width;
                    float th = (float)it->second.height;
                    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                    if (tw > 0.0f && th > 0.0f) {
                        float aspect = tw / th;
                        if (aspect > 1.0f) {
                            float crop = (1.0f - (1.0f / aspect)) * 0.5f;
                            u0 = crop;
                            u1 = 1.0f - crop;
                        } else if (aspect < 1.0f) {
                            float crop = (1.0f - aspect) * 0.5f;
                            v0 = crop;
                            v1 = 1.0f - crop;
                        }
                    }

                    UIVertex v[6] = {
                        { ix, iy, u0, v0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                        { ix + iw, iy, u1, v0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                        { ix + iw, iy + ih, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },

                        { ix, iy, u0, v0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                        { ix + iw, iy + ih, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                        { ix, iy + ih, u0, v1, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                    };
                    font.vertices.insert(font.vertices.end(), v, v + 6);
                    font.render(windowW, windowH, it->second.texId);
                } else {
                    // Loading skeleton pulse placeholder with smooth breathing animation
                    float pulse = 0.70f + 0.30f * std::sin((float)glfwGetTime() * 4.0f);
                    font.beginBatch();
                    Color4 skelCol = pal.isDark ? Color4::Hex(0x1F222C, pulse * 0.9f) : Color4::Hex(0xE5E7EB, pulse * 0.9f);
                    font.addRoundedRect(rx + 2, ry + 2, rw - 4, rh - 4, 6.0f, skelCol);
                    font.render(windowW, windowH);
                }

                // Smooth Selected / Hover Accent Border
                float activeBorderAlpha = std::max(itm->selectAnim, itm->hoverAnim);
                if (activeBorderAlpha > 0.01f) {
                    font.beginBatch();
                    float bThickness = itm->selectAnim > 0.5f ? 3.0f : 2.0f;
                    font.addRoundedBorder(rx, ry, rw, rh, 8.0f, bThickness, Color4(pal.accent.r, pal.accent.g, pal.accent.b, activeBorderAlpha));
                    font.render(windowW, windowH);
                }

                // Filename overlay gradient on hover (smooth alpha fade)
                if (itm->hoverAnim > 0.01f) {
                    font.beginBatch();
                    font.addRoundedRect(rx, ry + rh - 30.0f, rw, 30.0f, 8.0f, Color4(0, 0, 0, 0.72f * itm->hoverAnim));
                    font.render(windowW, windowH);

                    font.beginBatch();
                    std::string fName = itm->record.filename;
                    if (font.measureText(fName) > rw - 16.0f) {
                        while (fName.length() > 3 && font.measureText(fName + "...") > rw - 16.0f) {
                            fName.pop_back();
                        }
                        fName += "...";
                    }
                    font.addText(rx + 8.0f, ry + rh - 22.0f, fName, Color4(1.0f, 1.0f, 1.0f, itm->hoverAnim));
                    font.render(windowW, windowH);
                }

                // Favorite / Star Icon Badge (smooth alpha fade)
                float starAlpha = itm->record.starred ? 1.0f : itm->hoverAnim;
                if (starAlpha > 0.01f) {
                    float starSize = 26.0f;
                    float starX = rx + rw - starSize - 6.0f;
                    float starY = ry + 6.0f;

                    font.beginBatch();
                    Color4 sBg = itm->record.starred ? Color4::Hex(0xEF4444, 0.95f * starAlpha) : Color4(0, 0, 0, 0.60f * starAlpha);
                    font.addRoundedRect(starX, starY, starSize, starSize, 13.0f, sBg);
                    font.render(windowW, windowH);

                    font.beginBatch();
                    iconAtlas.drawIcon(font, itm->record.starred ? ICON_HEART_FILLED : ICON_HEART, starX + 5.0f, starY + 5.0f, 16.0f, 16.0f,
                                       itm->record.starred ? Color4(1.0f, 1.0f, 1.0f, starAlpha) : Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, starAlpha));
                    font.render(windowW, windowH, 0, iconAtlas.textureId);
                }
            }

            // Empty State for Favorites
            if (currentTab == TAB_FAVORITES && records.empty()) {
                float cx = curGridAreaW * 0.5f;
                float cy = (windowH + topBarH) * 0.45f;
                font.beginBatch();
                std::string noFavTitle = "No Favorite Photos";
                std::string noFavSub = "Click the heart icon on any photo to add it here";
                float nw = font.measureText(noFavTitle);
                float sw = font.measureText(noFavSub);
                font.addText(cx - nw * 0.5f, cy, noFavTitle, pal.textPrimary);
                font.addText(cx - sw * 0.5f, cy + 28.0f, noFavSub, pal.textSecondary);
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // B. FOLDERS TAB VIEW (Responsive Auto-Scaling Symmetrical Grid)
        // -------------------------------------------------------------
        if (currentTab == TAB_FOLDERS) {
            folderList = db.getFolderStats();
            folderItemRects.clear();

            float sidePad = 24.0f;
            float availW = curGridAreaW - sidePad * 2.0f;
            float fGap = 16.0f;

            // Responsive card scaling based on zoom scale
            float zoomFactor = 0.8f + timeline.zoomScale * 0.6f; // 0.8 to 1.4
            float targetCardW = 280.0f * zoomFactor;
            int fCols = std::max(1, (int)((availW + fGap) / (targetCardW + fGap)));
            float fCardW = (availW - (fCols - 1) * fGap) / (float)fCols;
            float fCardH = 80.0f + (zoomFactor - 1.0f) * 16.0f;
            float fStartY = topBarH + 24.0f - scrollY;

            int totalRows = (int)((folderList.size() + fCols - 1) / fCols);
            float totalFoldersH = fStartY + scrollY + totalRows * (fCardH + fGap) + 40.0f;
            timeline.totalContentHeight = std::max((float)windowH, totalFoldersH);

            for (size_t i = 0; i < folderList.size(); ++i) {
                int col = (int)(i % fCols);
                int row = (int)(i / fCols);
                float fx = sidePad + col * (fCardW + fGap);
                float fy = fStartY + row * (fCardH + fGap);

                UIRect fr;
                fr.x = fx; fr.y = fy; fr.w = fCardW; fr.h = fCardH;
                fr.isHovered = isInside(mouseX, mouseY, fx, fy, fCardW, fCardH);
                folderItemRects.push_back(fr);

                if (fy + fCardH < topBarH || fy > windowH) continue;

                // Card Background & Elevation
                font.beginBatch();
                font.addRoundedRect(fx + 2, fy + 2, fCardW, fCardH, 10.0f, Color4(0, 0, 0, 0.25f));
                font.addRoundedRect(fx, fy, fCardW, fCardH, 10.0f, fr.isHovered ? pal.btnHover : pal.cardBg);
                font.addRoundedBorder(fx, fy, fCardW, fCardH, 10.0f, 1.0f, fr.isHovered ? pal.accent : pal.cardBorder);
                font.render(windowW, windowH);

                // Folder Icon
                font.beginBatch();
                float iconSize = 32.0f;
                float iconY = fy + (fCardH - iconSize) * 0.5f;
                iconAtlas.drawIcon(font, ICON_FOLDER, fx + 16.0f, iconY, iconSize, iconSize, pal.accent);
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                std::string folderName = folderList[i].folder;
                size_t lastSlash = folderName.find_last_of("/\\");
                if (lastSlash != std::string::npos) folderName = folderName.substr(lastSlash + 1);

                std::string dispName = fitTextWithEllipsis(font, folderName, fCardW - 74.0f);
                font.addText(fx + 58.0f, fy + 16.0f, dispName, pal.textPrimary);

                std::string countStr = std::to_string(folderList[i].count) + (folderList[i].count == 1 ? " photo" : " photos");
                font.addText(fx + 58.0f, fy + 42.0f, countStr + "  ->", pal.accent);
                font.render(windowW, windowH);
            }

            if (folderList.empty()) {
                float cx = curGridAreaW * 0.5f;
                float cy = (windowH + topBarH) * 0.45f;
                font.beginBatch();
                std::string emptyTitle = "No Folders Found";
                std::string emptySub = "Scan your Pictures or Home directory to discover photo folders";
                float nw = font.measureText(emptyTitle);
                float sw = font.measureText(emptySub);
                font.addText(cx - nw * 0.5f, cy, emptyTitle, pal.textPrimary);
                font.addText(cx - sw * 0.5f, cy + 28.0f, emptySub, pal.textSecondary);
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // C. RIGHT-SIDE FAST TIMELINE SCRUBBER
        // -------------------------------------------------------------
        if (currentTab != TAB_FOLDERS && timeline.totalContentHeight > windowH && (!showSidebar || windowW >= 1100)) {
            float scH = windowH - topBarH - 80.0f;
            float scW = 16.0f;
            float scX = curGridAreaW - scW - 8.0f;
            float scY = topBarH + 40.0f;

            scrubberRect.x = scX; scrubberRect.y = scY; scrubberRect.w = scW; scrubberRect.h = scH;

            font.beginBatch();
            font.addRoundedRect(scX + 4, scY, 8.0f, scH, 4.0f, Color4(0, 0, 0, 0.15f * scrollbarAlpha));
            font.addRoundedBorder(scX + 4, scY, 8.0f, scH, 4.0f, 1.0f, Color4(1, 1, 1, 0.1f * scrollbarAlpha));

            // Scrubber Handle
            float handleRatio = scrollY / std::max(1.0f, timeline.totalContentHeight - windowH);
            float handleY = scY + handleRatio * (scH - 30.0f);
            font.addRoundedRect(scX + 1, handleY, 14.0f, 30.0f, 7.0f, pal.accent);
            font.render(windowW, windowH);

            // Floating Year/Month Bubble Tooltip on Scrub
            if (isDraggingScrubber) {
                int actSecIdx = timeline.getActiveStickySection(scrollY);
                std::string bText = (actSecIdx >= 0 && actSecIdx < (int)timeline.sections.size())
                    ? timeline.sections[actSecIdx].title
                    : "Timeline";

                float bw = font.measureText(bText) + 24.0f;
                float bh = 32.0f;
                float bx = scX - bw - 12.0f;
                float by = handleY;

                font.beginBatch();
                font.addRoundedRect(bx + 2, by + 2, bw, bh, 6.0f, Color4(0, 0, 0, 0.4f));
                font.addRoundedRect(bx, by, bw, bh, 6.0f, pal.toastBg);
                font.addRoundedBorder(bx, by, bw, bh, 6.0f, 1.0f, pal.toastBorder);
                font.render(windowW, windowH);

                font.beginBatch();
                font.addText(bx + 12.0f, by + 8.0f, bText, pal.textAccent);
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // D. FILEPILOT RIGHT SIDEBAR PREVIEW COLUMN (Animated Slide)
        // -------------------------------------------------------------
        if (sidebarAnim > 0.002f && windowW >= 750) {
            renderSidebar(windowW, windowH, mouseX, mouseY, font, iconAtlas);
        }

        // -------------------------------------------------------------
        // E. TOP MODERN GLASS HEADER & NAVIGATION BAR (Responsive)
        // -------------------------------------------------------------
        font.beginBatch();
        font.addRect(0, topBarH, (float)windowW, 4.0f, Color4(0, 0, 0, 0.15f));
        font.addRect(0, 0, (float)windowW, topBarH, pal.barBg);
        font.addRect(0, topBarH - 1.0f, (float)windowW, 1.0f, pal.cardBorder);
        font.render(windowW, windowH);

        // 1. Right Controls (Scan / Rescan + Theme Toggle)
        float rightMargin = 14.0f;
        float btnSize = 32.0f;
        float btnGap = 8.0f;
        float ry = (topBarH - btnSize) * 0.5f;

        themeBtnRect.x = (float)windowW - rightMargin - btnSize;
        themeBtnRect.y = ry;
        themeBtnRect.w = btnSize;
        themeBtnRect.h = btnSize;

        scanBtnRect.x = themeBtnRect.x - btnGap - btnSize;
        scanBtnRect.y = ry;
        scanBtnRect.w = btnSize;
        scanBtnRect.h = btnSize;

        scanBtnRect.isHovered = isInside(mouseX, mouseY, scanBtnRect.x, scanBtnRect.y, btnSize, btnSize);
        themeBtnRect.isHovered = isInside(mouseX, mouseY, themeBtnRect.x, themeBtnRect.y, btnSize, btnSize);

        font.beginBatch();
        if (scanBtnRect.isHovered) font.addRoundedRect(scanBtnRect.x, scanBtnRect.y, btnSize, btnSize, 6.0f, pal.btnHover);
        if (showThemeMenu || themeBtnRect.isHovered) font.addRoundedRect(themeBtnRect.x, themeBtnRect.y, btnSize, btnSize, 6.0f, showThemeMenu ? pal.accent : pal.btnHover);
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_REFRESH, scanBtnRect.x + 6, scanBtnRect.y + 6, 20, 20,
                           scanner.isScanning.load() ? pal.accent : pal.textPrimary);
        iconAtlas.drawIcon(font, theme.isDarkMode.load() ? ICON_THEME_LIGHT : ICON_THEME_DARK,
                           themeBtnRect.x + 6, themeBtnRect.y + 6, 20, 20, showThemeMenu ? Color4(1, 1, 1, 1) : pal.textPrimary);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        // 2. Left App Logo & Title (Responsive)
        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_PHOTO, 16.0f, 16.0f, 28.0f, 28.0f, pal.accent);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        float leftBoundary = 52.0f;
        if (windowW >= 720) {
            font.beginBatch();
            font.addText(52.0f, 20.0f, "SilverGallery", pal.textPrimary);
            font.render(windowW, windowH);
            leftBoundary = 52.0f + font.measureText("SilverGallery") + 18.0f;
        }

        // 3. Center Filter Tabs (Responsive Segmented Control Pill)
        float rightBoundary = scanBtnRect.x - 14.0f;
        float availTabSpace = std::max(120.0f, rightBoundary - leftBoundary);

        const char* tabNamesFull[4] = { "All Photos", "Timeline", "Favorites", "Folders" };
        const char* tabNamesMed[4]  = { "Photos", "Timeline", "Favorites", "Folders" };
        const char* tabNamesShort[4] = { "Photos", "Time", "Favs", "Folders" };

        const char** activeTabNames = tabNamesFull;
        if (availTabSpace < 380.0f) activeTabNames = tabNamesMed;
        if (availTabSpace < 290.0f) activeTabNames = tabNamesShort;

        float tabH = 36.0f;
        float tabY = (topBarH - tabH) * 0.5f;
        float maxTabGroupW = (availTabSpace >= 460.0f) ? 420.0f : (availTabSpace - 16.0f);
        float totalTabW = std::clamp(maxTabGroupW, 200.0f, 420.0f);
        float tabX = leftBoundary + (availTabSpace - totalTabW) * 0.5f;

        font.beginBatch();
        font.addRoundedRect(tabX, tabY, totalTabW, tabH, 8.0f, pal.cardBg);
        font.addRoundedBorder(tabX, tabY, totalTabW, tabH, 8.0f, 1.0f, pal.cardBorder);
        font.render(windowW, windowH);

        // Pre-compute tab rects
        float curTabX = tabX + 3.0f;
        float tw = (totalTabW - 6.0f - 6.0f) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            tabRects[i].x = curTabX;
            tabRects[i].y = tabY + 3.0f;
            tabRects[i].w = tw;
            tabRects[i].h = tabH - 6.0f;
            tabRects[i].isHovered = isInside(mouseX, mouseY, tabRects[i].x, tabRects[i].y, tabRects[i].w, tabRects[i].h);
            curTabX += tw + 2.0f;
        }

        // 1. Draw smoothly animated sliding active indicator pill
        if (tabAnimW > 0.0f) {
            font.beginBatch();
            font.addRoundedRect(tabAnimX, tabY + 3.0f, tabAnimW, tabH - 6.0f, 6.0f, pal.accent);
            font.render(windowW, windowH);
        }

        // 2. Draw tab hover backgrounds and text
        for (int i = 0; i < 4; ++i) {
            bool isSel = (currentTab == (GalleryTab)i);

            if (!isSel && tabRects[i].isHovered) {
                font.beginBatch();
                font.addRoundedRect(tabRects[i].x, tabRects[i].y, tabRects[i].w, tabRects[i].h, 6.0f, pal.btnHover);
                font.render(windowW, windowH);
            }

            font.beginBatch();
            float textW = font.measureText(activeTabNames[i]);
            float tx = tabRects[i].x + (tabRects[i].w - textW) * 0.5f;
            Color4 textCol = isSel ? Color4(1, 1, 1, 1) : (tabRects[i].isHovered ? pal.textPrimary : pal.textSecondary);
            font.addText(tx, tabRects[i].y + 6.5f, activeTabNames[i], textCol);
            font.render(windowW, windowH);
        }

        // -------------------------------------------------------------
        // F. FLOATING STATUS & ZOOM PILL (Bottom)
        // -------------------------------------------------------------
        // 1. Status pill at bottom center
        if (!showSidebar || windowW >= 1200) {
            std::string statusText;
            if (scanner.isScanning.load()) {
                std::lock_guard<std::mutex> lock(scanner.statusMutex);
                statusText = "Scanning folders... " + std::to_string(scanner.scannedFiles.load()) + " photos found";
            } else if (!activeFolderFilter.empty()) {
                statusText = "Folder: " + activeFolderFilter + " (" + std::to_string(timeline.totalPhotoCount) + " photos)";
            } else {
                int total = db.getTotalCount();
                int64_t bytes = db.getTotalBytes();
                char bBuf[128];
                if (bytes > 1024 * 1024 * 1024) {
                    snprintf(bBuf, sizeof(bBuf), "Total: %d Photos - %.2f GB", total, (float)bytes / (1024.0f * 1024.0f * 1024.0f));
                } else {
                    snprintf(bBuf, sizeof(bBuf), "Total: %d Photos - %.1f MB", total, (float)bytes / (1024.0f * 1024.0f));
                }
                statusText = bBuf;
            }

            if (!statusText.empty()) {
                float tw = font.measureText(statusText) + 36.0f;
                float th = 34.0f;
                float tx = (curGridAreaW - tw) * 0.5f;
                float ty = (float)windowH - th - 16.0f;

                font.beginBatch();
                font.addRoundedRect(tx + 2, ty + 2, tw, th, 17.0f, Color4(0, 0, 0, 0.35f));
                font.addRoundedRect(tx, ty, tw, th, 17.0f, pal.toastBg);
                font.addRoundedBorder(tx, ty, tw, th, 17.0f, 1.0f, pal.toastBorder);
                font.render(windowW, windowH);

                font.beginBatch();
                font.addText(tx + 18.0f, ty + 8.5f, statusText, pal.textAccent);
                font.render(windowW, windowH);
            }
        }

        // 2. Bottom Right Zoom Percentage Pill Button & Vertical Slider Popup
        {
            float pillW = 66.0f;
            float pillH = 32.0f;
            float pillX = curGridAreaW - pillW - 20.0f;
            float pillY = (float)windowH - pillH - 18.0f;
            zoomPillBtnRect.x = pillX;
            zoomPillBtnRect.y = pillY;
            zoomPillBtnRect.w = pillW;
            zoomPillBtnRect.h = pillH;
            zoomPillBtnRect.isHovered = isInside(mouseX, mouseY, pillX, pillY, pillW, pillH);

            char pctBuf[32];
            snprintf(pctBuf, sizeof(pctBuf), "%d%%", timeline.getZoomPercentage());
            float ptw = font.measureText(pctBuf);

            font.beginBatch();
            font.addRoundedRect(pillX + 2, pillY + 2, pillW, pillH, 8.0f, Color4(0, 0, 0, 0.35f));
            if (showZoomPopup || zoomPillBtnRect.isHovered) {
                font.addRoundedRect(pillX, pillY, pillW, pillH, 8.0f, pal.accent);
            } else {
                font.addRoundedRect(pillX, pillY, pillW, pillH, 8.0f, pal.toastBg);
                font.addRoundedBorder(pillX, pillY, pillW, pillH, 8.0f, 1.0f, pal.toastBorder);
            }
            font.render(windowW, windowH);

            font.beginBatch();
            font.addText(pillX + (pillW - ptw) * 0.5f, pillY + 7.5f, pctBuf, (showZoomPopup || zoomPillBtnRect.isHovered) ? Color4(1, 1, 1, 1) : pal.textPrimary);
            font.render(windowW, windowH);

            // 3. Zoom Popup HUD / Menu & Vertical Slider (Smooth Slide-Up & Alpha Fade)
            if (zoomPopupAnim > 0.002f) {
                float popW = 180.0f;
                float popH = 166.0f;
                float popX = std::max(16.0f, pillX + pillW - popW);
                float popTargetY = pillY - popH - 10.0f;
                float popSlideOffset = (1.0f - zoomPopupAnim) * 12.0f;
                float popY = popTargetY + popSlideOffset;

                zoomPopupRect.x = popX;
                zoomPopupRect.y = popTargetY;
                zoomPopupRect.w = popW;
                zoomPopupRect.h = popH;

                float popupAlpha = zoomPopupAnim;

                // Background & Shadow
                font.beginBatch();
                font.addRoundedRect(popX + 3, popY + 4, popW, popH, 10.0f, Color4(0, 0, 0, 0.50f * popupAlpha));
                font.addRoundedRect(popX, popY, popW, popH, 10.0f, pal.isDark ? Color4::Hex(0x181A20, 0.98f * popupAlpha) : Color4::Hex(0xFFFFFF, 0.98f * popupAlpha));
                font.addRoundedBorder(popX, popY, popW, popH, 10.0f, 1.2f, pal.isDark ? Color4::Hex(0x2D323E, popupAlpha) : Color4::Hex(0xE0E3E8, popupAlpha));
                font.render(windowW, windowH);

                // Preset List (Left column)
                struct PresetInfo {
                    GridPreset preset;
                    IconType icon;
                    const char* label;
                    UIRect* rect;
                };
                PresetInfo presets[4] = {
                    { PRESET_XL, ICON_PHOTO, "Extra Large", &presetXLRect },
                    { PRESET_LARGE, ICON_GRID, "Large", &presetLargeRect },
                    { PRESET_MEDIUM, ICON_GRID, "Medium", &presetMediumRect },
                    { PRESET_SMALL, ICON_GRID, "Small", &presetSmallRect }
                };

                float itemX = popX + 8.0f;
                float itemW = popW - 38.0f; // 142px
                float itemH = 32.0f;
                float itemGap = 3.0f;
                float curItemY = popY + 14.0f;

                for (int pIdx = 0; pIdx < 4; ++pIdx) {
                    PresetInfo& pi = presets[pIdx];
                    pi.rect->x = itemX;
                    pi.rect->y = curItemY;
                    pi.rect->w = itemW;
                    pi.rect->h = itemH;
                    pi.rect->isHovered = isInside(mouseX, mouseY, itemX, curItemY, itemW, itemH);

                    bool isActive = (timeline.currentPreset == pi.preset);

                    if (isActive || pi.rect->isHovered) {
                        font.beginBatch();
                        Color4 bgCol = isActive ? pal.accent : (pal.isDark ? Color4::Hex(0x252830, 0.90f * popupAlpha) : Color4::Hex(0xEEF0F4, 0.90f * popupAlpha));
                        font.addRoundedRect(itemX, curItemY, itemW, itemH, 6.0f, bgCol);
                        font.render(windowW, windowH);
                    }

                    // Icon
                    Color4 iconCol = isActive ? Color4(1, 1, 1, popupAlpha) : (pi.rect->isHovered ? pal.textPrimary : pal.textSecondary);
                    font.beginBatch();
                    iconAtlas.drawIcon(font, pi.icon, itemX + 8.0f, curItemY + 7.0f, 18.0f, 18.0f, iconCol);
                    font.render(windowW, windowH, 0, iconAtlas.textureId);

                    // Text
                    Color4 textCol = isActive ? Color4(1, 1, 1, popupAlpha) : pal.textPrimary;
                    font.beginBatch();
                    font.addText(itemX + 32.0f, curItemY + 7.5f, pi.label, textCol);
                    font.render(windowW, windowH);

                    curItemY += itemH + itemGap;
                }

                // Vertical Slider (Right column)
                float trackX = popX + popW - 18.0f;
                float trackY = popY + 14.0f;
                float trackW = 6.0f;
                float trackH = popH - 28.0f; // 138px

                zoomSliderTrackRect.x = trackX - 8.0f;
                zoomSliderTrackRect.y = trackY;
                zoomSliderTrackRect.w = 22.0f;
                zoomSliderTrackRect.h = trackH;

                float thumbY = trackY + (1.0f - timeline.zoomScale) * trackH;
                float thumbW = 12.0f;
                float thumbH = 22.0f;
                float thumbX = trackX + (trackW - thumbW) * 0.5f;

                zoomSliderThumbRect.x = thumbX;
                zoomSliderThumbRect.y = thumbY - thumbH * 0.5f;
                zoomSliderThumbRect.w = thumbW;
                zoomSliderThumbRect.h = thumbH;

                font.beginBatch();
                // Rail background
                Color4 railBg = pal.isDark ? Color4::Hex(0x2A2E38, popupAlpha) : Color4::Hex(0xDFE2E8, popupAlpha);
                font.addRoundedRect(trackX, trackY, trackW, trackH, 3.0f, railBg);

                // Blue track fill from bottom up to thumbY
                float fillBottom = trackY + trackH;
                if (fillBottom > thumbY) {
                    font.addRoundedRect(trackX, thumbY, trackW, fillBottom - thumbY, 3.0f, Color4(pal.accent.r, pal.accent.g, pal.accent.b, popupAlpha));
                }

                // Thumb pill handle
                font.addRoundedRect(thumbX, thumbY - thumbH * 0.5f, thumbW, thumbH, 6.0f, Color4(pal.accent.r, pal.accent.g, pal.accent.b, popupAlpha));
                font.addRoundedBorder(thumbX, thumbY - thumbH * 0.5f, thumbW, thumbH, 6.0f, 1.2f, Color4(1, 1, 1, 0.40f * popupAlpha));
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // G. THEME TOAST NOTIFICATION
        // -------------------------------------------------------------
        if (themeToastTimer > 0.0f && !themeToastText.empty()) {
            float toastAlpha = std::min(1.0f, themeToastTimer * 3.0f);
            float tw = font.measureText(themeToastText) + 36.0f;
            float th = 34.0f;
            float tx = ((float)windowW - tw) * 0.5f;
            float ty = topBarH + 16.0f;

            font.beginBatch();
            font.addRoundedRect(tx + 2, ty + 2, tw, th, 17.0f, Color4(0, 0, 0, 0.40f * toastAlpha));
            font.addRoundedRect(tx, ty, tw, th, 17.0f, pal.toastBg);
            font.addRoundedBorder(tx, ty, tw, th, 17.0f, 1.0f, Color4(pal.accent.r, pal.accent.g, pal.accent.b, 0.85f * toastAlpha));
            font.render(windowW, windowH);

            font.beginBatch();
            font.addText(tx + 18.0f, ty + 8.5f, themeToastText, Color4(pal.textAccent.r, pal.textAccent.g, pal.textAccent.b, toastAlpha));
            font.render(windowW, windowH);
        }

        // -------------------------------------------------------------
        // G.5 THEME MENU POPOVER (Top Z-Index Floating Layer)
        // -------------------------------------------------------------
        if (themeMenuAnim > 0.002f) {
            float tmW = 160.0f;
            float tmH = 118.0f;
            float tmX = std::clamp(themeBtnRect.x + themeBtnRect.w - tmW, 12.0f, (float)windowW - tmW - 14.0f);
            float tmY = topBarH + 6.0f + (1.0f - themeMenuAnim) * (-8.0f);

            themeMenuRect.x = tmX;
            themeMenuRect.y = topBarH + 6.0f;
            themeMenuRect.w = tmW;
            themeMenuRect.h = tmH;

            float tmAlpha = themeMenuAnim;

            // Background & Shadow
            font.beginBatch();
            font.addRoundedRect(tmX + 3, tmY + 4, tmW, tmH, 8.0f, Color4(0, 0, 0, 0.50f * tmAlpha));
            font.addRoundedRect(tmX, tmY, tmW, tmH, 8.0f, pal.isDark ? Color4::Hex(0x181A20, 0.98f * tmAlpha) : Color4::Hex(0xFFFFFF, 0.98f * tmAlpha));
            font.addRoundedBorder(tmX, tmY, tmW, tmH, 8.0f, 1.0f, pal.isDark ? Color4::Hex(0x2D323E, tmAlpha) : Color4::Hex(0xDFE2E8, tmAlpha));
            font.render(windowW, windowH);

            // 3 Options: System, Dark, Light
            struct ThemeOpt {
                ThemeMode m;
                IconType icon;
                const char* label;
                UIRect* rect;
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
                font.addText(optX + 32.0f, optY + 7.5f, to.label, textCol);
                font.render(windowW, windowH);

                optY += optH + 3.0f;
            }
        }

        // -------------------------------------------------------------
        // H. FULLSCREEN IN-GALLERY VIEW OVERLAY (Animated Transition)
        // -------------------------------------------------------------
        if (fsAnim > 0.002f) {
            renderFullScreenView(windowW, windowH, mouseX, mouseY, records, font, iconAtlas, fsAnim);
        }
    }

    // -----------------------------------------------------------------
    // RENDER: FILEPILOT RIGHT SIDEBAR PREVIEW COLUMN
    // -----------------------------------------------------------------
    void renderSidebar(int windowW, int windowH, float mouseX, float mouseY,
                       FontRenderer& font, IconAtlas& iconAtlas) {
        const ThemePalette& pal = theme.current;
        float sbSlideOffset = (1.0f - sidebarAnim) * sidebarWidth;
        float sbX = (float)windowW - sidebarWidth + sbSlideOffset;
        float topBarH = 60.0f;
        float sbH = (float)windowH - topBarH;

        // Background & Shadow with smooth slide and fade
        font.beginBatch();
        font.addRect(sbX - 6.0f, topBarH, 6.0f, sbH, Color4(0, 0, 0, 0.22f * sidebarAnim));
        font.addRect(sbX, topBarH, sidebarWidth, sbH, pal.cardBg);
        font.addRect(sbX, topBarH, 1.0f, sbH, pal.cardBorder);
        font.render(windowW, windowH);

        float curY = topBarH + 14.0f;
        float innerX = sbX + 16.0f;
        float innerW = sidebarWidth - 32.0f;

        // 1. Header: Title & Polished Close Button
        float btnCloseSize = 28.0f;
        sbCloseRect.x = sbX + sidebarWidth - btnCloseSize - 16.0f;
        sbCloseRect.y = curY;
        sbCloseRect.w = btnCloseSize;
        sbCloseRect.h = btnCloseSize;
        sbCloseRect.isHovered = isInside(mouseX, mouseY, sbCloseRect.x, sbCloseRect.y, btnCloseSize, btnCloseSize);

        font.beginBatch();
        font.addText(innerX, curY + 5.0f, "Preview & Info", pal.textPrimary);
        // Styled rounded close button pill
        font.addRoundedRect(sbCloseRect.x, sbCloseRect.y, btnCloseSize, btnCloseSize, 6.0f, sbCloseRect.isHovered ? pal.btnHover : pal.btnBg);
        font.addRoundedBorder(sbCloseRect.x, sbCloseRect.y, btnCloseSize, btnCloseSize, 6.0f, 1.0f, sbCloseRect.isHovered ? pal.accent : pal.btnBorder);
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_CLOSE, sbCloseRect.x + 7.0f, sbCloseRect.y + 7.0f, 14.0f, 14.0f, sbCloseRect.isHovered ? pal.textPrimary : pal.textSecondary);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        curY += 38.0f;

        // 2. Large Image Preview Box (Aspect-Fit with Recessed Matte Frame)
        float prevH = 190.0f;
        sbPreviewRect.x = innerX;
        sbPreviewRect.y = curY;
        sbPreviewRect.w = innerW;
        sbPreviewRect.h = prevH;
        sbPreviewRect.isHovered = isInside(mouseX, mouseY, innerX, curY, innerW, prevH);

        Color4 previewMatteBg = pal.isDark ? Color4::Hex(0x0C0D10, 1.0f) : Color4::Hex(0xEAEEF3, 1.0f);
        font.beginBatch();
        font.addRoundedRect(innerX, curY, innerW, prevH, 8.0f, previewMatteBg);
        font.addRoundedBorder(innerX, curY, innerW, prevH, 8.0f, 1.0f, sbPreviewRect.isHovered ? pal.accent : pal.cardBorder);
        font.render(windowW, windowH);

        // Draw High-Res / Thumbnail Preview
        GLuint drawTexId = highResPreview.id ? highResPreview.id : 0;
        int imgW = highResPreview.width > 0 ? highResPreview.width : selectedRecord.width;
        int imgH = highResPreview.height > 0 ? highResPreview.height : selectedRecord.height;

        if (!drawTexId) {
            auto it = thumbs.cache.find(selectedPath);
            if (it != thumbs.cache.end() && it->second.ready) {
                drawTexId = it->second.texId;
                if (imgW <= 0) imgW = it->second.width;
                if (imgH <= 0) imgH = it->second.height;
            }
        }

        if (drawTexId && imgW > 0 && imgH > 0) {
            float aspect = (float)imgW / (float)imgH;
            float targetW = innerW - 12.0f;
            float targetH = prevH - 12.0f;
            float finalW = targetW;
            float finalH = targetW / aspect;
            if (finalH > targetH) {
                finalH = targetH;
                finalW = targetH * aspect;
            }

            float px = innerX + (innerW - finalW) * 0.5f;
            float py = curY + (prevH - finalH) * 0.5f;

            font.beginBatch();
            UIVertex v[6] = {
                { px, py, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px + finalW, py, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px + finalW, py + finalH, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },

                { px, py, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px + finalW, py + finalH, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px, py + finalH, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
            };
            font.vertices.insert(font.vertices.end(), v, v + 6);
            font.render(windowW, windowH, drawTexId);

            // Hover overlay "Click to Fullscreen"
            if (sbPreviewRect.isHovered) {
                font.beginBatch();
                font.addRoundedRect(innerX, curY + prevH - 30.0f, innerW, 30.0f, 8.0f, Color4(0, 0, 0, 0.65f));
                font.render(windowW, windowH);

                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_FIT, innerX + 16.0f, curY + prevH - 22.0f, 14.0f, 14.0f, Color4(1, 1, 1, 1));
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                font.addText(innerX + 36.0f, curY + prevH - 22.0f, "Click for Fullscreen", Color4(1, 1, 1, 1));
                font.render(windowW, windowH);
            }
        }

        curY += prevH + 14.0f;

        // 3. Action Buttons Row: [Star] [Fullscreen] [Viewer]
        float btnH = 34.0f;
        float btnW = (innerW - 12.0f) / 3.0f;

        sbFavBtnRect.x = innerX; sbFavBtnRect.y = curY; sbFavBtnRect.w = btnW; sbFavBtnRect.h = btnH;
        sbFullscreenBtnRect.x = innerX + btnW + 6.0f; sbFullscreenBtnRect.y = curY; sbFullscreenBtnRect.w = btnW; sbFullscreenBtnRect.h = btnH;
        sbViewerBtnRect.x = innerX + (btnW + 6.0f) * 2; sbViewerBtnRect.y = curY; sbViewerBtnRect.w = btnW; sbViewerBtnRect.h = btnH;

        sbFavBtnRect.isHovered = isInside(mouseX, mouseY, sbFavBtnRect.x, sbFavBtnRect.y, btnW, btnH);
        sbFullscreenBtnRect.isHovered = isInside(mouseX, mouseY, sbFullscreenBtnRect.x, sbFullscreenBtnRect.y, btnW, btnH);
        sbViewerBtnRect.isHovered = isInside(mouseX, mouseY, sbViewerBtnRect.x, sbViewerBtnRect.y, btnW, btnH);

        font.beginBatch();
        // Fav Button
        Color4 favBg = selectedRecord.starred ? Color4::Hex(0xEF4444, 0.9f) : (sbFavBtnRect.isHovered ? pal.btnHover : pal.cardBg);
        font.addRoundedRect(sbFavBtnRect.x, sbFavBtnRect.y, btnW, btnH, 6.0f, favBg);
        font.addRoundedBorder(sbFavBtnRect.x, sbFavBtnRect.y, btnW, btnH, 6.0f, 1.0f, selectedRecord.starred ? favBg : pal.cardBorder);

        // Fullscreen Button
        font.addRoundedRect(sbFullscreenBtnRect.x, sbFullscreenBtnRect.y, btnW, btnH, 6.0f, sbFullscreenBtnRect.isHovered ? pal.btnHover : pal.cardBg);
        font.addRoundedBorder(sbFullscreenBtnRect.x, sbFullscreenBtnRect.y, btnW, btnH, 6.0f, 1.0f, pal.cardBorder);

        // Viewer Button
        font.addRoundedRect(sbViewerBtnRect.x, sbViewerBtnRect.y, btnW, btnH, 6.0f, sbViewerBtnRect.isHovered ? pal.btnHover : pal.cardBg);
        font.addRoundedBorder(sbViewerBtnRect.x, sbViewerBtnRect.y, btnW, btnH, 6.0f, 1.0f, pal.cardBorder);
        font.render(windowW, windowH);

        // Draw icons
        font.beginBatch();
        iconAtlas.drawIcon(font, selectedRecord.starred ? ICON_HEART_FILLED : ICON_HEART,
                           sbFavBtnRect.x + 8.0f, sbFavBtnRect.y + 9.0f, 16.0f, 16.0f,
                           selectedRecord.starred ? Color4(1, 1, 1, 1) : pal.textPrimary);
        iconAtlas.drawIcon(font, ICON_FIT,
                           sbFullscreenBtnRect.x + 8.0f, sbFullscreenBtnRect.y + 9.0f, 16.0f, 16.0f, pal.textPrimary);
        iconAtlas.drawIcon(font, ICON_EXTERNAL_LINK,
                           sbViewerBtnRect.x + 8.0f, sbViewerBtnRect.y + 9.0f, 16.0f, 16.0f, pal.textPrimary);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        // Draw labels
        font.beginBatch();
        font.addText(sbFavBtnRect.x + 28.0f, sbFavBtnRect.y + 8.5f, selectedRecord.starred ? "Starred" : "Star", selectedRecord.starred ? Color4(1, 1, 1, 1) : pal.textPrimary);
        font.addText(sbFullscreenBtnRect.x + 28.0f, sbFullscreenBtnRect.y + 8.5f, "View", pal.textPrimary);
        font.addText(sbViewerBtnRect.x + 28.0f, sbViewerBtnRect.y + 8.5f, "Open", pal.textPrimary);
        font.render(windowW, windowH);

        curY += btnH + 16.0f;

        // 4. FilePilot Rich Metadata Cards
        auto renderMetaSection = [&](const std::string& secTitle, const std::vector<std::pair<std::string, std::string>>& entries) {
            font.beginBatch();
            font.addText(innerX, curY, secTitle, pal.textAccent);
            font.render(windowW, windowH);
            curY += 22.0f;

            float blockH = (float)entries.size() * 24.0f + 12.0f;
            font.beginBatch();
            font.addRoundedRect(innerX, curY, innerW, blockH, 6.0f, pal.cardBg);
            font.addRoundedBorder(innerX, curY, innerW, blockH, 6.0f, 1.0f, pal.cardBorder);
            font.render(windowW, windowH);

            float rowY = curY + 8.0f;
            for (const auto& kv : entries) {
                font.beginBatch();
                font.addText(innerX + 10.0f, rowY, kv.first, pal.textSecondary);
                float labelW = font.measureText(kv.first);
                float maxValW = innerW - labelW - 24.0f;
                std::string displayVal = fitTextWithEllipsis(font, kv.second, maxValW);
                float valW = font.measureText(displayVal);
                float valX = innerX + innerW - valW - 10.0f;
                if (valX < innerX + labelW + 14.0f) valX = innerX + labelW + 14.0f;
                font.addText(valX, rowY, displayVal, pal.textPrimary);
                font.render(windowW, windowH);
                rowY += 24.0f;
            }
            curY += blockH + 14.0f;
        };

        // Section A: File Details
        char sizeBuf[64];
        if (selectedRecord.fileSize > 1024 * 1024) {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.2f MB", (float)selectedRecord.fileSize / (1024.0f * 1024.0f));
        } else {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", (float)selectedRecord.fileSize / 1024.0f);
        }

        char dimBuf[64];
        if (selectedRecord.width > 0 && selectedRecord.height > 0) {
            snprintf(dimBuf, sizeof(dimBuf), "%d x %d", selectedRecord.width, selectedRecord.height);
        } else {
            snprintf(dimBuf, sizeof(dimBuf), "Probing...");
        }

        std::string ext = selectedRecord.fileType.empty() ? "Image" : selectedRecord.fileType;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);

        renderMetaSection("FILE DETAILS", {
            { "Name", selectedRecord.filename },
            { "Dimensions", dimBuf },
            { "File Size", sizeBuf },
            { "Format", ext }
        });

        // Section B: Timestamps
        auto formatEpoch = [](int64_t t) -> std::string {
            if (t <= 0) return "Unknown";
            time_t tt = (time_t)t;
            struct tm* tm = localtime(&tt);
            if (!tm) return "Unknown";
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
            return std::string(buf);
        };

        renderMetaSection("TIMESTAMPS", {
            { "Captured", formatEpoch(selectedRecord.captureTime) },
            { "Modified", formatEpoch(selectedRecord.modifiedTime) }
        });

        // Section C: Location & Path
        std::string folderDisplay = selectedRecord.folder;
        size_t lastSlash = folderDisplay.find_last_of("/\\");
        if (lastSlash != std::string::npos) folderDisplay = folderDisplay.substr(lastSlash + 1);

        renderMetaSection("LOCATION", {
            { "Folder", folderDisplay }
        });

        // Copy Full Path Button
        sbCopyPathRect.x = innerX; sbCopyPathRect.y = curY; sbCopyPathRect.w = innerW; sbCopyPathRect.h = 32.0f;
        sbCopyPathRect.isHovered = isInside(mouseX, mouseY, innerX, curY, innerW, 32.0f);

        font.beginBatch();
        font.addRoundedRect(innerX, curY, innerW, 32.0f, 6.0f, sbCopyPathRect.isHovered ? pal.btnHover : pal.cardBg);
        font.addRoundedBorder(innerX, curY, innerW, 32.0f, 6.0f, 1.0f, pal.cardBorder);
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_COPY, innerX + 12.0f, curY + 8.0f, 16.0f, 16.0f, pal.accent);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        font.beginBatch();
        font.addText(innerX + 34.0f, curY + 7.5f, "Copy Full File Path", pal.accent);
        font.render(windowW, windowH);
    }

    // -----------------------------------------------------------------
    // RENDER: MOBILE / DESKTOP RESPONSIVE FULL-SCREEN IN-APP VIEWER (Animated Lightbox)
    // -----------------------------------------------------------------
    void renderFullScreenView(int windowW, int windowH, float mouseX, float mouseY,
                              const std::vector<GalleryRecord>& records,
                              FontRenderer& font, IconAtlas& iconAtlas,
                              float anim = 1.0f) {
        const ThemePalette& pal = theme.current;

        // 1. Dark Backdrop (Smooth Fade)
        font.beginBatch();
        Color4 bgCol = pal.isDark ? Color4::Hex(0x0A0B0E, 0.98f * anim) : Color4::Hex(0xF3F4F6, 0.98f * anim);
        font.addRect(0, 0, (float)windowW, (float)windowH, bgCol);
        font.render(windowW, windowH);

        // 2. Render Fullscreen Image (Smooth Zoom, Pan & Motion Spring Scale)
        GLuint drawTexId = highResPreview.id;
        int imgW = highResPreview.width > 0 ? highResPreview.width : selectedRecord.width;
        int imgH = highResPreview.height > 0 ? highResPreview.height : selectedRecord.height;

        if (!drawTexId) {
            auto it = thumbs.cache.find(selectedPath);
            if (it != thumbs.cache.end() && it->second.ready) {
                drawTexId = it->second.texId;
                if (imgW <= 0) imgW = it->second.width;
                if (imgH <= 0) imgH = it->second.height;
            }
        }

        float topBarH = 64.0f;
        float viewH = (float)windowH - topBarH;

        if (drawTexId && imgW > 0 && imgH > 0) {
            float aspect = (float)imgW / (float)imgH;
            float fitW = (float)windowW - 40.0f;
            float fitH = viewH - 40.0f;
            float scaleW = fitW;
            float scaleH = fitW / aspect;
            if (scaleH > fitH) {
                scaleH = fitH;
                scaleW = fitH * aspect;
            }

            float motionScale = (0.88f + 0.12f * anim) * fsZoom;
            scaleW *= motionScale;
            scaleH *= motionScale;

            float cx = (float)windowW * 0.5f + fsPanX;
            float cy = topBarH + viewH * 0.5f + fsPanY;
            float ix = cx - scaleW * 0.5f;
            float iy = cy - scaleH * 0.5f;

            font.beginBatch();
            UIVertex v[6] = {
                { ix, iy, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix + scaleW, iy, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix + scaleW, iy + scaleH, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, anim, 2.0f },

                { ix, iy, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix + scaleW, iy + scaleH, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix, iy + scaleH, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, anim, 2.0f },
            };
            font.vertices.insert(font.vertices.end(), v, v + 6);
            font.render(windowW, windowH, drawTexId);
        }

        // 3. Navigation Arrows (< and >)
        float arrowSize = 48.0f;
        fsPrevBtnRect.x = 16.0f;
        fsPrevBtnRect.y = topBarH + (viewH - arrowSize) * 0.5f;
        fsPrevBtnRect.w = arrowSize;
        fsPrevBtnRect.h = arrowSize;
        fsPrevBtnRect.isHovered = isInside(mouseX, mouseY, fsPrevBtnRect.x, fsPrevBtnRect.y, arrowSize, arrowSize);

        fsNextBtnRect.x = (float)windowW - arrowSize - 16.0f;
        fsNextBtnRect.y = topBarH + (viewH - arrowSize) * 0.5f;
        fsNextBtnRect.w = arrowSize;
        fsNextBtnRect.h = arrowSize;
        fsNextBtnRect.isHovered = isInside(mouseX, mouseY, fsNextBtnRect.x, fsNextBtnRect.y, arrowSize, arrowSize);

        if (fullScreenIndex > 0) {
            font.beginBatch();
            font.addRoundedRect(fsPrevBtnRect.x, fsPrevBtnRect.y, arrowSize, arrowSize, 24.0f,
                                fsPrevBtnRect.isHovered ? pal.btnHover : Color4(0, 0, 0, 0.45f * anim));
            font.render(windowW, windowH);

            font.beginBatch();
            iconAtlas.drawIcon(font, ICON_CHEVRON_LEFT, fsPrevBtnRect.x + 12.0f, fsPrevBtnRect.y + 12.0f, 24.0f, 24.0f, Color4(1, 1, 1, anim));
            font.render(windowW, windowH, 0, iconAtlas.textureId);
        }

        if (fullScreenIndex + 1 < (int)records.size()) {
            font.beginBatch();
            font.addRoundedRect(fsNextBtnRect.x, fsNextBtnRect.y, arrowSize, arrowSize, 24.0f,
                                fsNextBtnRect.isHovered ? pal.btnHover : Color4(0, 0, 0, 0.45f * anim));
            font.render(windowW, windowH);

            font.beginBatch();
            iconAtlas.drawIcon(font, ICON_CHEVRON_RIGHT, fsNextBtnRect.x + 12.0f, fsNextBtnRect.y + 12.0f, 24.0f, 24.0f, Color4(1, 1, 1, anim));
            font.render(windowW, windowH, 0, iconAtlas.textureId);
        }

        // 4. Top Mobile Navigation Header Bar
        font.beginBatch();
        font.addRect(0, 0, (float)windowW, topBarH, Color4(pal.barBg.r, pal.barBg.g, pal.barBg.b, pal.barBg.a * anim));
        font.addRect(0, topBarH - 1.0f, (float)windowW, 1.0f, Color4(pal.cardBorder.r, pal.cardBorder.g, pal.cardBorder.b, anim));
        font.render(windowW, windowH);

        // Mobile-Style "Back" Button
        fsBackBtnRect.x = 16.0f;
        fsBackBtnRect.y = 14.0f;
        fsBackBtnRect.w = 90.0f;
        fsBackBtnRect.h = 36.0f;
        fsBackBtnRect.isHovered = isInside(mouseX, mouseY, fsBackBtnRect.x, fsBackBtnRect.y, fsBackBtnRect.w, fsBackBtnRect.h);

        font.beginBatch();
        font.addRoundedRect(fsBackBtnRect.x, fsBackBtnRect.y, fsBackBtnRect.w, fsBackBtnRect.h, 18.0f,
                            fsBackBtnRect.isHovered ? pal.btnHover : pal.cardBg);
        font.addRoundedBorder(fsBackBtnRect.x, fsBackBtnRect.y, fsBackBtnRect.w, fsBackBtnRect.h, 18.0f, 1.0f, pal.cardBorder);
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_ARROW_LEFT, fsBackBtnRect.x + 10.0f, fsBackBtnRect.y + 9.0f, 18.0f, 18.0f, pal.accent);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        font.beginBatch();
        font.addText(fsBackBtnRect.x + 34.0f, fsBackBtnRect.y + 8.5f, "Back", pal.accent);
        font.render(windowW, windowH);

        // Center Filename & Counter
        font.beginBatch();
        char countBuf[64];
        snprintf(countBuf, sizeof(countBuf), "  [%d / %d]", fullScreenIndex + 1, (int)records.size());
        float countW = font.measureText(countBuf);
        float maxNameW = std::max(60.0f, (float)windowW - 300.0f - countW);
        std::string fittedName = fitTextWithEllipsis(font, selectedRecord.filename, maxNameW);
        std::string fullHeader = fittedName + countBuf;
        float cw = font.measureText(fullHeader);
        float cx = ((float)windowW - cw) * 0.5f;
        font.addText(cx, 22.0f, fullHeader, Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, anim));
        font.render(windowW, windowH);

        // Right Controls: [Favorite] [Standalone Viewer] [Close]
        float fsRx = (float)windowW - 140.0f;
        float btnS = 36.0f;

        fsFavBtnRect.x = fsRx; fsFavBtnRect.y = 14.0f; fsFavBtnRect.w = btnS; fsFavBtnRect.h = btnS;
        fsViewerBtnRect.x = fsRx + 44.0f; fsViewerBtnRect.y = 14.0f; fsViewerBtnRect.w = btnS; fsViewerBtnRect.h = btnS;
        fsCloseBtnRect.x = fsRx + 88.0f; fsCloseBtnRect.y = 14.0f; fsCloseBtnRect.w = btnS; fsCloseBtnRect.h = btnS;

        fsFavBtnRect.isHovered = isInside(mouseX, mouseY, fsFavBtnRect.x, fsFavBtnRect.y, btnS, btnS);
        fsViewerBtnRect.isHovered = isInside(mouseX, mouseY, fsViewerBtnRect.x, fsViewerBtnRect.y, btnS, btnS);
        fsCloseBtnRect.isHovered = isInside(mouseX, mouseY, fsCloseBtnRect.x, fsCloseBtnRect.y, btnS, btnS);

        font.beginBatch();
        Color4 fsFavBg = selectedRecord.starred ? Color4::Hex(0xEF4444, 0.9f) : (fsFavBtnRect.isHovered ? pal.btnHover : pal.cardBg);
        font.addRoundedRect(fsFavBtnRect.x, fsFavBtnRect.y, btnS, btnS, 8.0f, fsFavBg);
        if (fsViewerBtnRect.isHovered) font.addRoundedRect(fsViewerBtnRect.x, fsViewerBtnRect.y, btnS, btnS, 8.0f, pal.btnHover);
        if (fsCloseBtnRect.isHovered) font.addRoundedRect(fsCloseBtnRect.x, fsCloseBtnRect.y, btnS, btnS, 8.0f, pal.btnHover);
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, selectedRecord.starred ? ICON_HEART_FILLED : ICON_HEART,
                           fsFavBtnRect.x + 8.0f, fsFavBtnRect.y + 8.0f, 20.0f, 20.0f,
                           selectedRecord.starred ? Color4(1, 1, 1, anim) : pal.textPrimary);
        iconAtlas.drawIcon(font, ICON_EXTERNAL_LINK,
                           fsViewerBtnRect.x + 8.0f, fsViewerBtnRect.y + 8.0f, 20.0f, 20.0f,
                           pal.textPrimary);
        iconAtlas.drawIcon(font, ICON_CLOSE,
                           fsCloseBtnRect.x + 8.0f, fsCloseBtnRect.y + 8.0f, 20.0f, 20.0f,
                           pal.textSecondary);
        font.render(windowW, windowH, 0, iconAtlas.textureId);
    }
};

