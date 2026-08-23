#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "../viewer_linux/gl_loader.h"
#include "../viewer_linux/shaders.h"
#include "../viewer_linux/font.h"
#include "../viewer_linux/icons.h"
#include "db.h"
#include "scanner.h"
#include "timeline.h"
#include "gallery_ui.h"

struct GalleryApp {
    GLFWwindow* window = nullptr;
    int windowW = 1200;
    int windowH = 800;
    int fbW = 1200;
    int fbH = 800;

    double mouseX = 0;
    double mouseY = 0;
    bool isMouseDown = false;

    GalleryDatabase db;
    GalleryScanner scanner;
    TimelineManager timeline;
    GalleryUI ui;

    FontRenderer font;
    IconAtlas iconAtlas;
    ImageShader bgShader;

    std::vector<GalleryRecord> currentRecords;
    bool needsTimelineRebuild = false;
};

static GalleryApp g_gal;

static void refreshRecords() {
    bool onlyStarred = (g_gal.ui.currentTab == TAB_FAVORITES);
    std::string fFilter = g_gal.ui.activeFolderFilter;

    g_gal.currentRecords = g_gal.db.fetchAllSorted(g_gal.ui.searchQuery, onlyStarred, fFilter);
    float gridW = (g_gal.ui.showSidebar && g_gal.windowW >= 750) ? ((float)g_gal.windowW - g_gal.ui.sidebarWidth) : (float)g_gal.windowW;
    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
    g_gal.timeline.buildTimeline(g_gal.currentRecords, gridW, hasBanner);
}

static void launchViewer(const std::string& path) {
    if (path.empty()) return;

    pid_t pid = fork();
    if (pid == 0) {
        std::string viewerBin = "./bin/silver_viewer";
        if (access(viewerBin.c_str(), X_OK) != 0) {
            viewerBin = "../bin/silver_viewer";
        }
        if (access(viewerBin.c_str(), X_OK) != 0) {
            viewerBin = "./bin/silver_viewer_linux";
        }
        if (access(viewerBin.c_str(), X_OK) != 0) {
            viewerBin = "../bin/silver_viewer_linux";
        }
        execlp(viewerBin.c_str(), viewerBin.c_str(), path.c_str(), nullptr);
        _exit(1);
    }
}

static void copyPathToClipboard(const std::string& path) {
    if (path.empty()) return;
    std::thread([path]() {
        std::string cmd = "printf '%s' \"" + path + "\" | wl-copy 2>/dev/null || printf '%s' \"" + path + "\" | xclip -selection clipboard 2>/dev/null";
        int res = system(cmd.c_str());
        (void)res;
    }).detach();
    std::cout << "Copied to clipboard: " << path << std::endl;
}

// -----------------------------------------------------------------------------
// GLFW Callbacks
// -----------------------------------------------------------------------------

static void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    g_gal.fbW = w;
    g_gal.fbH = h;
    glViewport(0, 0, w, h);
}

static void windowSizeCallback(GLFWwindow* window, int w, int h) {
    g_gal.windowW = w;
    g_gal.windowH = h;
    float gridW = (g_gal.ui.showSidebar && w >= 750) ? ((float)w - g_gal.ui.sidebarWidth) : (float)w;
    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
    g_gal.timeline.buildTimeline(g_gal.currentRecords, gridW, hasBanner);
}

static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    g_gal.mouseX = xpos;
    g_gal.mouseY = ypos;

    if (g_gal.isMouseDown) {
        g_gal.ui.handleMouseDrag((float)xpos, (float)ypos, g_gal.timeline, (float)g_gal.windowW, (float)g_gal.windowH, g_gal.currentRecords);
    }
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_gal.isMouseDown = true;
            GalleryUIAction act = g_gal.ui.handleMouseDown((float)g_gal.mouseX, (float)g_gal.mouseY,
                                                          g_gal.timeline, g_gal.currentRecords,
                                                          (float)g_gal.windowW, (float)g_gal.windowH);
            switch (act.type) {
                case GalleryUIAction::SELECT_IMAGE: {
                    float gridW = (g_gal.ui.showSidebar && g_gal.windowW >= 750) ? ((float)g_gal.windowW - g_gal.ui.sidebarWidth) : (float)g_gal.windowW;
                    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                    g_gal.timeline.buildTimeline(g_gal.currentRecords, gridW, hasBanner);
                    break;
                }
                case GalleryUIAction::CLOSE_SIDEBAR: {
                    float gridW = (float)g_gal.windowW;
                    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                    g_gal.timeline.buildTimeline(g_gal.currentRecords, gridW, hasBanner);
                    break;
                }
                case GalleryUIAction::OPEN_IN_VIEWER:
                    launchViewer(act.targetPath);
                    break;
                case GalleryUIAction::TOGGLE_STAR:
                    g_gal.db.toggleStarred(act.targetPath);
                    if (g_gal.ui.currentTab == TAB_FAVORITES) {
                        refreshRecords();
                    }
                    break;
                case GalleryUIAction::START_SCAN:
                    g_gal.scanner.startScan(g_gal.db, true);
                    break;
                case GalleryUIAction::SWITCH_TAB:
                case GalleryUIAction::SELECT_FOLDER:
                case GalleryUIAction::CLEAR_FOLDER_FILTER:
                    refreshRecords();
                    break;
                case GalleryUIAction::COPY_PATH:
                    copyPathToClipboard(act.targetPath);
                    break;
                default:
                    break;
            }
        } else if (action == GLFW_RELEASE) {
            g_gal.isMouseDown = false;
            g_gal.ui.handleMouseUp();
        }
    }
}

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    bool isCtrl = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

    if (g_gal.ui.isFullScreenView) {
        // Zoom in/out in fullscreen
        float zoomDelta = (float)yoffset * 0.15f;
        g_gal.ui.fsZoom = std::clamp(g_gal.ui.fsZoom + zoomDelta, 0.2f, 10.0f);
        return;
    }

    if (isCtrl) {
        // Continuous smooth zoom on Touchpad Pinch / Ctrl + Scroll wheel!
        float zoomDelta = (float)yoffset * 0.04f;
        float gridW = (g_gal.ui.showSidebar && g_gal.windowW >= 750) ? ((float)g_gal.windowW - g_gal.ui.sidebarWidth) : (float)g_gal.windowW;
        bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
        g_gal.timeline.applyZoomDelta(zoomDelta, gridW, g_gal.currentRecords, hasBanner);
        g_gal.ui.showZoomPopup = true;
        g_gal.ui.zoomPopupAutoCloseTimer = 2.5f;
        return;
    }

    g_gal.ui.handleScroll((float)yoffset, g_gal.timeline.totalContentHeight, (float)g_gal.windowH);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_BACKSPACE) {
            if (g_gal.ui.showThemeMenu) {
                g_gal.ui.showThemeMenu = false;
            } else if (g_gal.ui.showZoomPopup) {
                g_gal.ui.showZoomPopup = false;
            } else if (g_gal.ui.isFullScreenView) {
                g_gal.ui.isFullScreenView = false;
            } else if (g_gal.ui.showSidebar) {
                g_gal.ui.showSidebar = false;
                g_gal.timeline.clearSelection();
                float gridW = (float)g_gal.windowW;
                bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                g_gal.timeline.buildTimeline(g_gal.currentRecords, gridW, hasBanner);
            } else if (!g_gal.ui.activeFolderFilter.empty()) {
                g_gal.ui.activeFolderFilter.clear();
                g_gal.ui.currentTab = TAB_FOLDERS;
                refreshRecords();
            } else if (g_gal.ui.currentTab != TAB_ALL) {
                g_gal.ui.currentTab = TAB_ALL;
                refreshRecords();
            } else {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        } else if (key == GLFW_KEY_LEFT) {
            if (g_gal.ui.isFullScreenView) {
                if (g_gal.ui.fullScreenIndex > 0) {
                    g_gal.ui.openFullScreen(g_gal.ui.fullScreenIndex - 1, g_gal.currentRecords, g_gal.timeline);
                }
            } else if (!g_gal.timeline.flatAllItems.empty()) {
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx <= 0) ? 0 : (curIdx - 1);
                if (nextIdx < (int)g_gal.timeline.flatAllItems.size()) {
                    g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[nextIdx]->record, g_gal.timeline, (float)g_gal.windowH);
                }
            }
        } else if (key == GLFW_KEY_RIGHT) {
            if (g_gal.ui.isFullScreenView) {
                if (g_gal.ui.fullScreenIndex + 1 < (int)g_gal.currentRecords.size()) {
                    g_gal.ui.openFullScreen(g_gal.ui.fullScreenIndex + 1, g_gal.currentRecords, g_gal.timeline);
                }
            } else if (!g_gal.timeline.flatAllItems.empty()) {
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::min((int)g_gal.timeline.flatAllItems.size() - 1, curIdx + 1);
                if (nextIdx < (int)g_gal.timeline.flatAllItems.size()) {
                    g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[nextIdx]->record, g_gal.timeline, (float)g_gal.windowH);
                }
            }
        } else if (key == GLFW_KEY_UP) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::max(0, curIdx - cols);
                if (nextIdx < (int)g_gal.timeline.flatAllItems.size()) {
                    g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[nextIdx]->record, g_gal.timeline, (float)g_gal.windowH);
                }
            }
        } else if (key == GLFW_KEY_DOWN) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::min((int)g_gal.timeline.flatAllItems.size() - 1, curIdx + cols);
                if (nextIdx < (int)g_gal.timeline.flatAllItems.size()) {
                    g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[nextIdx]->record, g_gal.timeline, (float)g_gal.windowH);
                }
            }
        } else if (key == GLFW_KEY_PAGE_UP) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::max(0, curIdx - cols * 3);
                if (nextIdx < (int)g_gal.timeline.flatAllItems.size()) {
                    g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[nextIdx]->record, g_gal.timeline, (float)g_gal.windowH);
                }
            }
        } else if (key == GLFW_KEY_PAGE_DOWN) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::min((int)g_gal.timeline.flatAllItems.size() - 1, curIdx + cols * 3);
                if (nextIdx < (int)g_gal.timeline.flatAllItems.size()) {
                    g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[nextIdx]->record, g_gal.timeline, (float)g_gal.windowH);
                }
            }
        } else if (key == GLFW_KEY_HOME) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[0]->record, g_gal.timeline, (float)g_gal.windowH);
            }
        } else if (key == GLFW_KEY_END) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int lastIdx = (int)g_gal.timeline.flatAllItems.size() - 1;
                g_gal.ui.selectPhoto(g_gal.timeline.flatAllItems[lastIdx]->record, g_gal.timeline, (float)g_gal.windowH);
            }
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_F) {
            if (!g_gal.ui.selectedPath.empty()) {
                g_gal.ui.openFullScreen(g_gal.timeline.selectedFlatIndex, g_gal.currentRecords, g_gal.timeline);
            }
        } else if (key == GLFW_KEY_SPACE) {
            if (!g_gal.ui.selectedPath.empty()) {
                g_gal.db.toggleStarred(g_gal.ui.selectedPath);
                g_gal.ui.selectedRecord.starred = 1 - g_gal.ui.selectedRecord.starred;
                if (g_gal.ui.currentTab == TAB_FAVORITES) {
                    refreshRecords();
                }
            }
        } else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
            float gridW = (g_gal.ui.showSidebar && g_gal.windowW >= 750) ? ((float)g_gal.windowW - g_gal.ui.sidebarWidth) : (float)g_gal.windowW;
            bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
            g_gal.timeline.zoomGrid(1, gridW, g_gal.currentRecords, hasBanner);
            g_gal.ui.showZoomPopup = true;
            g_gal.ui.zoomPopupAutoCloseTimer = 2.5f;
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            float gridW = (g_gal.ui.showSidebar && g_gal.windowW >= 750) ? ((float)g_gal.windowW - g_gal.ui.sidebarWidth) : (float)g_gal.windowW;
            bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
            g_gal.timeline.zoomGrid(-1, gridW, g_gal.currentRecords, hasBanner);
            g_gal.ui.showZoomPopup = true;
            g_gal.ui.zoomPopupAutoCloseTimer = 2.5f;
        } else if ((key == GLFW_KEY_0 || key == GLFW_KEY_KP_0) && (mods & GLFW_MOD_CONTROL)) {
            float gridW = (g_gal.ui.showSidebar && g_gal.windowW >= 750) ? ((float)g_gal.windowW - g_gal.ui.sidebarWidth) : (float)g_gal.windowW;
            bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
            g_gal.timeline.resetGridZoom(gridW, g_gal.currentRecords, hasBanner);
            g_gal.ui.showZoomPopup = true;
            g_gal.ui.zoomPopupAutoCloseTimer = 2.5f;
        } else if (key == GLFW_KEY_F5 || (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))) {
            g_gal.scanner.startScan(g_gal.db, true);
        } else if (key == GLFW_KEY_T) {
            g_gal.ui.theme.cycleThemeMode();
            g_gal.ui.themeToastText = "Theme: " + g_gal.ui.theme.getThemeModeName();
            g_gal.ui.themeToastTimer = 2.0f;
        }
    }
}

// -----------------------------------------------------------------------------
// Main Application
// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::cout << "Starting SilverGallery (Timeline Image Organizer & Fast SQLite Engine)..." << std::endl;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Set Application ID & Class Context for Wayland & X11 (Prevents 'unknown' and ANR issues)
#ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "com.silvergallery.app");
#endif
#ifdef GLFW_X11_CLASS_NAME
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "com.silvergallery.app");
#endif
#ifdef GLFW_X11_INSTANCE_NAME
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "silver_gallery");
#endif

    g_gal.window = glfwCreateWindow(g_gal.windowW, g_gal.windowH, "SilverGallery - Photos", nullptr, nullptr);
    if (!g_gal.window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return 1;
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
                    iconPixels[idx + 0] = (unsigned char)(16 + (x * 40 / iconSize));
                    iconPixels[idx + 1] = (unsigned char)(185 + (y * 30 / iconSize));
                    iconPixels[idx + 2] = (unsigned char)(129 + (x * 20 / iconSize));
                    iconPixels[idx + 3] = 255;
                }
            }
        }
        GLFWimage iconImg;
        iconImg.width = iconSize;
        iconImg.height = iconSize;
        iconImg.pixels = iconPixels.data();
        glfwSetWindowIcon(g_gal.window, 1, &iconImg);
    }

    glfwMakeContextCurrent(g_gal.window);
    glfwSwapInterval(1); // VSync 60fps

    if (!init_gl_loader()) {
        std::cerr << "Failed to load OpenGL extensions!" << std::endl;
        glfwDestroyWindow(g_gal.window);
        glfwTerminate();
        return 1;
    }

    glfwGetFramebufferSize(g_gal.window, &g_gal.fbW, &g_gal.fbH);
    glViewport(0, 0, g_gal.fbW, g_gal.fbH);

    glfwSetFramebufferSizeCallback(g_gal.window, framebufferSizeCallback);
    glfwSetWindowSizeCallback(g_gal.window, windowSizeCallback);
    glfwSetCursorPosCallback(g_gal.window, cursorPosCallback);
    glfwSetMouseButtonCallback(g_gal.window, mouseButtonCallback);
    glfwSetScrollCallback(g_gal.window, scrollCallback);
    glfwSetKeyCallback(g_gal.window, keyCallback);

    // Initialize Subsystems
    g_gal.db.init();
    g_gal.font.init();
    g_gal.iconAtlas.init();
    g_gal.bgShader.init();

    // If custom directory passed on command line, add to search roots
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            g_gal.scanner.addCustomRoot(argv[i]);
        }
    }

    // Callback when background scan finishes
    g_gal.scanner.onScanComplete = []() {
        g_gal.needsTimelineRebuild = true;
    };

    // Load initial cached records immediately from SQLite (< 5ms)
    refreshRecords();

    // Start background folder scan for incremental updates
    g_gal.scanner.startScan(g_gal.db, true);

    auto lastTime = std::chrono::high_resolution_clock::now();

    // Main Render Loop
    while (!glfwWindowShouldClose(g_gal.window)) {
        glfwPollEvents();

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::min(dt, 0.1f);

        if (g_gal.needsTimelineRebuild) {
            g_gal.needsTimelineRebuild = false;
            refreshRecords();
        }

        g_gal.ui.update(dt, g_gal.timeline.totalContentHeight, (float)g_gal.windowH, g_gal.scanner.isScanning.load());
        if (g_gal.timeline.zoomPillTimer > 0.0f) {
            g_gal.timeline.zoomPillTimer = std::max(0.0f, g_gal.timeline.zoomPillTimer - dt);
        }
        g_gal.timeline.updateVisibility(g_gal.ui.scrollY, (float)g_gal.windowH, (float)g_gal.mouseX, (float)g_gal.mouseY, dt);

        // 1. Draw Background Canvas
        g_gal.bgShader.drawBackground(g_gal.windowW, g_gal.windowH, 0, g_gal.ui.theme.isDarkMode.load());

        // 2. Render Gallery UI & Photo Grid
        g_gal.ui.render(g_gal.windowW, g_gal.windowH, (float)g_gal.mouseX, (float)g_gal.mouseY,
                        g_gal.timeline, g_gal.db, g_gal.scanner, g_gal.font, g_gal.iconAtlas,
                        g_gal.currentRecords);

        glfwSwapBuffers(g_gal.window);
    }

    g_gal.scanner.stop();
    g_gal.db.close();
    glfwDestroyWindow(g_gal.window);
    glfwTerminate();
    return 0;
}

