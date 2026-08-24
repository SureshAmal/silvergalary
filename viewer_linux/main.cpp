#include "gl_loader.h"
#include "image_loader.h"
#include "shaders.h"
#include "folder.h"
#include "ui.h"
#include "async_loader.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include <cmath>

struct AppState {
    GLFWwindow* window = nullptr;
    int windowW = 1280;
    int windowH = 800;
    bool redrawPending = true;   // drives poll-vs-wait in the main loop
    int fbW = 1280;
    int fbH = 800;

    int savedWinX = 100, savedWinY = 100;
    int savedWinW = 1280, savedWinH = 800;
    bool isFullscreen = false;

    FolderScanner folder;
    ImageTexture currentImage;
    AsyncImagePreloader preloader;
    ImageShader imageShader;
    FilePilotUI ui;

    // View Transformation
    float scale = 1.0f;
    float targetScale = 1.0f;
    float scaleVel = 0.0f;
    float posVelX = 0.0f;
    float posVelY = 0.0f;
    float rotationVel = 0.0f;
    float fitScale = 1.0f;

    float posX = 0.0f;
    float posY = 0.0f;
    float targetPosX = 0.0f;
    float targetPosY = 0.0f;
    float fitPosX = 0.0f;
    float fitPosY = 0.0f;

    float rotation = 0.0f;
    float targetRotation = 0.0f;

    // Display Options
    bool nearestFilter = false;
    bool pixelGrid = false;
    int bgMode = 0; // 0=FilePilot Checkerboard, 1=Standard Checker, 2=Black, 3=White

    // Interaction State
    double mouseX = 0, mouseY = 0;
    bool isDragging = false;
    double dragStartX = 0, dragStartY = 0;
    float dragStartPosX = 0, dragStartPosY = 0;

    double lastClickTime = 0.0;
    float swipeAccumX = 0.0f;
    bool cursorIsHidden = false;
};

static AppState g_app;

static void calculateFitParameters(float& outScale, float& outPosX, float& outPosY) {
    if (!g_app.currentImage.isLoaded && g_app.currentImage.width <= 0) return;

    float availW = g_app.ui.presentationMode ? (float)g_app.windowW : ((float)g_app.windowW - 48.0f);
    float availH = g_app.ui.presentationMode ? (float)g_app.windowH : ((float)g_app.windowH - g_app.ui.topBarH - (g_app.ui.showThumbnails ? g_app.ui.bottomStripH : 0.0f) - 32.0f);
    if (availW < 100.0f) availW = 100.0f;
    if (availH < 100.0f) availH = 100.0f;

    float imgW = (float)g_app.currentImage.width;
    float imgH = (float)g_app.currentImage.height;

    int curRotInt = ((int)(g_app.targetRotation + 0.5f) % 360 + 360) % 360;
    if (curRotInt == 90 || curRotInt == 270) {
        std::swap(imgW, imgH);
    }

    float scaleX = availW / imgW;
    float scaleY = availH / imgH;
    outScale = std::min(scaleX, scaleY);

    outPosX = (float)g_app.windowW * 0.5f;
    outPosY = g_app.ui.presentationMode ? ((float)g_app.windowH * 0.5f) : (g_app.ui.topBarH + availH * 0.5f + 16.0f);
}

static void fitToWindow(bool immediate = false) {
    if (!g_app.currentImage.isLoaded && g_app.currentImage.width <= 0) return;
    calculateFitParameters(g_app.fitScale, g_app.fitPosX, g_app.fitPosY);
    g_app.targetScale = g_app.fitScale;
    g_app.targetPosX = g_app.fitPosX;
    g_app.targetPosY = g_app.fitPosY;

    if (immediate) {
        g_app.scale = g_app.targetScale;
        g_app.posX = g_app.targetPosX;
        g_app.posY = g_app.targetPosY;
    }
}

static void set1to1Scale() {
    if (!g_app.currentImage.isLoaded && g_app.currentImage.width <= 0) return;
    g_app.targetScale = 1.0f;
    calculateFitParameters(g_app.fitScale, g_app.fitPosX, g_app.fitPosY);
    g_app.targetPosX = g_app.fitPosX;
    g_app.targetPosY = g_app.fitPosY;
    g_app.ui.showToast("Scale: 100% (1:1 Actual Pixels)");
}

static void centerView() {
    fitToWindow();
    g_app.ui.showToast("View Centered");
}

static void zoomAtCursor(float factor) {
    if ((!g_app.currentImage.isLoaded && g_app.currentImage.width <= 0) || g_app.ui.isGridView) return;

    float oldTargetScale = g_app.targetScale;
    float newTargetScale = oldTargetScale * factor;

    float minScale = g_app.fitScale * 0.95f;
    float maxScale = std::max(g_app.fitScale * 35.0f, 40.0f);

    if (newTargetScale < minScale) newTargetScale = minScale;
    if (newTargetScale > maxScale) newTargetScale = maxScale;

    if (newTargetScale <= g_app.fitScale * 1.02f) {
        g_app.targetScale = g_app.fitScale;
        g_app.targetPosX = g_app.fitPosX;
        g_app.targetPosY = g_app.fitPosY;
        return;
    }

    float cursorX = (float)g_app.mouseX;
    float cursorY = (float)g_app.mouseY;

    g_app.targetPosX = cursorX - (cursorX - g_app.targetPosX) * (newTargetScale / oldTargetScale);
    g_app.targetPosY = cursorY - (cursorY - g_app.targetPosY) * (newTargetScale / oldTargetScale);
    g_app.targetScale = newTargetScale;
}

static void panToMinimapNorm(float normX, float normY) {
    if ((!g_app.currentImage.isLoaded && g_app.currentImage.width <= 0) || g_app.ui.isGridView) return;

    float imgW = (float)g_app.currentImage.width;
    float imgH = (float)g_app.currentImage.height;
    int curRotInt = ((int)(g_app.targetRotation + 0.5f) % 360 + 360) % 360;
    if (curRotInt == 90 || curRotInt == 270) std::swap(imgW, imgH);

    float curRenderW = imgW * g_app.targetScale;
    float curRenderH = imgH * g_app.targetScale;

    float viewCenterX = (float)g_app.windowW * 0.5f;
    float viewCenterY = g_app.ui.topBarH + ((float)g_app.windowH - g_app.ui.topBarH - (g_app.ui.showThumbnails ? g_app.ui.bottomStripH : 0.0f)) * 0.5f;

    g_app.targetPosX = viewCenterX - (normX - 0.5f) * curRenderW;
    g_app.targetPosY = viewCenterY - (normY - 0.5f) * curRenderH;
}

static void applyLoadedImageMeta(const ImageMetadata& meta) {
    // EXIF orientation is baked into the decoded pixels by the codec layer, so
    // the view starts unrotated.
    g_app.targetRotation = 0.0f;
    g_app.rotation = 0.0f;

    fitToWindow(true);

    g_app.ui.thumbs.centerOnIndex(g_app.folder.currentIndex, g_app.folder.count(),
                                  (float)g_app.windowW, g_app.ui.thumbW, g_app.ui.thumbGap);

    char titleBuf[512];
    snprintf(titleBuf, sizeof(titleBuf), "%s - [%d/%d] (%dx%d) - SilverViewer",
             meta.fileName.c_str(),
             g_app.folder.currentIndex + 1, g_app.folder.count(),
             g_app.currentImage.width, g_app.currentImage.height);
    glfwSetWindowTitle(g_app.window, titleBuf);
}

static void loadCurrentFile() {
    if (!g_app.folder.hasFiles()) return;

    std::string path = g_app.folder.currentPath();
    g_app.ui.notifyUserActivity();

    // 1. Fast Header Probe: Instant image dimensions, layout & fitting in microseconds!
    // This guarantees window resizing, zooming, and smooth interaction work immediately
    // even while the high-resolution pixel buffer is decoding in the background!
    g_app.currentImage.probeHeader(path);
    fitToWindow();

    // Update window title immediately
    char titleBuf[512];
    snprintf(titleBuf, sizeof(titleBuf), "%s - [%d/%d] (%dx%d) - SilverViewer",
             g_app.currentImage.meta.fileName.c_str(),
             g_app.folder.currentIndex + 1, g_app.folder.count(),
             g_app.currentImage.width, g_app.currentImage.height);
    glfwSetWindowTitle(g_app.window, titleBuf);

    // 2. Animated GIFs need every composited frame, which the single-frame
    //    preloader cannot carry - decode them straight into the texture.
    if (silvercodec::detectFormat(path) == silvercodec::FMT_GIF) {
        if (g_app.currentImage.load(path)) {
            g_app.currentImage.setFiltering(g_app.nearestFilter);
            applyLoadedImageMeta(g_app.currentImage.meta);
        }
        g_app.ui.thumbs.preloadFolder(g_app.folder.fileList, g_app.folder.currentIndex);
        return;
    }

    // 3. Check if full resolution buffer is already ready in RAM cache
    auto preloaded = g_app.preloader.getIfReady(path);

    if (preloaded && preloaded->data) {
        if (g_app.currentImage.uploadPixels(preloaded->data, preloaded->width, preloaded->height, preloaded->meta)) {
            g_app.currentImage.setFiltering(g_app.nearestFilter);
            applyLoadedImageMeta(preloaded->meta);
        }
    } else {
        // Asynchronous Large-Image Background Loader (Zero Main Thread Stalling!)
        g_app.preloader.requestPrimaryImage(path);
    }

    // 4. Schedule multi-core background pre-loading for neighboring images
    g_app.preloader.updatePreloadTargets(g_app.folder.fileList, g_app.folder.currentIndex);
    g_app.ui.thumbs.preloadFolder(g_app.folder.fileList, g_app.folder.currentIndex);
}

static void toggleFullscreen() {
    g_app.ui.notifyUserActivity();
    if (!g_app.isFullscreen) {
        glfwGetWindowPos(g_app.window, &g_app.savedWinX, &g_app.savedWinY);
        glfwGetWindowSize(g_app.window, &g_app.savedWinW, &g_app.savedWinH);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(g_app.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        g_app.isFullscreen = true;
    } else {
        glfwSetWindowMonitor(g_app.window, nullptr, g_app.savedWinX, g_app.savedWinY, g_app.savedWinW, g_app.savedWinH, 0);
        g_app.isFullscreen = false;
    }
}

// -----------------------------------------------------------------------------
// GLFW Callbacks & Touchpad Support
// -----------------------------------------------------------------------------

static void syncPixelScale() {
    if (g_app.windowW <= 0) return;
    float scale = (float)g_app.fbW / (float)g_app.windowW;
    g_app.ui.font.setPixelScale(scale);
    g_app.ui.iconAtlas.setPixelScale(scale);
}

static void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    g_app.redrawPending = true;
    g_app.fbW = w;
    g_app.fbH = h;
    glViewport(0, 0, w, h);
    g_app.ui.font.invalidateViewport();
    syncPixelScale();
}

static void windowSizeCallback(GLFWwindow* window, int w, int h) {
    g_app.windowW = w;
    g_app.windowH = h;
    fitToWindow(true);
    g_app.ui.cachedWindowW = (float)w;
    g_app.ui.cachedWindowH = (float)h;
}

static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    g_app.redrawPending = true;
    g_app.mouseX = xpos;
    g_app.mouseY = ypos;
    g_app.ui.notifyUserActivity();

    if (g_app.isDragging && !g_app.ui.isGridView && !g_app.ui.presentationMode) {
        float dx = (float)(xpos - g_app.dragStartX);
        float dy = (float)(ypos - g_app.dragStartY);
        g_app.targetPosX = g_app.dragStartPosX + dx;
        g_app.targetPosY = g_app.dragStartPosY + dy;
    } else {
        UIAction act = g_app.ui.handleMouseMove((float)xpos, (float)ypos);
        if (act.type == UIAction::PAN_TO_MINIMAP_NORM) {
            panToMinimapNorm(act.normX, act.normY);
        }
    }
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    g_app.redrawPending = true;
    g_app.ui.notifyUserActivity();
    if (action == GLFW_PRESS) {
        double now = glfwGetTime();

        // Presentation Mode Navigation via Mouse Click
        if (g_app.ui.presentationMode) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                if (g_app.folder.next()) loadCurrentFile();
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                if (g_app.folder.prev()) loadCurrentFile();
            }
            return;
        }

        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            UIAction act = g_app.ui.handleMouseDown((float)g_app.mouseX, (float)g_app.mouseY,
                                                    g_app.folder.count(), g_app.folder.currentIndex,
                                                    (float)g_app.currentImage.width, (float)g_app.currentImage.height);
            if (act.type == UIAction::PAN_TO_MINIMAP_NORM) {
                panToMinimapNorm(act.normX, act.normY);
                return;
            }
            if (act.type == UIAction::SET_ZOOM_SCALE) {
                if (g_app.ui.isGridView) g_app.ui.isGridView = false;
                g_app.targetScale = act.zoomScaleTarget;
                calculateFitParameters(g_app.fitScale, g_app.fitPosX, g_app.fitPosY);
                g_app.targetPosX = g_app.fitPosX;
                g_app.targetPosY = g_app.fitPosY;
                g_app.ui.showToast("Zoom: " + std::to_string((int)(act.zoomScaleTarget * 100.0f + 0.5f)) + "%");
                return;
            }
            if (act.type == UIAction::TOGGLE_THEME) {
                return;
            }
            if (act.type == UIAction::TOGGLE_FIT) {
                if (g_app.ui.isGridView) g_app.ui.isGridView = false;
                if (fabs(g_app.targetScale - g_app.fitScale) < 0.02f) set1to1Scale();
                else fitToWindow();
                return;
            }
            if (act.type == UIAction::SET_1TO1) {
                if (g_app.ui.isGridView) g_app.ui.isGridView = false;
                set1to1Scale();
                return;
            }
            if (act.type == UIAction::CENTER_VIEW) {
                if (g_app.ui.isGridView) g_app.ui.isGridView = false;
                centerView();
                return;
            }
            if (act.type == UIAction::ROTATE) {
                g_app.targetRotation += 90.0f;
                return;
            }
            if (act.type == UIAction::TOGGLE_GRID_VIEW) {
                g_app.ui.showToast(g_app.ui.isGridView ? "Grid View" : "Single Image View");
                return;
            }
            if (act.type == UIAction::SELECT_IMAGE_INDEX) {
                if (g_app.folder.jumpTo(act.index)) {
                    loadCurrentFile();
                }
                return;
            }
            if (act.type == UIAction::OPEN_IMAGE_INDEX) {
                if (g_app.folder.jumpTo(act.index)) {
                    loadCurrentFile();
                    g_app.ui.isGridView = false;
                }
                return;
            }
            if (act.type != UIAction::NONE) {
                return;
            }

            // Double click in Single View toggles fit / 1:1
            if (!g_app.ui.isGridView) {
                if (now - g_app.lastClickTime < 0.28) {
                    if (fabs(g_app.targetScale - g_app.fitScale) < 0.05f) set1to1Scale();
                    else fitToWindow();
                    g_app.lastClickTime = 0.0;
                    return;
                }
                g_app.lastClickTime = now;

                // Start Pan Drag
                g_app.isDragging = true;
                g_app.dragStartX = g_app.mouseX;
                g_app.dragStartY = g_app.mouseY;
                g_app.dragStartPosX = g_app.targetPosX;
                g_app.dragStartPosY = g_app.targetPosY;
            }
        } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (!g_app.ui.isGridView) {
                g_app.isDragging = true;
                g_app.dragStartX = g_app.mouseX;
                g_app.dragStartY = g_app.mouseY;
                g_app.dragStartPosX = g_app.targetPosX;
                g_app.dragStartPosY = g_app.targetPosY;
            }
        } else if (button == GLFW_MOUSE_BUTTON_4) {
            if (g_app.folder.prev()) loadCurrentFile();
        } else if (button == GLFW_MOUSE_BUTTON_5) {
            if (g_app.folder.next()) loadCurrentFile();
        }
    } else if (action == GLFW_RELEASE) {
        g_app.ui.handleMouseUp();
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
            g_app.isDragging = false;
        }
    }
}

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    g_app.redrawPending = true;
    g_app.ui.notifyUserActivity();

    // 1. Grid View Scrolling
    if (g_app.ui.isGridView) {
        g_app.ui.handleScroll(yoffset, g_app.folder.count());
        return;
    }

    // 2. Filmstrip Scrolling (when cursor is over filmstrip)
    float stripY = (float)g_app.windowH - g_app.ui.bottomStripH;
    if (!g_app.ui.presentationMode && g_app.ui.showThumbnails && g_app.mouseY >= stripY) {
        float step = 38.0f; // smooth pixel scrolling
        float scrollDelta = (float)xoffset != 0.0f ? -(float)xoffset : -(float)yoffset;
        g_app.ui.thumbs.targetScrollOffset += scrollDelta * step;
        float maxScroll = std::max(0.0f, g_app.folder.count() * (g_app.ui.thumbW + g_app.ui.thumbGap) - (float)g_app.windowW + 32.0f);
        g_app.ui.thumbs.targetScrollOffset = std::clamp(g_app.ui.thumbs.targetScrollOffset, 0.0f, maxScroll);
        g_app.ui.notifyScrollActivity();
        return;
    }

    // 3. Touchpad Pinch-to-Zoom / Two-Finger Vertical Touch Scroll
    // Smoothly zooms in/out centered directly at the mouse cursor
    if (yoffset != 0.0) {
        float factor = powf(1.08f, (float)yoffset);
        zoomAtCursor(factor);
    }

    // 4. Two-Finger Horizontal Slide
    // - If zoomed in: pans horizontally across image
    // - If at fit view: swipes next / previous image
    if (xoffset != 0.0) {
        bool isZoomed = (g_app.targetScale > g_app.fitScale * 1.05f);
        if (isZoomed) {
            g_app.targetPosX += (float)xoffset * 32.0f;
        } else {
            g_app.swipeAccumX += (float)xoffset;
            if (g_app.swipeAccumX > 3.0f) {
                g_app.swipeAccumX = 0.0f;
                if (g_app.folder.prev()) loadCurrentFile();
            } else if (g_app.swipeAccumX < -3.0f) {
                g_app.swipeAccumX = 0.0f;
                if (g_app.folder.next()) loadCurrentFile();
            }
        }
    }
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    g_app.redrawPending = true;
    // Physical-position keys: a compositor remap (caps:swapescape) is invisible
    // to GLFW, so honour the same config switch the gallery uses.
    if (key == GLFW_KEY_CAPS_LOCK &&
        SilverConfig::get().flag("keys.capsLockActsAsEscape", false)) {
        key = GLFW_KEY_ESCAPE;
    }

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    g_app.ui.notifyUserActivity();

    // Ctrl + 1 (100%), Ctrl + 2 (200%), Ctrl + 3 (300%), Ctrl + 0 (Fit)
    if (mods & GLFW_MOD_CONTROL) {
        if (key == GLFW_KEY_1) {
            g_app.targetScale = 1.0f;
            calculateFitParameters(g_app.fitScale, g_app.fitPosX, g_app.fitPosY);
            g_app.targetPosX = g_app.fitPosX;
            g_app.targetPosY = g_app.fitPosY;
            g_app.ui.showToast("Zoom: 100%");
            return;
        }
        if (key == GLFW_KEY_2) {
            g_app.targetScale = 2.0f;
            calculateFitParameters(g_app.fitScale, g_app.fitPosX, g_app.fitPosY);
            g_app.targetPosX = g_app.fitPosX;
            g_app.targetPosY = g_app.fitPosY;
            g_app.ui.showToast("Zoom: 200%");
            return;
        }
        if (key == GLFW_KEY_3) {
            g_app.targetScale = 3.0f;
            calculateFitParameters(g_app.fitScale, g_app.fitPosX, g_app.fitPosY);
            g_app.targetPosX = g_app.fitPosX;
            g_app.targetPosY = g_app.fitPosY;
            g_app.ui.showToast("Zoom: 300%");
            return;
        }
        if (key == GLFW_KEY_0) {
            fitToWindow();
            g_app.ui.showToast("Fit to Window");
            return;
        }
    }

    if (g_app.ui.isGridView) {
        int cols = std::max(1, (int)((g_app.windowW - 32) / 164));
        switch (key) {
            case GLFW_KEY_RIGHT:
            case GLFW_KEY_D:
                if (g_app.folder.next()) loadCurrentFile();
                break;
            case GLFW_KEY_LEFT:
            case GLFW_KEY_A:
                if (g_app.folder.prev()) loadCurrentFile();
                break;
            case GLFW_KEY_DOWN:
            case GLFW_KEY_S:
                if (g_app.folder.currentIndex + cols < g_app.folder.count()) {
                    g_app.folder.jumpTo(g_app.folder.currentIndex + cols);
                    loadCurrentFile();
                }
                break;
            case GLFW_KEY_UP:
            case GLFW_KEY_W:
                if (g_app.folder.currentIndex - cols >= 0) {
                    g_app.folder.jumpTo(g_app.folder.currentIndex - cols);
                    loadCurrentFile();
                }
                break;
            case GLFW_KEY_ENTER:
            case GLFW_KEY_SPACE:
                g_app.ui.isGridView = false;
                break;
            case GLFW_KEY_G:
                g_app.ui.isGridView = false;
                break;
            case GLFW_KEY_ESCAPE:
                g_app.ui.isGridView = false;
                break;
        }
        return;
    }

    switch (key) {
        case GLFW_KEY_RIGHT:
        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_D:
            if (g_app.folder.next()) loadCurrentFile();
            break;

        case GLFW_KEY_LEFT:
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_A:
            if (g_app.folder.prev()) loadCurrentFile();
            break;

        case GLFW_KEY_SPACE:
            if (mods & GLFW_MOD_SHIFT) {
                if (g_app.folder.prev()) loadCurrentFile();
            } else {
                if (g_app.folder.next()) loadCurrentFile();
            }
            break;

        case GLFW_KEY_HOME:
            if (g_app.folder.first()) loadCurrentFile();
            break;

        case GLFW_KEY_END:
            if (g_app.folder.last()) loadCurrentFile();
            break;

        case GLFW_KEY_EQUAL:
        case GLFW_KEY_KP_ADD:
            zoomAtCursor(1.25f);
            break;

        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            zoomAtCursor(0.80f);
            break;

        case GLFW_KEY_1:
            set1to1Scale();
            break;

        case GLFW_KEY_F:
            fitToWindow();
            g_app.ui.showToast("Fit to Window");
            break;

        case GLFW_KEY_P:
            g_app.ui.presentationMode = !g_app.ui.presentationMode;
            if (g_app.ui.presentationMode) {
                g_app.ui.showZoomMenu = false;
                g_app.ui.metadataState = 0;
                g_app.ui.isGridView = false;
                fitToWindow();
                g_app.ui.showToast("Presentation Mode (Press P or Esc to exit)", 2.0f);
            } else {
                fitToWindow();
                g_app.ui.showToast("Exited Presentation Mode", 1.5f);
            }
            break;

        case GLFW_KEY_C:
            centerView();
            break;

        case GLFW_KEY_G:
            g_app.ui.isGridView = !g_app.ui.isGridView;
            g_app.ui.showToast(g_app.ui.isGridView ? "Grid View" : "Single Image View");
            break;

        case GLFW_KEY_T:
            g_app.ui.theme.cycleThemeMode();
            g_app.ui.showToast(std::string("Theme: ") + g_app.ui.theme.getThemeModeName());
            break;

        case GLFW_KEY_R:
            if (mods & GLFW_MOD_SHIFT) {
                g_app.targetRotation -= 90.0f;
            } else {
                g_app.targetRotation += 90.0f;
            }
            break;

        case GLFW_KEY_I:
        case GLFW_KEY_F2:
            g_app.ui.metadataState = (g_app.ui.metadataState + 1) % 3;
            break;

        case GLFW_KEY_TAB:
            g_app.ui.showThumbnails = !g_app.ui.showThumbnails;
            fitToWindow();
            break;

        case GLFW_KEY_N:
            g_app.nearestFilter = !g_app.nearestFilter;
            g_app.currentImage.setFiltering(g_app.nearestFilter);
            g_app.ui.showToast(g_app.nearestFilter ? "Filter: Nearest-Neighbor" : "Filter: Bilinear");
            break;

        case GLFW_KEY_B:
            g_app.bgMode = (g_app.bgMode + 1) % 4;
            {
                const char* bgNames[] = { "FilePilot Checkerboard", "Standard Checkerboard", "Pure Black", "Pure White" };
                g_app.ui.showToast(std::string("Background: ") + bgNames[g_app.bgMode]);
            }
            break;

        case GLFW_KEY_F11:
            toggleFullscreen();
            break;

        case GLFW_KEY_ESCAPE:
            if (g_app.ui.showThemeMenu) {
                g_app.ui.showThemeMenu = false;
            } else if (g_app.ui.presentationMode) {
                g_app.ui.presentationMode = false;
                fitToWindow();
                g_app.ui.showToast("Exited Presentation Mode", 1.2f);
            } else if (g_app.ui.showZoomMenu) {
                g_app.ui.showZoomMenu = false;
            } else if (g_app.ui.metadataState > 0) {
                g_app.ui.metadataState = 0;
            } else if (g_app.ui.isGridView) {
                g_app.ui.isGridView = false;
            } else if (g_app.isFullscreen) {
                toggleFullscreen();
            } else {
                glfwSetWindowShouldClose(g_app.window, GLFW_TRUE);
            }
            break;

        default:
            break;
    }
}

static void dropCallback(GLFWwindow* window, int count, const char** paths) {
    if (count <= 0 || !paths || !paths[0]) return;
    g_app.ui.notifyUserActivity();

    if (g_app.folder.scan(paths[0])) {
        g_app.preloader.clear();
        loadCurrentFile();
    }
}

// -----------------------------------------------------------------------------
// Application Entry Point
// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::cout << "Starting SilverViewer FilePilot (Linux / Wayland / Multi-Threaded Engine)...\n";

    // Shared JSON config drives spacing, timings and animation switches.
    SilverConfig::get().init(argv[0]);
    silveranim::reloadFromConfig();
    silvercodec::gifMaxFrames() = std::max(1, SilverConfig::get().integer("gif.maxFrames", 300));
    silvercodec::gifMaxBytes()  = (size_t)std::max(8, SilverConfig::get().integer("gif.maxDecodeMegabytes", 256))
                                  * 1024ull * 1024ull;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    // Set Application ID & Class Context for Wayland & X11 (Prevents 'unknown' and ANR issues)
#ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "com.silvergallery.viewer");
#endif
#ifdef GLFW_X11_CLASS_NAME
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "com.silvergallery.viewer");
#endif
#ifdef GLFW_X11_INSTANCE_NAME
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "silver_viewer");
#endif

    g_app.window = glfwCreateWindow(g_app.windowW, g_app.windowH, "SilverViewer", nullptr, nullptr);
    if (!g_app.window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    // Set Window & Taskbar Icon
    {
        const int iconSize = 48;
        std::vector<unsigned char> iconPixels(iconSize * iconSize * 4, 0);
        for (int y = 0; y < iconSize; ++y) {
            for (int x = 0; x < iconSize; ++x) {
                float dx = (x - iconSize * 0.5f) / (iconSize * 0.5f);
                float dy = (y - iconSize * 0.5f) / (iconSize * 0.5f);
                float dist = std::sqrt(dx * dx + dy * dy);
                int idx = (y * iconSize + x) * 4;
                if (dist <= 0.90f) {
                    iconPixels[idx + 0] = (unsigned char)(2 + (x * 30 / iconSize));
                    iconPixels[idx + 1] = (unsigned char)(132 + (y * 50 / iconSize));
                    iconPixels[idx + 2] = (unsigned char)(199 + (x * 40 / iconSize));
                    iconPixels[idx + 3] = 255;
                }
            }
        }
        GLFWimage iconImg;
        iconImg.width = iconSize;
        iconImg.height = iconSize;
        iconImg.pixels = iconPixels.data();
        glfwSetWindowIcon(g_app.window, 1, &iconImg);
    }

    glfwMakeContextCurrent(g_app.window);
    glfwSwapInterval(1); // Enable V-Sync (butter-smooth 60/120/144 FPS)

    glfwGetFramebufferSize(g_app.window, &g_app.fbW, &g_app.fbH);
    glfwGetWindowSize(g_app.window, &g_app.windowW, &g_app.windowH);
    syncPixelScale();

    // Register Callbacks
    glfwSetFramebufferSizeCallback(g_app.window, framebufferSizeCallback);
    glfwSetWindowSizeCallback(g_app.window, windowSizeCallback);
    glfwSetCursorPosCallback(g_app.window, cursorPosCallback);
    glfwSetMouseButtonCallback(g_app.window, mouseButtonCallback);
    glfwSetScrollCallback(g_app.window, scrollCallback);
    glfwSetKeyCallback(g_app.window, keyCallback);
    glfwSetDropCallback(g_app.window, dropCallback);

    // Initialize Render Subsystems
    if (!g_app.imageShader.init()) {
        std::cerr << "Failed to initialize Image Shader\n";
        return -1;
    }

    g_app.ui.thumbs.onWorkReady = []() { glfwPostEmptyEvent(); };
    if (!g_app.ui.init()) {
        std::cerr << "Failed to initialize FilePilot UI\n";
        return -1;
    }

    g_app.preloader.init();

    // Load initial target file / directory
    std::string startPath = ".";
    if (argc > 1) {
        startPath = argv[1];
    }
    if (g_app.folder.scan(startPath)) {
        loadCurrentFile();
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(g_app.window)) {
        double curTime = glfwGetTime();
        float dt = (float)(curTime - lastTime);
        if (dt > 0.1f) dt = 0.1f;
        lastTime = curTime;

        // Process Wayland desktop events
        // Sleep between events unless something is actually changing: an image
        // on screen with no animation needs no new frames.
        bool busy = g_app.redrawPending ||
                    g_app.currentImage.isAnimated() ||          // GIF playback
                    g_app.preloader.isCurrentLoading.load() ||
                    g_app.ui.thumbs.hasPendingWork() ||
                    g_app.ui.animating ||
                    std::abs(g_app.scale - g_app.targetScale) > 0.0005f ||
                    std::abs(g_app.posX - g_app.targetPosX) > 0.05f ||
                    std::abs(g_app.posY - g_app.targetPosY) > 0.05f ||
                    std::abs(g_app.rotation - g_app.targetRotation) > 0.05f;

        if (busy) glfwPollEvents();
        else      glfwWaitEventsTimeout(0.2);
        g_app.redrawPending = false;

        // Check if an asynchronously decoded large image became ready in background
        if (g_app.preloader.isCurrentLoading.load() && g_app.folder.hasFiles()) {
            std::string currentPath = g_app.folder.currentPath();
            auto preloaded = g_app.preloader.getIfReady(currentPath);
            if (preloaded && preloaded->data) {
                if (g_app.currentImage.uploadPixels(preloaded->data, preloaded->width, preloaded->height, preloaded->meta)) {
                    g_app.currentImage.setFiltering(g_app.nearestFilter);
                    applyLoadedImageMeta(preloaded->meta);
                }
            }
        }

        // Presentation Mode & Fullscreen Auto-Hide Cursor
        bool shouldHideCursor = g_app.ui.presentationMode || (g_app.isFullscreen && g_app.ui.uiVisibilityAlpha < 0.05f);
        if (shouldHideCursor) {
            if (!g_app.cursorIsHidden) {
                glfwSetInputMode(g_app.window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                g_app.cursorIsHidden = true;
            }
        } else {
            if (g_app.cursorIsHidden) {
                glfwSetInputMode(g_app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                g_app.cursorIsHidden = false;
            }
        }

        // Animated GIF playback (no-op for stills).
        g_app.currentImage.advanceAnimation(dt);

        // Pan / zoom / rotation share one second-order channel.
        const silveranim::Channel& tf = silveranim::rates().chViewerTransform;
        silveranim::drivePos(g_app.posX, g_app.posVelX, g_app.targetPosX, tf, dt);
        silveranim::drivePos(g_app.posY, g_app.posVelY, g_app.targetPosY, tf, dt);
        silveranim::drive(g_app.scale, g_app.scaleVel, g_app.targetScale, tf, dt, 0.0005f);
        silveranim::drivePos(g_app.rotation, g_app.rotationVel, g_app.targetRotation, tf, dt);

        bool isZoomedIn = (g_app.targetScale > g_app.fitScale * 1.05f);
        g_app.ui.update(dt, isZoomedIn, g_app.folder.count(), g_app.isFullscreen);

        // 1. Draw Background Canvas
        int activeBgMode = g_app.bgMode;
        if (g_app.ui.presentationMode) {
            // Presentation mode uses direct solid background (black in dark theme, white in light theme)
            activeBgMode = g_app.ui.theme.isDarkMode ? 2 : 3;
        }
        g_app.imageShader.drawBackground(g_app.windowW, g_app.windowH, activeBgMode, g_app.ui.theme.isDarkMode);

        // 2. Render Single Image (Full Res or Progressive Preview)
        if (!g_app.ui.isGridView) {
            GLuint renderTexId = g_app.currentImage.id;

            // If full resolution is still decoding in background, fall back to thumbnail texture
            if (!renderTexId && g_app.folder.hasFiles()) {
                auto it = g_app.ui.thumbs.cache.find(g_app.folder.currentPath());
                if (it != g_app.ui.thumbs.cache.end() && it->second.ready) {
                    renderTexId = it->second.texId;
                }
            }

            if (renderTexId) {
                int curRotInt = ((int)(g_app.rotation + 0.5f) % 360 + 360) % 360;
                float drawW = (float)(g_app.currentImage.width > 0 ? g_app.currentImage.width : 800);
                float drawH = (float)(g_app.currentImage.height > 0 ? g_app.currentImage.height : 600);

                g_app.imageShader.draw(
                    g_app.windowW, g_app.windowH,
                    g_app.posX, g_app.posY,
                    drawW, drawH,
                    g_app.scale, curRotInt,
                    activeBgMode, g_app.pixelGrid,
                    renderTexId
                );
            }
        }

        // 3. Render UI Overlays, Zoom Dropdown Menu, Minimap, and Badges
        // One coalesced batch: every font.render() below only queues geometry.
        g_app.ui.font.beginFrame(g_app.windowW, g_app.windowH);
        int curRotInt = ((int)(g_app.rotation + 0.5f) % 360 + 360) % 360;
        g_app.ui.render(
            g_app.windowW, g_app.windowH,
            (float)g_app.mouseX, (float)g_app.mouseY,
            g_app.currentImage.meta,
            g_app.folder.fileList,
            g_app.folder.currentIndex,
            g_app.scale, g_app.fitScale,
            g_app.posX, g_app.posY,
            curRotInt,
            g_app.nearestFilter, g_app.pixelGrid,
            g_app.preloader.isCurrentLoading.load(),
            g_app.currentImage.id
        );
        g_app.ui.font.endFrame();

        glfwSwapBuffers(g_app.window);
    }

    glfwDestroyWindow(g_app.window);
    glfwTerminate();
    return 0;
}
