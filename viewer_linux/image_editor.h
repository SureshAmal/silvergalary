#pragma once

#include "gl_loader.h"
#include "font.h"
#include "theme.h"
#include "icons.h"
#include "silver_codec.h"
#include "folder.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

enum CropAspectMode {
    CROP_ASPECT_FREE = 0,
    CROP_ASPECT_ORIGINAL,
    CROP_ASPECT_1_1,
    CROP_ASPECT_16_9,
    CROP_ASPECT_4_3,
    CROP_ASPECT_3_2,
    CROP_ASPECT_5_4,
    CROP_ASPECT_COUNT
};

enum CropDragHandle {
    HANDLE_NONE = 0,
    HANDLE_BOX,
    HANDLE_CORNER_TL,
    HANDLE_CORNER_TR,
    HANDLE_CORNER_BL,
    HANDLE_CORNER_BR,
    HANDLE_EDGE_TOP,
    HANDLE_EDGE_BOTTOM,
    HANDLE_EDGE_LEFT,
    HANDLE_EDGE_RIGHT
};

struct CropUIRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool isHovered = false;
};

struct CropEditState {
    bool active = false;

    // Normalized crop rectangle [0..1] within the oriented image
    float cropX = 0.0f;
    float cropY = 0.0f;
    float cropW = 1.0f;
    float cropH = 1.0f;

    // Incremental transforms in edit mode
    int rotation = 0;      // 0, 90, 180, 270 (degrees clockwise)
    bool flipH = false;
    bool flipV = false;

    CropAspectMode aspectMode = CROP_ASPECT_FREE;
    bool isPortrait = false;

    // Save menu popover state
    bool showSaveMenu = false;
    float saveMenuAnim = 0.0f;

    // Drag interaction
    CropDragHandle activeHandle = HANDLE_NONE;
    CropDragHandle hoveredHandle = HANDLE_NONE;

    double dragStartMouseX = 0.0;
    double dragStartMouseY = 0.0;
    float dragStartCropX = 0.0f;
    float dragStartCropY = 0.0f;
    float dragStartCropW = 1.0f;
    float dragStartCropH = 1.0f;

    // Clickable UI Rectangles in Crop Mode
    CropUIRect btnCancel;
    CropUIRect btnSaveDropdown;
    CropUIRect saveMenuRect;
    CropUIRect optSaveAsRect;
    CropUIRect optReplaceRect;

    // Side panel rects
    CropUIRect sidePanelRect;
    CropUIRect aspectBtnRects[CROP_ASPECT_COUNT];
    CropUIRect btnLandscape;
    CropUIRect btnPortrait;
    CropUIRect btnRotateCCW;
    CropUIRect btnRotateCW;
    CropUIRect btnFlipH;
    CropUIRect btnFlipV;
    CropUIRect btnReset;

    void enter(int imgW, int imgH) {
        active = true;
        rotation = 0;
        flipH = false;
        flipV = false;
        aspectMode = CROP_ASPECT_FREE;
        isPortrait = false;
        showSaveMenu = false;
        saveMenuAnim = 0.0f;
        activeHandle = HANDLE_NONE;
        hoveredHandle = HANDLE_NONE;
        resetCrop(imgW, imgH);
    }

    void exit() {
        active = false;
        showSaveMenu = false;
        activeHandle = HANDLE_NONE;
        hoveredHandle = HANDLE_NONE;
    }

    void resetCrop(int imgW, int imgH) {
        cropX = 0.0f;
        cropY = 0.0f;
        cropW = 1.0f;
        cropH = 1.0f;
    }

    float getTargetRatio(int nativeW, int nativeH) const {
        int w = nativeW > 0 ? nativeW : 1920;
        int h = nativeH > 0 ? nativeH : 1080;
        if (rotation == 90 || rotation == 270) std::swap(w, h);

        float r = 1.0f;
        switch (aspectMode) {
            case CROP_ASPECT_FREE: return 0.0f;
            case CROP_ASPECT_ORIGINAL: r = (float)w / (float)h; break;
            case CROP_ASPECT_1_1:  r = 1.0f; break;
            case CROP_ASPECT_16_9: r = 16.0f / 9.0f; break;
            case CROP_ASPECT_4_3:  r = 4.0f / 3.0f; break;
            case CROP_ASPECT_3_2:  r = 3.0f / 2.0f; break;
            case CROP_ASPECT_5_4:  r = 5.0f / 4.0f; break;
            default: return 0.0f;
        }
        if (isPortrait && aspectMode != CROP_ASPECT_1_1 && r > 0.001f) {
            r = 1.0f / r;
        }
        return r;
    }

    void applyAspectMode(CropAspectMode mode, int nativeW, int nativeH) {
        aspectMode = mode;
        float targetRatio = getTargetRatio(nativeW, nativeH);
        if (targetRatio <= 0.001f) return; // Free aspect ratio

        int curW = nativeW > 0 ? nativeW : 1920;
        int curH = nativeH > 0 ? nativeH : 1080;
        if (rotation == 90 || rotation == 270) std::swap(curW, curH);

        float imageRatio = (float)curW / (float)curH;
        float normRatio = targetRatio / imageRatio; // in normalized coords [0..1]

        float curCenterX = cropX + cropW * 0.5f;
        float curCenterY = cropY + cropH * 0.5f;

        float newW = cropW;
        float newH = cropH;

        if (normRatio >= 1.0f) {
            newW = 1.0f;
            newH = newW / normRatio;
            if (newH > 1.0f) {
                newH = 1.0f;
                newW = newH * normRatio;
            }
        } else {
            newH = 1.0f;
            newW = newH * normRatio;
            if (newW > 1.0f) {
                newW = 1.0f;
                newH = newW / normRatio;
            }
        }

        cropW = std::clamp(newW, 0.05f, 1.0f);
        cropH = std::clamp(newH, 0.05f, 1.0f);
        cropX = std::clamp(curCenterX - cropW * 0.5f, 0.0f, 1.0f - cropW);
        cropY = std::clamp(curCenterY - cropH * 0.5f, 0.0f, 1.0f - cropH);
    }
};

namespace image_editor {

// Transform pixels in-place or return a newly allocated transformed RGBA buffer.
inline unsigned char* transformPixelsRGBA(const unsigned char* src, int w, int h,
                                         int rotDeg, bool flipH, bool flipV,
                                         int* outW, int* outH) {
    if (!src || w <= 0 || h <= 0) return nullptr;
    int curRot = ((rotDeg % 360) + 360) % 360;

    int dstW = (curRot == 90 || curRot == 270) ? h : w;
    int dstH = (curRot == 90 || curRot == 270) ? w : h;

    unsigned char* dst = (unsigned char*)malloc((size_t)dstW * dstH * 4);
    if (!dst) return nullptr;

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            int sx = x;
            int sy = y;

            if (curRot == 90) {
                sx = y;
                sy = h - 1 - x;
            } else if (curRot == 180) {
                sx = w - 1 - x;
                sy = h - 1 - y;
            } else if (curRot == 270) {
                sx = w - 1 - y;
                sy = x;
            }

            if (flipH) sx = w - 1 - sx;
            if (flipV) sy = h - 1 - sy;

            sx = std::clamp(sx, 0, w - 1);
            sy = std::clamp(sy, 0, h - 1);

            const unsigned char* s = src + ((size_t)sy * w + sx) * 4;
            unsigned char* d = dst + ((size_t)y * dstW + x) * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }

    if (outW) *outW = dstW;
    if (outH) *outH = dstH;
    return dst;
}

// Crop RGBA buffer into a newly allocated buffer
inline unsigned char* cropPixelsRGBA(const unsigned char* src, int srcW, int srcH,
                                    int cropX, int cropY, int cropW, int cropH) {
    if (!src || srcW <= 0 || srcH <= 0 || cropW <= 0 || cropH <= 0) return nullptr;
    cropX = std::clamp(cropX, 0, srcW - 1);
    cropY = std::clamp(cropY, 0, srcH - 1);
    cropW = std::clamp(cropW, 1, srcW - cropX);
    cropH = std::clamp(cropH, 1, srcH - cropY);

    unsigned char* dst = (unsigned char*)malloc((size_t)cropW * cropH * 4);
    if (!dst) return nullptr;

    for (int y = 0; y < cropH; ++y) {
        const unsigned char* s = src + ((size_t)(cropY + y) * srcW + cropX) * 4;
        unsigned char* d = dst + (size_t)y * cropW * 4;
        memcpy(d, s, (size_t)cropW * 4);
    }
    return dst;
}

// Save RGBA buffer to disk
inline bool saveImageToFile(const std::string& path, const unsigned char* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0 || path.empty()) return false;

    std::string ext;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        ext = path.substr(dotPos + 1);
        for (char& c : ext) c = (char)tolower(c);
    }

    if (ext == "jpg" || ext == "jpeg") {
        return stbi_write_jpg(path.c_str(), w, h, 4, rgba, 94) != 0;
    } else if (ext == "bmp") {
        return stbi_write_bmp(path.c_str(), w, h, 4, rgba) != 0;
    } else if (ext == "tga") {
        return stbi_write_tga(path.c_str(), w, h, 4, rgba) != 0;
    } else {
        return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
    }
}

// Generate unique save path for "Save As..."
inline std::string generateUniqueSavePath(const std::string& srcPath) {
    std::filesystem::path p(srcPath);
    std::string parent = p.parent_path().string();
    if (parent.empty()) parent = ".";
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    std::string candidate = parent + "/" + stem + "_cropped" + ext;
    if (!std::filesystem::exists(candidate)) return candidate;

    for (int i = 1; i < 10000; ++i) {
        candidate = parent + "/" + stem + "_cropped_" + std::to_string(i) + ext;
        if (!std::filesystem::exists(candidate)) return candidate;
    }
    return candidate;
}

// Perform full crop and save workflow
inline bool executeCropAndSave(const std::string& srcPath, const std::string& outSavedPath,
                               const CropEditState& state, int* outWidth = nullptr, int* outHeight = nullptr) {
    if (srcPath.empty() || outSavedPath.empty()) return false;

    int origW = 0, origH = 0, comp = 0;
    unsigned char* origData = silvercodec::loadRGBA(srcPath, &origW, &origH, &comp);
    if (!origData || origW <= 0 || origH <= 0) return false;

    int orientation = 1;
    if (silvercodec::detectFormat(srcPath) == silvercodec::FMT_JPEG) {
        orientation = silvercodec::readExifOrientation(srcPath);
    }
    if (orientation > 1) {
        origData = silvercodec::applyOrientation(origData, origW, origH, orientation, &origW, &origH);
    }

    // Apply rotation & flip
    int transW = origW, transH = origH;
    unsigned char* transData = transformPixelsRGBA(origData, origW, origH,
                                                   state.rotation, state.flipH, state.flipV,
                                                   &transW, &transH);
    silvercodec::freePixels(origData);
    if (!transData) return false;

    // Calculate crop rectangle in pixel coordinates
    int cropPxX = (int)std::round(state.cropX * transW);
    int cropPxY = (int)std::round(state.cropY * transH);
    int cropPxW = (int)std::round(state.cropW * transW);
    int cropPxH = (int)std::round(state.cropH * transH);

    cropPxX = std::clamp(cropPxX, 0, transW - 1);
    cropPxY = std::clamp(cropPxY, 0, transH - 1);
    cropPxW = std::clamp(cropPxW, 1, transW - cropPxX);
    cropPxH = std::clamp(cropPxH, 1, transH - cropPxY);

    unsigned char* croppedData = cropPixelsRGBA(transData, transW, transH,
                                                cropPxX, cropPxY, cropPxW, cropPxH);
    free(transData);
    if (!croppedData) return false;

    bool ok = saveImageToFile(outSavedPath, croppedData, cropPxW, cropPxH);
    free(croppedData);

    if (ok) {
        if (outWidth) *outWidth = cropPxW;
        if (outHeight) *outHeight = cropPxH;
    }
    return ok;
}

inline bool executeCropAndSave(const std::string& srcPath, const CropEditState& state,
                               bool replaceOriginal, std::string& outSavedPath,
                               int& outWidth, int& outHeight) {
    std::string targetPath = replaceOriginal ? srcPath : generateUniqueSavePath(srcPath);
    bool ok = executeCropAndSave(srcPath, targetPath, state, &outWidth, &outHeight);
    if (ok) {
        outSavedPath = targetPath;
    }
    return ok;
}

} // namespace image_editor
