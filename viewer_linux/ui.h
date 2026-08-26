#pragma once

#include "font.h"
#include "icons.h"
#include "theme.h"
#include "image_loader.h"
#include "thumbnails.h"
#include "silver_anim.h"
#include "image_editor.h"
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
        CLOSE_OVERLAYS,
        TOGGLE_CROP_MODE,
        CROP_CANCEL,
        CROP_SAVE_AS,
        CROP_REPLACE_ORIGINAL,
        CROP_TOGGLE_SAVE_MENU,
        CROP_SET_ASPECT,
        CROP_TOGGLE_ORIENTATION,
        CROP_ROTATE_CCW,
        CROP_ROTATE_CW,
        CROP_FLIP_H,
        CROP_FLIP_V,
        CROP_RESET
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
    Rect btnCrop;
    Rect btnGridView;
    Rect btnTheme;

    CropEditState cropState;

    void getImageQuadBounds(int windowW, int windowH,
                            float imgPosX, float imgPosY,
                            float imgW, float imgH,
                            float scale, int rotDeg,
                            float& outX, float& outY, float& outW, float& outH) {
        int curRot = ((rotDeg % 360) + 360) % 360;
        float w = (curRot == 90 || curRot == 270) ? imgH : imgW;
        float h = (curRot == 90 || curRot == 270) ? imgW : imgH;
        outW = w * scale;
        outH = h * scale;
        outX = imgPosX - outW * 0.5f;
        outY = imgPosY - outH * 0.5f;
    }

    bool handleCropMouseDown(float mx, float my,
                             float imgScreenX, float imgScreenY, float imgScreenW, float imgScreenH) {
        if (!cropState.active) return false;
        float cropScreenX = imgScreenX + cropState.cropX * imgScreenW;
        float cropScreenY = imgScreenY + cropState.cropY * imgScreenH;
        float cropScreenW = cropState.cropW * imgScreenW;
        float cropScreenH = cropState.cropH * imgScreenH;

        float hitR = 14.0f * uiScale;

        auto distSq = [](float x1, float y1, float x2, float y2) {
            float dx = x1 - x2, dy = y1 - y2;
            return dx * dx + dy * dy;
        };

        CropDragHandle h = HANDLE_NONE;

        if (distSq(mx, my, cropScreenX, cropScreenY) <= hitR * hitR) {
            h = HANDLE_CORNER_TL;
        } else if (distSq(mx, my, cropScreenX + cropScreenW, cropScreenY) <= hitR * hitR) {
            h = HANDLE_CORNER_TR;
        } else if (distSq(mx, my, cropScreenX, cropScreenY + cropScreenH) <= hitR * hitR) {
            h = HANDLE_CORNER_BL;
        } else if (distSq(mx, my, cropScreenX + cropScreenW, cropScreenY + cropScreenH) <= hitR * hitR) {
            h = HANDLE_CORNER_BR;
        } else if (mx >= cropScreenX + hitR && mx <= cropScreenX + cropScreenW - hitR && std::abs(my - cropScreenY) <= hitR) {
            h = HANDLE_EDGE_TOP;
        } else if (mx >= cropScreenX + hitR && mx <= cropScreenX + cropScreenW - hitR && std::abs(my - (cropScreenY + cropScreenH)) <= hitR) {
            h = HANDLE_EDGE_BOTTOM;
        } else if (my >= cropScreenY + hitR && my <= cropScreenY + cropScreenH - hitR && std::abs(mx - cropScreenX) <= hitR) {
            h = HANDLE_EDGE_LEFT;
        } else if (my >= cropScreenY + hitR && my <= cropScreenY + cropScreenH - hitR && std::abs(mx - (cropScreenX + cropScreenW)) <= hitR) {
            h = HANDLE_EDGE_RIGHT;
        } else if (mx >= cropScreenX && mx <= cropScreenX + cropScreenW && my >= cropScreenY && my <= cropScreenY + cropScreenH) {
            h = HANDLE_BOX;
        }

        if (h != HANDLE_NONE) {
            cropState.activeHandle = h;
            cropState.dragStartMouseX = mx;
            cropState.dragStartMouseY = my;
            cropState.dragStartCropX = cropState.cropX;
            cropState.dragStartCropY = cropState.cropY;
            cropState.dragStartCropW = cropState.cropW;
            cropState.dragStartCropH = cropState.cropH;
            return true;
        }
        return false;
    }

    void handleCropMouseMove(float mx, float my,
                             float imgScreenX, float imgScreenY, float imgScreenW, float imgScreenH,
                             int nativeW, int nativeH) {
        if (!cropState.active) return;
        if (cropState.activeHandle == HANDLE_NONE) {
            float cropScreenX = imgScreenX + cropState.cropX * imgScreenW;
            float cropScreenY = imgScreenY + cropState.cropY * imgScreenH;
            float cropScreenW = cropState.cropW * imgScreenW;
            float cropScreenH = cropState.cropH * imgScreenH;

            float hitR = 14.0f * uiScale;
            auto distSq = [](float x1, float y1, float x2, float y2) {
                float dx = x1 - x2, dy = y1 - y2;
                return dx * dx + dy * dy;
            };

            if (distSq(mx, my, cropScreenX, cropScreenY) <= hitR * hitR) {
                cropState.hoveredHandle = HANDLE_CORNER_TL;
            } else if (distSq(mx, my, cropScreenX + cropScreenW, cropScreenY) <= hitR * hitR) {
                cropState.hoveredHandle = HANDLE_CORNER_TR;
            } else if (distSq(mx, my, cropScreenX, cropScreenY + cropScreenH) <= hitR * hitR) {
                cropState.hoveredHandle = HANDLE_CORNER_BL;
            } else if (distSq(mx, my, cropScreenX + cropScreenW, cropScreenY + cropScreenH) <= hitR * hitR) {
                cropState.hoveredHandle = HANDLE_CORNER_BR;
            } else if (mx >= cropScreenX + hitR && mx <= cropScreenX + cropScreenW - hitR && std::abs(my - cropScreenY) <= hitR) {
                cropState.hoveredHandle = HANDLE_EDGE_TOP;
            } else if (mx >= cropScreenX + hitR && mx <= cropScreenX + cropScreenW - hitR && std::abs(my - (cropScreenY + cropScreenH)) <= hitR) {
                cropState.hoveredHandle = HANDLE_EDGE_BOTTOM;
            } else if (my >= cropScreenY + hitR && my <= cropScreenY + cropScreenH - hitR && std::abs(mx - cropScreenX) <= hitR) {
                cropState.hoveredHandle = HANDLE_EDGE_LEFT;
            } else if (my >= cropScreenY + hitR && my <= cropScreenY + cropScreenH - hitR && std::abs(mx - (cropScreenX + cropScreenW)) <= hitR) {
                cropState.hoveredHandle = HANDLE_EDGE_RIGHT;
            } else if (mx >= cropScreenX && mx <= cropScreenX + cropScreenW && my >= cropScreenY && my <= cropScreenY + cropScreenH) {
                cropState.hoveredHandle = HANDLE_BOX;
            } else {
                cropState.hoveredHandle = HANDLE_NONE;
            }
            return;
        }

        if (imgScreenW <= 0.0f || imgScreenH <= 0.0f) return;

        float dx = (float)(mx - cropState.dragStartMouseX) / imgScreenW;
        float dy = (float)(my - cropState.dragStartMouseY) / imgScreenH;

        if (cropState.activeHandle == HANDLE_BOX) {
            float newX = std::clamp(cropState.dragStartCropX + dx, 0.0f, 1.0f - cropState.cropW);
            float newY = std::clamp(cropState.dragStartCropY + dy, 0.0f, 1.0f - cropState.cropH);
            cropState.cropX = newX;
            cropState.cropY = newY;
            return;
        }

        float minSize = 0.02f;
        float startX = cropState.dragStartCropX;
        float startY = cropState.dragStartCropY;
        float startW = cropState.dragStartCropW;
        float startH = cropState.dragStartCropH;

        if (cropState.aspectMode == CROP_ASPECT_FREE) {
            switch (cropState.activeHandle) {
                case HANDLE_CORNER_TL: {
                    float newX = std::clamp(startX + dx, 0.0f, startX + startW - minSize);
                    float newY = std::clamp(startY + dy, 0.0f, startY + startH - minSize);
                    cropState.cropW = (startX + startW) - newX;
                    cropState.cropH = (startY + startH) - newY;
                    cropState.cropX = newX;
                    cropState.cropY = newY;
                    break;
                }
                case HANDLE_CORNER_TR: {
                    float newR = std::clamp(startX + startW + dx, startX + minSize, 1.0f);
                    float newY = std::clamp(startY + dy, 0.0f, startY + startH - minSize);
                    cropState.cropW = newR - startX;
                    cropState.cropH = (startY + startH) - newY;
                    cropState.cropY = newY;
                    break;
                }
                case HANDLE_CORNER_BL: {
                    float newX = std::clamp(startX + dx, 0.0f, startX + startW - minSize);
                    float newB = std::clamp(startY + startH + dy, startY + minSize, 1.0f);
                    cropState.cropW = (startX + startW) - newX;
                    cropState.cropH = newB - startY;
                    cropState.cropX = newX;
                    break;
                }
                case HANDLE_CORNER_BR: {
                    float newR = std::clamp(startX + startW + dx, startX + minSize, 1.0f);
                    float newB = std::clamp(startY + startH + dy, startY + minSize, 1.0f);
                    cropState.cropW = newR - startX;
                    cropState.cropH = newB - startY;
                    break;
                }
                case HANDLE_EDGE_TOP: {
                    float newY = std::clamp(startY + dy, 0.0f, startY + startH - minSize);
                    cropState.cropH = (startY + startH) - newY;
                    cropState.cropY = newY;
                    break;
                }
                case HANDLE_EDGE_BOTTOM: {
                    float newB = std::clamp(startY + startH + dy, startY + minSize, 1.0f);
                    cropState.cropH = newB - startY;
                    break;
                }
                case HANDLE_EDGE_LEFT: {
                    float newX = std::clamp(startX + dx, 0.0f, startX + startW - minSize);
                    cropState.cropW = (startX + startW) - newX;
                    cropState.cropX = newX;
                    break;
                }
                case HANDLE_EDGE_RIGHT: {
                    float newR = std::clamp(startX + startW + dx, startX + minSize, 1.0f);
                    cropState.cropW = newR - startX;
                    break;
                }
                default: break;
            }
        } else {
            float targetRatio = cropState.getTargetRatio(nativeW, nativeH);
            int curW = nativeW > 0 ? nativeW : 1920;
            int curH = nativeH > 0 ? nativeH : 1080;
            if (cropState.rotation == 90 || cropState.rotation == 270) std::swap(curW, curH);
            float normRatio = targetRatio / ((float)curW / (float)curH);

            switch (cropState.activeHandle) {
                case HANDLE_CORNER_BR:
                case HANDLE_EDGE_RIGHT:
                case HANDLE_EDGE_BOTTOM: {
                    float newW = std::clamp(startW + dx, 0.05f, 1.0f - startX);
                    float newH = newW / normRatio;
                    if (startY + newH > 1.0f) {
                        newH = 1.0f - startY;
                        newW = newH * normRatio;
                    }
                    cropState.cropW = newW;
                    cropState.cropH = newH;
                    break;
                }
                case HANDLE_CORNER_TL:
                case HANDLE_EDGE_LEFT:
                case HANDLE_EDGE_TOP: {
                    float newW = std::clamp(startW - dx, 0.05f, startX + startW);
                    float newH = newW / normRatio;
                    if (newH > startY + startH) {
                        newH = startY + startH;
                        newW = newH * normRatio;
                    }
                    cropState.cropX = (startX + startW) - newW;
                    cropState.cropY = (startY + startH) - newH;
                    cropState.cropW = newW;
                    cropState.cropH = newH;
                    break;
                }
                case HANDLE_CORNER_TR: {
                    float newW = std::clamp(startW + dx, 0.05f, 1.0f - startX);
                    float newH = newW / normRatio;
                    if (newH > startY + startH) {
                        newH = startY + startH;
                        newW = newH * normRatio;
                    }
                    cropState.cropY = (startY + startH) - newH;
                    cropState.cropW = newW;
                    cropState.cropH = newH;
                    break;
                }
                case HANDLE_CORNER_BL: {
                    float newW = std::clamp(startW - dx, 0.05f, startX + startW);
                    float newH = newW / normRatio;
                    if (startY + newH > 1.0f) {
                        newH = 1.0f - startY;
                        newW = newH * normRatio;
                    }
                    cropState.cropX = (startX + startW) - newW;
                    cropState.cropW = newW;
                    cropState.cropH = newH;
                    break;
                }
                default: break;
            }
        }
    }

    void renderCropUI(int windowW, int windowH,
                      float mouseX, float mouseY,
                      const ImageMetadata& meta,
                      float zoomScale,
                      float imgPosX, float imgPosY,
                      int currentRotation) {
        const ThemePalette& pal = theme.current;
        int nativeW = meta.width > 0 ? meta.width : 1920;
        int nativeH = meta.height > 0 ? meta.height : 1080;
        int totalRot = (currentRotation + cropState.rotation) % 360;

        float imgScreenX = 0, imgScreenY = 0, imgScreenW = 0, imgScreenH = 0;
        getImageQuadBounds(windowW, windowH, imgPosX, imgPosY,
                           (float)nativeW, (float)nativeH,
                           zoomScale, totalRot,
                           imgScreenX, imgScreenY, imgScreenW, imgScreenH);

        float cropScreenX = imgScreenX + cropState.cropX * imgScreenW;
        float cropScreenY = imgScreenY + cropState.cropY * imgScreenH;
        float cropScreenW = cropState.cropW * imgScreenW;
        float cropScreenH = cropState.cropH * imgScreenH;

        // A. Dimmed mask overlay
        font.beginBatch();
        Color4 dimCol(0.0f, 0.0f, 0.0f, 0.68f);
        if (cropScreenY > 0.0f) {
            font.addRect(0, 0, (float)windowW, cropScreenY, dimCol);
        }
        if (cropScreenY + cropScreenH < (float)windowH) {
            font.addRect(0, cropScreenY + cropScreenH, (float)windowW, (float)windowH - (cropScreenY + cropScreenH), dimCol);
        }
        if (cropScreenX > 0.0f) {
            font.addRect(0, cropScreenY, cropScreenX, cropScreenH, dimCol);
        }
        if (cropScreenX + cropScreenW < (float)windowW) {
            font.addRect(cropScreenX + cropScreenW, cropScreenY, (float)windowW - (cropScreenX + cropScreenW), cropScreenH, dimCol);
        }

        // B. Crop border & Rule-of-Thirds Grid
        Color4 borderCol(1.0f, 1.0f, 1.0f, 0.95f);
        font.addRoundedBorder(cropScreenX, cropScreenY, cropScreenW, cropScreenH, 0.0f, 1.5f, borderCol);

        Color4 gridCol(1.0f, 1.0f, 1.0f, 0.30f);
        float thirdW = cropScreenW / 3.0f;
        float thirdH = cropScreenH / 3.0f;
        font.addLine(cropScreenX + thirdW, cropScreenY, cropScreenX + thirdW, cropScreenY + cropScreenH, 1.0f, gridCol);
        font.addLine(cropScreenX + thirdW * 2.0f, cropScreenY, cropScreenX + thirdW * 2.0f, cropScreenY + cropScreenH, 1.0f, gridCol);
        font.addLine(cropScreenX, cropScreenY + thirdH, cropScreenX + cropScreenW, cropScreenY + thirdH, 1.0f, gridCol);
        font.addLine(cropScreenX, cropScreenY + thirdH * 2.0f, cropScreenX + cropScreenW, cropScreenY + thirdH * 2.0f, 1.0f, gridCol);

        // C. Corner Handles (L-shaped thick brackets)
        float bracketLen = 18.0f * uiScale;
        float bracketThick = 3.0f * uiScale;
        Color4 handleCol(1.0f, 1.0f, 1.0f, 1.0f);

        // TL
        font.addRect(cropScreenX - 1.0f, cropScreenY - 1.0f, bracketLen, bracketThick, handleCol);
        font.addRect(cropScreenX - 1.0f, cropScreenY - 1.0f, bracketThick, bracketLen, handleCol);
        // TR
        font.addRect(cropScreenX + cropScreenW - bracketLen + 1.0f, cropScreenY - 1.0f, bracketLen, bracketThick, handleCol);
        font.addRect(cropScreenX + cropScreenW - bracketThick + 1.0f, cropScreenY - 1.0f, bracketThick, bracketLen, handleCol);
        // BL
        font.addRect(cropScreenX - 1.0f, cropScreenY + cropScreenH - bracketThick + 1.0f, bracketLen, bracketThick, handleCol);
        font.addRect(cropScreenX - 1.0f, cropScreenY + cropScreenH - bracketLen + 1.0f, bracketThick, bracketLen, handleCol);
        // BR
        font.addRect(cropScreenX + cropScreenW - bracketLen + 1.0f, cropScreenY + cropScreenH - bracketThick + 1.0f, bracketLen, bracketThick, handleCol);
        font.addRect(cropScreenX + cropScreenW - bracketThick + 1.0f, cropScreenY + cropScreenH - bracketLen + 1.0f, bracketThick, bracketLen, handleCol);

        // D. Edge Handles (centered rounded pills)
        float edgePillLen = 24.0f * uiScale;
        float edgePillThick = 4.0f * uiScale;
        font.addRoundedRect(cropScreenX + (cropScreenW - edgePillLen) * 0.5f, cropScreenY - edgePillThick * 0.5f, edgePillLen, edgePillThick, 2.0f, handleCol);
        font.addRoundedRect(cropScreenX + (cropScreenW - edgePillLen) * 0.5f, cropScreenY + cropScreenH - edgePillThick * 0.5f, edgePillLen, edgePillThick, 2.0f, handleCol);
        font.addRoundedRect(cropScreenX - edgePillThick * 0.5f, cropScreenY + (cropScreenH - edgePillLen) * 0.5f, edgePillThick, edgePillLen, 2.0f, handleCol);
        font.addRoundedRect(cropScreenX + cropScreenW - edgePillThick * 0.5f, cropScreenY + (cropScreenH - edgePillLen) * 0.5f, edgePillThick, edgePillLen, 2.0f, handleCol);

        // E. Dimension Badge on crop box
        int activeW = nativeW;
        int activeH = nativeH;
        if (cropState.rotation == 90 || cropState.rotation == 270) std::swap(activeW, activeH);
        int targetCropPxW = (int)std::round(cropState.cropW * activeW);
        int targetCropPxH = (int)std::round(cropState.cropH * activeH);
        std::string dimText = std::to_string(targetCropPxW) + " x " + std::to_string(targetCropPxH) + " px";
        float dimTextW = font.measureText(dimText);
        float badgeW = dimTextW + 18.0f * uiScale;
        float badgeH = 22.0f * uiScale;
        float badgeX = cropScreenX + (cropScreenW - badgeW) * 0.5f;
        float badgeY = cropScreenY + cropScreenH + 8.0f * uiScale;
        if (badgeY + badgeH > (float)windowH - 12.0f) {
            badgeY = cropScreenY - badgeH - 8.0f * uiScale;
        }
        font.addRoundedRect(badgeX, badgeY, badgeW, badgeH, 4.0f * uiScale, Color4(0.06f, 0.08f, 0.10f, 0.85f));
        font.addRoundedBorder(badgeX, badgeY, badgeW, badgeH, 4.0f * uiScale, 1.0f, Color4(1.0f, 1.0f, 1.0f, 0.20f));
        font.addTextVCentered(badgeX + 9.0f * uiScale, badgeY, badgeH, dimText, Color4(1.0f, 1.0f, 1.0f, 0.95f));

        font.render(windowW, windowH);

        // F. Crop Mode Top Bar
        font.beginBatch();
        Color4 barBg = pal.barBg;
        Color4 barBorder = pal.barBorder;
        font.addRect(0, 0, (float)windowW, topBarH, barBg);
        font.addRect(0, topBarH - 1.0f, (float)windowW, 1.0f, barBorder);

        float btnH = std::max(28.0f * uiScale, font.textHeight() + 12.0f * uiScale);
        float btnY = (topBarH - btnH) * 0.5f;

        // Left: "Cancel" button
        float cancelBtnW = 86.0f * uiScale;
        cropState.btnCancel.x = 16.0f * uiScale;
        cropState.btnCancel.y = btnY;
        cropState.btnCancel.w = cancelBtnW;
        cropState.btnCancel.h = btnH;
        cropState.btnCancel.isHovered = isInside(mouseX, mouseY, cropState.btnCancel.x, cropState.btnCancel.y, cropState.btnCancel.w, cropState.btnCancel.h);

        Color4 cancelBg = cropState.btnCancel.isHovered ? pal.btnHover : Color4(0, 0, 0, 0);
        font.addRoundedRect(cropState.btnCancel.x, cropState.btnCancel.y, cropState.btnCancel.w, cropState.btnCancel.h, 6.0f * uiScale, cancelBg);
        if (cropState.btnCancel.isHovered) {
            font.addRoundedBorder(cropState.btnCancel.x, cropState.btnCancel.y, cropState.btnCancel.w, cropState.btnCancel.h, 6.0f * uiScale, 1.0f, pal.btnBorder);
        }
        float cancelIconS = 16.0f * uiScale;
        iconAtlas.drawIcon(font, ICON_CLOSE, cropState.btnCancel.x + 8.0f * uiScale, btnY + (btnH - cancelIconS) * 0.5f, cancelIconS, cancelIconS, pal.textPrimary);
        font.addTextVCentered(cropState.btnCancel.x + 28.0f * uiScale, btnY, btnH, "Cancel", pal.textPrimary);

        // Center: Filename
        std::string titleText = meta.fileName.empty() ? "Image Crop & Edit" : meta.fileName;
        float titleW = font.measureText(titleText);
        float titleX = ((float)windowW - titleW) * 0.5f;
        font.addTextVCentered(titleX, btnY, btnH, titleText, pal.textPrimary);

        // Right: "Save ∨" button
        float saveBtnW = 96.0f * uiScale;
        cropState.btnSaveDropdown.x = (float)windowW - saveBtnW - 16.0f * uiScale;
        cropState.btnSaveDropdown.y = btnY;
        cropState.btnSaveDropdown.w = saveBtnW;
        cropState.btnSaveDropdown.h = btnH;
        cropState.btnSaveDropdown.isHovered = isInside(mouseX, mouseY, cropState.btnSaveDropdown.x, cropState.btnSaveDropdown.y, cropState.btnSaveDropdown.w, cropState.btnSaveDropdown.h);

        Color4 saveBg = cropState.showSaveMenu ? pal.accentHover : (cropState.btnSaveDropdown.isHovered ? pal.accentHover : pal.accent);
        font.addRoundedRect(cropState.btnSaveDropdown.x, cropState.btnSaveDropdown.y, cropState.btnSaveDropdown.w, cropState.btnSaveDropdown.h, 6.0f * uiScale, saveBg);
        font.addRoundedBorder(cropState.btnSaveDropdown.x, cropState.btnSaveDropdown.y, cropState.btnSaveDropdown.w, cropState.btnSaveDropdown.h, 6.0f * uiScale, 1.0f, pal.accentHover);

        float saveIconS = 16.0f * uiScale;
        iconAtlas.drawIcon(font, ICON_SAVE, cropState.btnSaveDropdown.x + 10.0f * uiScale, btnY + (btnH - saveIconS) * 0.5f, saveIconS, saveIconS, Color4(1, 1, 1, 1));
        font.addTextVCentered(cropState.btnSaveDropdown.x + 30.0f * uiScale, btnY, btnH, "Save", Color4(1, 1, 1, 1));
        iconAtlas.drawIcon(font, ICON_CHEVRON_DOWN, cropState.btnSaveDropdown.x + saveBtnW - 20.0f * uiScale, btnY + (btnH - 14.0f * uiScale) * 0.5f, 14.0f * uiScale, 14.0f * uiScale, Color4(1, 1, 1, 0.9f));

        font.render(windowW, windowH, 0, iconAtlas.textureId);

        // G. Right Floating Control Panel
        float panelW = 210.0f * uiScale;
        float panelX = (float)windowW - panelW - 16.0f * uiScale;
        float panelY = topBarH + 16.0f * uiScale;
        float panelH = 380.0f * uiScale;

        cropState.sidePanelRect.x = panelX;
        cropState.sidePanelRect.y = panelY;
        cropState.sidePanelRect.w = panelW;
        cropState.sidePanelRect.h = panelH;

        font.beginBatch();
        font.addRoundedRect(panelX, panelY, panelW, panelH, 10.0f * uiScale, pal.cardBg);
        font.addRoundedBorder(panelX, panelY, panelW, panelH, 10.0f * uiScale, 1.0f, pal.cardBorder);

        float curPanelY = panelY + 12.0f * uiScale;
        font.addText(panelX + 14.0f * uiScale, curPanelY, "ASPECT RATIO", pal.textSecondary);
        curPanelY += font.textHeight() + 8.0f * uiScale;

        const char* aspectLabels[] = { "Free", "Original", "1:1", "16:9", "4:3", "3:2", "5:4" };
        float colW = (panelW - 28.0f * uiScale - 6.0f * uiScale) * 0.5f;
        float itemBtnH = 26.0f * uiScale;

        for (int i = 0; i < CROP_ASPECT_COUNT; ++i) {
            int col = i % 2;
            int row = i / 2;
            float bx = panelX + 14.0f * uiScale + col * (colW + 6.0f * uiScale);
            float by = curPanelY + row * (itemBtnH + 6.0f * uiScale);
            float bw = (i == CROP_ASPECT_COUNT - 1 && CROP_ASPECT_COUNT % 2 != 0) ? (panelW - 28.0f * uiScale) : colW;

            cropState.aspectBtnRects[i].x = bx;
            cropState.aspectBtnRects[i].y = by;
            cropState.aspectBtnRects[i].w = bw;
            cropState.aspectBtnRects[i].h = itemBtnH;
            cropState.aspectBtnRects[i].isHovered = isInside(mouseX, mouseY, bx, by, bw, itemBtnH);

            bool isSel = (cropState.aspectMode == (CropAspectMode)i);
            Color4 abg = isSel ? pal.accent : (cropState.aspectBtnRects[i].isHovered ? pal.btnHover : pal.btnBg);
            font.addRoundedRect(bx, by, bw, itemBtnH, 5.0f * uiScale, abg);
            if (isSel) {
                font.addRoundedBorder(bx, by, bw, itemBtnH, 5.0f * uiScale, 1.0f, pal.accentHover);
            } else if (cropState.aspectBtnRects[i].isHovered) {
                font.addRoundedBorder(bx, by, bw, itemBtnH, 5.0f * uiScale, 1.0f, pal.btnBorder);
            }

            Color4 tc = isSel ? Color4(1, 1, 1, 1) : pal.textPrimary;
            float tw = font.measureText(aspectLabels[i]);
            font.addTextVCentered(bx + (bw - tw) * 0.5f, by, itemBtnH, aspectLabels[i], tc);
        }
        curPanelY += ((CROP_ASPECT_COUNT + 1) / 2) * (itemBtnH + 6.0f * uiScale) + 4.0f * uiScale;

        // Landscape vs Portrait Toggle Row
        float orientBtnW = (panelW - 28.0f * uiScale - 6.0f * uiScale) * 0.5f;
        cropState.btnLandscape.x = panelX + 14.0f * uiScale;
        cropState.btnLandscape.y = curPanelY;
        cropState.btnLandscape.w = orientBtnW;
        cropState.btnLandscape.h = itemBtnH;
        cropState.btnLandscape.isHovered = isInside(mouseX, mouseY, cropState.btnLandscape.x, cropState.btnLandscape.y, orientBtnW, itemBtnH);

        cropState.btnPortrait.x = panelX + 14.0f * uiScale + orientBtnW + 6.0f * uiScale;
        cropState.btnPortrait.y = curPanelY;
        cropState.btnPortrait.w = orientBtnW;
        cropState.btnPortrait.h = itemBtnH;
        cropState.btnPortrait.isHovered = isInside(mouseX, mouseY, cropState.btnPortrait.x, cropState.btnPortrait.y, orientBtnW, itemBtnH);

        bool isLand = !cropState.isPortrait;
        Color4 lBg = isLand ? pal.accent : (cropState.btnLandscape.isHovered ? pal.btnHover : pal.btnBg);
        font.addRoundedRect(cropState.btnLandscape.x, curPanelY, orientBtnW, itemBtnH, 5.0f * uiScale, lBg);
        if (isLand) font.addRoundedBorder(cropState.btnLandscape.x, curPanelY, orientBtnW, itemBtnH, 5.0f * uiScale, 1.0f, pal.accentHover);
        else if (cropState.btnLandscape.isHovered) font.addRoundedBorder(cropState.btnLandscape.x, curPanelY, orientBtnW, itemBtnH, 5.0f * uiScale, 1.0f, pal.btnBorder);

        Color4 ltc = isLand ? Color4(1, 1, 1, 1) : pal.textPrimary;
        float ltw = font.measureText("Landscape");
        font.addTextVCentered(cropState.btnLandscape.x + (orientBtnW - ltw) * 0.5f, curPanelY, itemBtnH, "Landscape", ltc);

        bool isPort = cropState.isPortrait;
        Color4 pBg = isPort ? pal.accent : (cropState.btnPortrait.isHovered ? pal.btnHover : pal.btnBg);
        font.addRoundedRect(cropState.btnPortrait.x, curPanelY, orientBtnW, itemBtnH, 5.0f * uiScale, pBg);
        if (isPort) font.addRoundedBorder(cropState.btnPortrait.x, curPanelY, orientBtnW, itemBtnH, 5.0f * uiScale, 1.0f, pal.accentHover);
        else if (cropState.btnPortrait.isHovered) font.addRoundedBorder(cropState.btnPortrait.x, curPanelY, orientBtnW, itemBtnH, 5.0f * uiScale, 1.0f, pal.btnBorder);

        Color4 ptc = isPort ? Color4(1, 1, 1, 1) : pal.textPrimary;
        float ptw = font.measureText("Portrait");
        font.addTextVCentered(cropState.btnPortrait.x + (orientBtnW - ptw) * 0.5f, curPanelY, itemBtnH, "Portrait", ptc);

        curPanelY += itemBtnH + 12.0f * uiScale;

        font.addText(panelX + 14.0f * uiScale, curPanelY, "TRANSFORM", pal.textSecondary);
        curPanelY += font.textHeight() + 8.0f * uiScale;

        float tfBtnW = (panelW - 28.0f * uiScale - 18.0f * uiScale) / 4.0f;
        float tfBtnH = 30.0f * uiScale;
        float tfIconS = 16.0f * uiScale;

        auto setupTfBtn = [&](CropUIRect& r, float x, IconType icon) {
            r.x = x; r.y = curPanelY; r.w = tfBtnW; r.h = tfBtnH;
            r.isHovered = isInside(mouseX, mouseY, x, curPanelY, tfBtnW, tfBtnH);
            Color4 bg = r.isHovered ? pal.btnHover : pal.btnBg;
            font.addRoundedRect(x, curPanelY, tfBtnW, tfBtnH, 5.0f * uiScale, bg);
            if (r.isHovered) font.addRoundedBorder(x, curPanelY, tfBtnW, tfBtnH, 5.0f * uiScale, 1.0f, pal.btnBorder);
            iconAtlas.drawIcon(font, icon, x + (tfBtnW - tfIconS) * 0.5f, curPanelY + (tfBtnH - tfIconS) * 0.5f, tfIconS, tfIconS, pal.textPrimary);
        };

        setupTfBtn(cropState.btnRotateCCW, panelX + 14.0f * uiScale, ICON_ROTATE_CCW);
        setupTfBtn(cropState.btnRotateCW, panelX + 14.0f * uiScale + (tfBtnW + 6.0f * uiScale), ICON_ROTATE_CW);
        setupTfBtn(cropState.btnFlipH, panelX + 14.0f * uiScale + (tfBtnW + 6.0f * uiScale) * 2.0f, ICON_FLIP_H);
        setupTfBtn(cropState.btnFlipV, panelX + 14.0f * uiScale + (tfBtnW + 6.0f * uiScale) * 3.0f, ICON_FLIP_V);

        curPanelY += tfBtnH + 14.0f * uiScale;

        font.addLine(panelX + 14.0f * uiScale, curPanelY, panelX + panelW - 14.0f * uiScale, curPanelY, 1.0f, pal.cardBorder);
        curPanelY += 10.0f * uiScale;

        float rstW = panelW - 28.0f * uiScale;
        float rstH = 28.0f * uiScale;
        cropState.btnReset.x = panelX + 14.0f * uiScale;
        cropState.btnReset.y = curPanelY;
        cropState.btnReset.w = rstW;
        cropState.btnReset.h = rstH;
        cropState.btnReset.isHovered = isInside(mouseX, mouseY, cropState.btnReset.x, cropState.btnReset.y, rstW, rstH);

        Color4 rstBg = cropState.btnReset.isHovered ? pal.btnHover : pal.btnBg;
        font.addRoundedRect(cropState.btnReset.x, curPanelY, rstW, rstH, 5.0f * uiScale, rstBg);
        if (cropState.btnReset.isHovered) {
            font.addRoundedBorder(cropState.btnReset.x, curPanelY, rstW, rstH, 5.0f * uiScale, 1.0f, pal.btnBorder);
        }
        float rstIconS = 14.0f * uiScale;
        float rstTextW = font.measureText("Reset All");
        float rstTotalW = rstIconS + 8.0f * uiScale + rstTextW;
        float rstStartX = cropState.btnReset.x + (rstW - rstTotalW) * 0.5f;
        iconAtlas.drawIcon(font, ICON_REFRESH, rstStartX, curPanelY + (rstH - rstIconS) * 0.5f, rstIconS, rstIconS, pal.textPrimary);
        font.addTextVCentered(rstStartX + rstIconS + 8.0f * uiScale, curPanelY, rstH, "Reset All", pal.textPrimary);

        font.render(windowW, windowH, 0, iconAtlas.textureId);

        // H. Save Dropdown Popover Menu (when active)
        if (cropState.showSaveMenu) {
            float popW = 290.0f * uiScale;
            float popH = 122.0f * uiScale;
            float popX = (float)windowW - popW - 16.0f * uiScale;
            float popY = topBarH + 6.0f * uiScale;

            cropState.saveMenuRect.x = popX;
            cropState.saveMenuRect.y = popY;
            cropState.saveMenuRect.w = popW;
            cropState.saveMenuRect.h = popH;

            font.beginBatch();
            font.addRoundedRect(popX + 2.0f, popY + 4.0f, popW, popH, 10.0f * uiScale, Color4(0, 0, 0, 0.45f));
            font.addRoundedRect(popX, popY, popW, popH, 10.0f * uiScale, pal.cardBg);
            font.addRoundedBorder(popX, popY, popW, popH, 10.0f * uiScale, 1.0f, pal.cardBorder);

            float optH = 48.0f * uiScale;
            float optPad = 7.0f * uiScale;

            // Option 1: "Save As..."
            cropState.optSaveAsRect.x = popX + optPad;
            cropState.optSaveAsRect.y = popY + optPad;
            cropState.optSaveAsRect.w = popW - optPad * 2.0f;
            cropState.optSaveAsRect.h = optH;
            cropState.optSaveAsRect.isHovered = isInside(mouseX, mouseY, cropState.optSaveAsRect.x, cropState.optSaveAsRect.y, cropState.optSaveAsRect.w, optH);

            Color4 opt1Bg = cropState.optSaveAsRect.isHovered ? pal.btnHover : Color4(0, 0, 0, 0);
            font.addRoundedRect(cropState.optSaveAsRect.x, cropState.optSaveAsRect.y, cropState.optSaveAsRect.w, optH, 7.0f * uiScale, opt1Bg);
            if (cropState.optSaveAsRect.isHovered) {
                font.addRoundedBorder(cropState.optSaveAsRect.x, cropState.optSaveAsRect.y, cropState.optSaveAsRect.w, optH, 7.0f * uiScale, 1.0f, pal.btnBorder);
            }
            float badgeSize = 30.0f * uiScale;
            float badgeX = cropState.optSaveAsRect.x + 8.0f * uiScale;
            float badgeY1 = cropState.optSaveAsRect.y + (optH - badgeSize) * 0.5f;
            Color4 badgeBg1 = pal.isDark ? Color4(0.12f, 0.45f, 0.90f, 0.22f) : Color4(0.15f, 0.50f, 0.95f, 0.15f);
            font.addRoundedRect(badgeX, badgeY1, badgeSize, badgeSize, 6.0f * uiScale, badgeBg1);
            float optIconS = 16.0f * uiScale;
            iconAtlas.drawIcon(font, ICON_SAVE, badgeX + (badgeSize - optIconS) * 0.5f, badgeY1 + (badgeSize - optIconS) * 0.5f, optIconS, optIconS, pal.accent);

            float textX = cropState.optSaveAsRect.x + 46.0f * uiScale;
            font.addText(textX, cropState.optSaveAsRect.y + 7.0f * uiScale, "Save As...", pal.textPrimary);
            font.addText(textX, cropState.optSaveAsRect.y + 25.0f * uiScale, "Keep original and create new file", pal.textSecondary);

            // Option 2: "Replace Original"
            cropState.optReplaceRect.x = popX + optPad;
            cropState.optReplaceRect.y = popY + optPad + optH + 4.0f * uiScale;
            cropState.optReplaceRect.w = popW - optPad * 2.0f;
            cropState.optReplaceRect.h = optH;
            cropState.optReplaceRect.isHovered = isInside(mouseX, mouseY, cropState.optReplaceRect.x, cropState.optReplaceRect.y, cropState.optReplaceRect.w, optH);

            Color4 opt2Bg = cropState.optReplaceRect.isHovered ? pal.btnHover : Color4(0, 0, 0, 0);
            font.addRoundedRect(cropState.optReplaceRect.x, cropState.optReplaceRect.y, cropState.optReplaceRect.w, optH, 7.0f * uiScale, opt2Bg);
            if (cropState.optReplaceRect.isHovered) {
                font.addRoundedBorder(cropState.optReplaceRect.x, cropState.optReplaceRect.y, cropState.optReplaceRect.w, optH, 7.0f * uiScale, 1.0f, pal.btnBorder);
            }
            float badgeY2 = cropState.optReplaceRect.y + (optH - badgeSize) * 0.5f;
            Color4 badgeBg2 = pal.isDark ? Color4(0.90f, 0.55f, 0.10f, 0.22f) : Color4(0.95f, 0.55f, 0.10f, 0.15f);
            font.addRoundedRect(badgeX, badgeY2, badgeSize, badgeSize, 6.0f * uiScale, badgeBg2);
            iconAtlas.drawIcon(font, ICON_REFRESH, badgeX + (badgeSize - optIconS) * 0.5f, badgeY2 + (badgeSize - optIconS) * 0.5f, optIconS, optIconS, Color4::Hex(0xF59E0B, 1.0f));

            font.addText(textX, cropState.optReplaceRect.y + 7.0f * uiScale, "Replace Original", pal.textPrimary);
            font.addText(textX, cropState.optReplaceRect.y + 25.0f * uiScale, "Overwrite current file directly", pal.textSecondary);

            font.render(windowW, windowH, 0, iconAtlas.textureId);
        }
    }

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

        // =============================================================
        // 0. Crop Mode Interactivity
        // =============================================================
        if (cropState.active) {
            // Save Dropdown Popover Click Handling
            if (cropState.showSaveMenu) {
                if (isInside(mx, my, cropState.optSaveAsRect.x, cropState.optSaveAsRect.y, cropState.optSaveAsRect.w, cropState.optSaveAsRect.h)) {
                    cropState.showSaveMenu = false;
                    act.type = UIAction::CROP_SAVE_AS;
                    return act;
                }
                if (isInside(mx, my, cropState.optReplaceRect.x, cropState.optReplaceRect.y, cropState.optReplaceRect.w, cropState.optReplaceRect.h)) {
                    cropState.showSaveMenu = false;
                    act.type = UIAction::CROP_REPLACE_ORIGINAL;
                    return act;
                }
                if (isInside(mx, my, cropState.saveMenuRect.x, cropState.saveMenuRect.y, cropState.saveMenuRect.w, cropState.saveMenuRect.h)) {
                    return act;
                }
                if (!isInside(mx, my, cropState.btnSaveDropdown.x, cropState.btnSaveDropdown.y, cropState.btnSaveDropdown.w, cropState.btnSaveDropdown.h)) {
                    cropState.showSaveMenu = false;
                }
            }

            // Top Bar Buttons
            if (isInside(mx, my, cropState.btnSaveDropdown.x, cropState.btnSaveDropdown.y, cropState.btnSaveDropdown.w, cropState.btnSaveDropdown.h)) {
                cropState.showSaveMenu = !cropState.showSaveMenu;
                act.type = UIAction::CROP_TOGGLE_SAVE_MENU;
                return act;
            }
            if (isInside(mx, my, cropState.btnCancel.x, cropState.btnCancel.y, cropState.btnCancel.w, cropState.btnCancel.h)) {
                cropState.exit();
                act.type = UIAction::CROP_CANCEL;
                return act;
            }

            // Side Panel Buttons
            int nativeW = (int)imgW;
            int nativeH = (int)imgH;
            for (int i = 0; i < CROP_ASPECT_COUNT; ++i) {
                if (isInside(mx, my, cropState.aspectBtnRects[i].x, cropState.aspectBtnRects[i].y, cropState.aspectBtnRects[i].w, cropState.aspectBtnRects[i].h)) {
                    cropState.applyAspectMode((CropAspectMode)i, nativeW, nativeH);
                    act.type = UIAction::CROP_SET_ASPECT;
                    return act;
                }
            }
            if (isInside(mx, my, cropState.btnLandscape.x, cropState.btnLandscape.y, cropState.btnLandscape.w, cropState.btnLandscape.h)) {
                if (cropState.isPortrait) {
                    cropState.isPortrait = false;
                    cropState.applyAspectMode(cropState.aspectMode, nativeW, nativeH);
                }
                act.type = UIAction::CROP_TOGGLE_ORIENTATION;
                return act;
            }
            if (isInside(mx, my, cropState.btnPortrait.x, cropState.btnPortrait.y, cropState.btnPortrait.w, cropState.btnPortrait.h)) {
                if (!cropState.isPortrait) {
                    cropState.isPortrait = true;
                    cropState.applyAspectMode(cropState.aspectMode, nativeW, nativeH);
                }
                act.type = UIAction::CROP_TOGGLE_ORIENTATION;
                return act;
            }
            if (isInside(mx, my, cropState.btnRotateCCW.x, cropState.btnRotateCCW.y, cropState.btnRotateCCW.w, cropState.btnRotateCCW.h)) {
                cropState.rotation = (cropState.rotation - 90 + 360) % 360;
                act.type = UIAction::CROP_ROTATE_CCW;
                return act;
            }
            if (isInside(mx, my, cropState.btnRotateCW.x, cropState.btnRotateCW.y, cropState.btnRotateCW.w, cropState.btnRotateCW.h)) {
                cropState.rotation = (cropState.rotation + 90) % 360;
                act.type = UIAction::CROP_ROTATE_CW;
                return act;
            }
            if (isInside(mx, my, cropState.btnFlipH.x, cropState.btnFlipH.y, cropState.btnFlipH.w, cropState.btnFlipH.h)) {
                cropState.flipH = !cropState.flipH;
                act.type = UIAction::CROP_FLIP_H;
                return act;
            }
            if (isInside(mx, my, cropState.btnFlipV.x, cropState.btnFlipV.y, cropState.btnFlipV.w, cropState.btnFlipV.h)) {
                cropState.flipV = !cropState.flipV;
                act.type = UIAction::CROP_FLIP_V;
                return act;
            }
            if (isInside(mx, my, cropState.btnReset.x, cropState.btnReset.y, cropState.btnReset.w, cropState.btnReset.h)) {
                cropState.resetCrop(nativeW, nativeH);
                cropState.rotation = 0;
                cropState.flipH = false;
                cropState.flipV = false;
                cropState.aspectMode = CROP_ASPECT_FREE;
                cropState.isPortrait = false;
                act.type = UIAction::CROP_RESET;
                return act;
            }
            if (isInside(mx, my, cropState.sidePanelRect.x, cropState.sidePanelRect.y, cropState.sidePanelRect.w, cropState.sidePanelRect.h)) {
                return act;
            }
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
        if (isInside(mx, my, btnCrop.x, btnCrop.y, btnCrop.w, btnCrop.h)) {
            cropState.enter((int)imgW, (int)imgH);
            act.type = UIAction::TOGGLE_CROP_MODE;
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
        cropState.activeHandle = HANDLE_NONE;
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
            // Keep the canvas clean until the pointer enters an edge navigation
            // zone. Only available directions are shown.
            float zoneW = std::min(180.0f, (float)windowW * 0.22f);
            bool hoverPrev = currentFileIdx > 0 && mouseX <= zoneW;
            bool hoverNext = currentFileIdx + 1 < (int)fileList.size() &&
                             mouseX >= (float)windowW - zoneW;
            if (hoverPrev || hoverNext) {
                float size = 52.0f * uiScale;
                float x = hoverPrev ? 20.0f * uiScale
                                    : (float)windowW - size - 20.0f * uiScale;
                float y = ((float)windowH - size) * 0.5f;
                font.beginBatch();
                font.addRoundedRect(x + 2.0f, y + 2.0f, size, size, size * 0.5f,
                                    Color4(0, 0, 0, 0.35f));
                font.addRoundedRect(x, y, size, size, size * 0.5f,
                                    Color4(0.06f, 0.08f, 0.10f, 0.78f));
                font.addRoundedBorder(x, y, size, size, size * 0.5f, 1.0f,
                                      Color4(1, 1, 1, 0.20f));
                float iconSize = 26.0f * uiScale;
                iconAtlas.drawIcon(font, hoverPrev ? ICON_CHEVRON_LEFT : ICON_CHEVRON_RIGHT,
                                   x + (size - iconSize) * 0.5f,
                                   y + (size - iconSize) * 0.5f,
                                   iconSize, iconSize, Color4(1, 1, 1, 0.95f));
                font.render(windowW, windowH, 0, iconAtlas.textureId);
            }
            return;
        }

        // =============================================================
        // 0. CROP & EDIT MODE (Dedicated Fullscreen Editor UI)
        // =============================================================
        if (cropState.active) {
            renderCropUI(windowW, windowH, mouseX, mouseY, meta, zoomScale, imgPosX, imgPosY, currentRotation);
            return;
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

                thumbs.requestThumbnail(fileList[i], true, 160);
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
                    thumbs.touch(fileList[i]);
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
        // B. QUIET FIRST-LOAD INDICATOR
        // =============================================================
        // Navigation normally has a thumbnail ready, so never cover it with a
        // decoding banner. Only a truly blank first load gets a small,
        // text-free activity mark.
        if (isImageDecoding && !isGridView && currentTextureId == 0) {
            font.beginBatch();
            float pulse = 0.5f + 0.5f * sinf(shimmerPhase);
            Color4 pulseCol = pal.accent;
            pulseCol.a = 0.35f + 0.55f * pulse;
            float dot = 8.0f * uiScale;
            font.addRoundedRect(((float)windowW - dot) * 0.5f, topBarH + 18.0f,
                                dot, dot, dot * 0.5f, pulseCol);
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

            float btnW = 32.0f * uiScale;
            float btnH = std::max(26.0f * uiScale, font.textHeight() + 12.0f * uiScale);
            float btnY = (topBarH - btnH) * 0.5f;
            float curBtnX = 12.0f * uiScale;
            float gap = 4.0f * uiScale;
            float iconSize = std::min(btnW, btnH) - 10.0f * uiScale;

            auto setupBtn = [&](Rect& btn, float x, bool active = false) {
                btn.x = x; btn.y = btnY; btn.w = btnW; btn.h = btnH;
                btn.isHovered = isInside(mouseX, mouseY, x, btnY, btnW, btnH);
                Color4 bg = active ? pal.accent : (btn.isHovered ? pal.btnHover : Color4(0,0,0,0));
                bg.a *= uiAlpha;
                font.addRoundedRect(x, btnY, btnW, btnH, 5.0f * uiScale, bg);
                if (active) {
                    Color4 ah = pal.accentHover; ah.a *= uiAlpha;
                    font.addRoundedBorder(x, btnY, btnW, btnH, 5.0f * uiScale, 1.0f, ah);
                } else if (btn.isHovered) {
                    Color4 bb = pal.btnBorder; bb.a *= uiAlpha;
                    font.addRoundedBorder(x, btnY, btnW, btnH, 5.0f * uiScale, 1.0f, bb);
                }
            };

            auto drawBtnIcon = [&](IconType type, float x, Color4 col) {
                float ix = x + (btnW - iconSize) * 0.5f;
                float iy = btnY + (btnH - iconSize) * 0.5f;
                iconAtlas.drawIcon(font, type, ix, iy, iconSize, iconSize, col);
            };

            // [ ⤢ Fit ]
            bool isFit = (!isGridView && fabs(zoomScale - fitScale) < 0.01f);
            setupBtn(btnFit, curBtnX, isFit);
            Color4 cFit = isFit ? Color4(1,1,1,1) : pal.textPrimary; cFit.a *= uiAlpha;
            drawBtnIcon(ICON_FIT, curBtnX, cFit);
            curBtnX += btnW + gap;

            // [ 1:1 ]
            bool is1to1 = (!isGridView && fabs(zoomScale - 1.0f) < 0.01f);
            setupBtn(btn1to1, curBtnX, is1to1);
            Color4 c1 = is1to1 ? Color4(1,1,1,1) : pal.textPrimary; c1.a *= uiAlpha;
            drawBtnIcon(ICON_1TO1, curBtnX, c1);
            curBtnX += btnW + gap;

            // [ ⌖ Center View ]
            setupBtn(btnCenter, curBtnX);
            Color4 cC = pal.textPrimary; cC.a *= uiAlpha;
            drawBtnIcon(ICON_TARGET, curBtnX, cC);
            curBtnX += btnW + gap;

            // [ ⟳ Rotate ]
            setupBtn(btnRotate, curBtnX);
            Color4 cR = pal.textPrimary; cR.a *= uiAlpha;
            drawBtnIcon(ICON_ROTATE, curBtnX, cR);
            curBtnX += btnW + gap;

            // [ ✂ Crop & Edit ]
            setupBtn(btnCrop, curBtnX, cropState.active);
            Color4 cCrop = cropState.active ? Color4(1, 1, 1, 1) : pal.textPrimary; cCrop.a *= uiAlpha;
            drawBtnIcon(ICON_CROP, curBtnX, cCrop);
            curBtnX += btnW + gap;

            // [ #✓ Grid Gallery View Toggle ]
            setupBtn(btnGridView, curBtnX, isGridView);
            Color4 cG = isGridView ? Color4(1,1,1,1) : pal.textPrimary; cG.a *= uiAlpha;
            drawBtnIcon(ICON_GRID_CHECK, curBtnX, cG);
            curBtnX += btnW + gap + 8.0f * uiScale;

            // Separator
            font.addLine(curBtnX, btnY + 4.0f * uiScale, curBtnX, btnY + btnH - 4.0f * uiScale, 1.0f, topBorder);
            curBtnX += 9.0f * uiScale;

            // [ ◐ Theme ]
            setupBtn(btnTheme, curBtnX, showThemeMenu);
            Color4 cT = showThemeMenu ? Color4(1, 1, 1, 1) : pal.textPrimary; cT.a *= uiAlpha;
            drawBtnIcon(theme.isDarkMode ? ICON_THEME_DARK : ICON_THEME_LIGHT, curBtnX, cT);
            curBtnX += btnW + gap;

            // -------------------------------------------------------------
            // D. TOP RIGHT METADATA HEADER BUTTON (State 0, Responsive)
            // -------------------------------------------------------------
            float availHeaderSpace = std::max(36.0f, (float)windowW - curBtnX - 24.0f);
            float maxTitleW = std::clamp(availHeaderSpace - 40.0f, 0.0f, 260.0f);
            std::string headerTitle = meta.fileName.empty() ? "No File" : meta.fileName;
            std::string headerTrunc = (maxTitleW > 24.0f) ? truncateText(headerTitle, maxTitleW) : "";
            float htw = headerTrunc.empty() ? 0.0f : font.measureText(headerTrunc);
            float hbw = headerTrunc.empty() ? (32.0f * uiScale) : (htw + 34.0f * uiScale);
            float hbx = (float)windowW - hbw - 14.0f * uiScale;
            float hby = (topBarH - btnH) * 0.5f;

            headerLabelRect.x = hbx; headerLabelRect.y = hby; headerLabelRect.w = hbw; headerLabelRect.h = btnH;
            headerLabelRect.isHovered = isInside(mouseX, mouseY, hbx, hby, hbw, btnH);

            Color4 headBg = (metadataState > 0) ? pal.cardHeaderBg : (headerLabelRect.isHovered ? pal.btnHover : Color4(0,0,0,0));
            headBg.a *= uiAlpha;
            font.addRoundedRect(hbx, hby, hbw, btnH, 5.0f * uiScale, headBg);
            if (metadataState > 0 || headerLabelRect.isHovered) {
                Color4 hb = pal.btnBorder; hb.a *= uiAlpha;
                font.addRoundedBorder(hbx, hby, hbw, btnH, 5.0f * uiScale, 1.0f, hb);
            }
            if (!headerTrunc.empty()) {
                Color4 headText = pal.textPrimary; headText.a *= uiAlpha;
                font.addTextVCentered(hbx + 10.0f * uiScale, hby, btnH, headerTrunc, headText);
            }
            Color4 cArr = pal.textSecondary; cArr.a *= uiAlpha;
            float chevSize = 16.0f * uiScale;
            float chevX = headerTrunc.empty() ? (hbx + (hbw - chevSize) * 0.5f) : (hbx + hbw - chevSize - 8.0f * uiScale);
            float chevY = hby + (btnH - chevSize) * 0.5f;
            iconAtlas.drawIcon(font, (metadataState > 0) ? ICON_CHEVRON_DOWN : (headerTrunc.empty() ? ICON_INFO : ICON_CHEVRON_RIGHT),
                               chevX, chevY, chevSize, chevSize, cArr);

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

                    thumbs.requestThumbnail(fileList[i], true, 160);
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
            float zIconS = 14.0f * uiScale;
            iconAtlas.drawIcon(font, showZoomMenu ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN,
                               zx + zw - zIconS - 8.0f * uiScale, zy + (zh - zIconS) * 0.5f, zIconS, zIconS, cArr);
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

            float bx1 = cardX + cardW - 28.0f * uiScale;
            float bx2 = cardX + cardW - 54.0f * uiScale;
            float btnPopS = 24.0f * uiScale;
            btnPopupClose.x = bx1; btnPopupClose.y = cardY + 4.0f * uiScale; btnPopupClose.w = btnPopS; btnPopupClose.h = btnPopS;
            btnPopupExpand.x = bx2; btnPopupExpand.y = cardY + 4.0f * uiScale; btnPopupExpand.w = btnPopS; btnPopupExpand.h = btnPopS;

            btnPopupClose.isHovered = isInside(mouseX, mouseY, btnPopupClose.x, btnPopupClose.y, btnPopupClose.w, btnPopupClose.h);
            btnPopupExpand.isHovered = isInside(mouseX, mouseY, btnPopupExpand.x, btnPopupExpand.y, btnPopupExpand.w, btnPopupExpand.h);

            if (btnPopupClose.isHovered) font.addRoundedRect(btnPopupClose.x, btnPopupClose.y, btnPopS, btnPopS, 4.0f * uiScale, pal.btnHover);
            if (btnPopupExpand.isHovered) font.addRoundedRect(btnPopupExpand.x, btnPopupExpand.y, btnPopS, btnPopS, 4.0f * uiScale, pal.btnHover);

            float popCloseS = 14.0f * uiScale;
            iconAtlas.drawIcon(font, ICON_CLOSE,
                               btnPopupClose.x + (btnPopS - popCloseS) * 0.5f,
                               btnPopupClose.y + (btnPopS - popCloseS) * 0.5f, popCloseS, popCloseS, pal.textSecondary);
            iconAtlas.drawIcon(font, (metadataState == 2) ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN,
                               btnPopupExpand.x + (btnPopS - popCloseS) * 0.5f,
                               btnPopupExpand.y + (btnPopS - popCloseS) * 0.5f, popCloseS, popCloseS, pal.textSecondary);

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
                    thumbs.touch(fileList[i]);
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
                float optIconS = 18.0f * uiScale;
                iconAtlas.drawIcon(font, to.icon, optX + 8.0f * uiScale, optY + (optH - optIconS) * 0.5f, optIconS, optIconS, iconCol);
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
