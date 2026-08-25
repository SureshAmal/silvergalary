#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <stb_image.h>

#include "../viewer_linux/gl_loader.h"
#include "../viewer_linux/shaders.h"
#include "../viewer_linux/font.h"
#include "../viewer_linux/icons.h"
#include "silver_config.h"
#include "silver_anim.h"
#include "silver_constants.h"
#include "db.h"
#include "scanner.h"
#include "timeline.h"
#include "gallery_ui.h"

struct GalleryApp {
    GLFWwindow* window = nullptr;
    int windowW = silver::defaults::galleryWindowWidth;
    int windowH = silver::defaults::galleryWindowHeight;
    int fbW = silver::defaults::galleryWindowWidth;
    int fbH = silver::defaults::galleryWindowHeight;

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
    bool redrawPending = true;      // drives poll-vs-wait in the main loop
    bool showRenderStats = false;   // SILVER_RENDER_STATS=1
    bool showThumbStats = false;    // SILVER_THUMB_STATS=1
};

static GalleryApp g_gal;

static void refreshRecords() {
    bool onlyStarred = (g_gal.ui.currentTab == TAB_FAVORITES);
    std::string fFilter = g_gal.ui.activeFolderFilter;

    g_gal.currentRecords = g_gal.db.fetchAllSorted(g_gal.ui.searchQuery, onlyStarred, fFilter,
                                                  g_gal.ui.folderFilterRecursive);
    g_gal.ui.invalidateDbCache();
    float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
    g_gal.timeline.buildTimeline(g_gal.currentRecords, gridW, hasBanner);
}

static std::vector<std::string> collectAllPaths() {
    std::vector<std::string> all;
    all.reserve(g_gal.currentRecords.size());
    for (const auto& rec : g_gal.currentRecords) all.push_back(rec.path);
    return all;
}

static void launchViewer(const std::string& path) {
    if (path.empty()) return;

#ifdef _WIN32
    std::string cmd = "silver_viewer.exe \"" + path + "\"";
    WinExec(cmd.c_str(), SW_SHOWNORMAL);
#else
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
#endif
}

// Select by flat index from the keyboard, reflowing the grid the same way a
// click does so the highlighted photo is always on screen.
static void selectFlatIndex(int index) {
    if (index < 0 || index >= (int)g_gal.timeline.flatAllItems.size()) return;

    const GalleryRecord rec = g_gal.timeline.flatAllItems[index]->record;
    const bool sidebarWasOpen = g_gal.ui.showSidebar && !g_gal.ui.selectedPath.empty();
    g_gal.ui.selectPhoto(rec, g_gal.timeline);

    // Opening the inspector changes the available grid width once. Moving the
    // keyboard selection while it is already open does not, so rebuilding the
    // full timeline on every arrow press only creates competing motion.
    if (!sidebarWasOpen) {
        float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
        bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
        g_gal.ui.relayoutKeepingAnchor(g_gal.timeline, gridW, hasBanner,
                                       (float)g_gal.windowH, rec.path);
    }

    if (const TimelineItem* itm = g_gal.timeline.findItem(rec.path)) {
        g_gal.ui.ensureItemVisible(itm->y, itm->h, g_gal.timeline.totalContentHeight,
                                   (float)g_gal.windowH);
    }
}

// Return from the lightbox to the exact photo the user reached there. Keeping
// this in one path makes Escape, Back, and the close button restore identical
// gallery context.
static void closeFullscreenAndRevealSelection() {
    GalleryUI& ui = g_gal.ui;
    ui.isFullScreenView = false;
    ui.showThemeMenu = false;
    ui.showZoomPopup = false;
    ui.fsZoom = 1.0f;
    ui.fsPanX = 0.0f;
    ui.fsPanY = 0.0f;
    ui.isFsDragging = false;

    g_gal.timeline.selectItem(ui.selectedPath);
    if (const TimelineItem* item = g_gal.timeline.findItem(ui.selectedPath)) {
        ui.ensureItemVisible(item->y, item->h, g_gal.timeline.totalContentHeight,
                             (float)g_gal.windowH);
    }
}

static void switchTab(GalleryTab tab) {
    if (g_gal.ui.currentTab == tab) return;
    g_gal.ui.currentTab = tab;
    g_gal.ui.showSidebar = false;
    g_gal.timeline.clearSelection();
    // Reset the scroll spring outright; animating a jump between tabs reads as
    // the grid sliding on its own.
    g_gal.ui.targetScrollY = 0.0f;
    g_gal.ui.scrollY = 0.0f;
    g_gal.ui.scrollVel = 0.0f;
    g_gal.ui.scrollPrevTarget = 0.0f;
    refreshRecords();
}

static void copyPathToClipboard(const std::string& path) {
    if (path.empty()) return;
    if (g_gal.window) {
        glfwSetClipboardString(g_gal.window, path.c_str());
    }
    std::cout << "Copied to clipboard: " << path << std::endl;
}

// -----------------------------------------------------------------------------
// GLFW Callbacks
// -----------------------------------------------------------------------------

// Keep the glyph and icon atlases at framebuffer resolution. On a scaled
// display (Hyprland monitor scale, HiDPI) the framebuffer is larger than the
// window, and rasterizing at window size then letting the GPU upscale is what
// makes text and icons look pixelated.
static void syncPixelScale() {
    if (g_gal.windowW <= 0 || g_gal.windowH <= 0) return;
    float scaleX = (float)g_gal.fbW / (float)g_gal.windowW;
    float scaleY = (float)g_gal.fbH / (float)g_gal.windowH;
    // Rasterize for the denser axis. GLFW normally reports equal axes, but
    // max() remains sharp under unusual compositor transforms as well.
    float scale = std::max(scaleX, scaleY);
    g_gal.font.setPixelScale(scale);
    g_gal.iconAtlas.setPixelScale(scale);
    g_gal.ui.thumbs.setPixelScale(scale);
}

static void contentScaleCallback(GLFWwindow* window, float, float) {
    glfwGetWindowSize(window, &g_gal.windowW, &g_gal.windowH);
    glfwGetFramebufferSize(window, &g_gal.fbW, &g_gal.fbH);
    glViewport(0, 0, g_gal.fbW, g_gal.fbH);
    g_gal.font.invalidateViewport();
    syncPixelScale();
    g_gal.redrawPending = true;
}

static void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    g_gal.fbW = w;
    g_gal.fbH = h;
    glViewport(0, 0, w, h);
    g_gal.font.invalidateViewport();
    syncPixelScale();
    g_gal.redrawPending = true;
}

static void windowSizeCallback(GLFWwindow* window, int w, int h) {
    g_gal.windowW = w;
    g_gal.windowH = h;
    syncPixelScale();
    g_gal.redrawPending = true;
    float gridW = g_gal.ui.gridWidth((float)w);
    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
    // Resizing changes the column count; keep the selection (or nothing) anchored
    // instead of letting the viewport jump somewhere unrelated.
    g_gal.ui.relayoutKeepingAnchor(g_gal.timeline, gridW, hasBanner, (float)h, g_gal.ui.selectedPath);
}

static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    g_gal.mouseX = xpos;
    g_gal.mouseY = ypos;
    g_gal.redrawPending = true;   // hover states follow the cursor

    if (g_gal.isMouseDown) {
        g_gal.ui.handleMouseDrag((float)xpos, (float)ypos, g_gal.timeline, (float)g_gal.windowW, (float)g_gal.windowH, g_gal.currentRecords);
    }
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    g_gal.redrawPending = true;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_gal.isMouseDown = true;
            GalleryUIAction act = g_gal.ui.handleMouseDown((float)g_gal.mouseX, (float)g_gal.mouseY,
                                                          g_gal.timeline, g_gal.currentRecords,
                                                          (float)g_gal.windowW, (float)g_gal.windowH);
            switch (act.type) {
                case GalleryUIAction::SELECT_IMAGE: {
                    // Pin the photo that was just clicked so narrowing the grid
                    // for the sidebar cannot push it out of view.
                    float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
                    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                    g_gal.ui.relayoutKeepingAnchor(g_gal.timeline, gridW, hasBanner,
                                                   (float)g_gal.windowH, act.targetPath);
                    break;
                }
                case GalleryUIAction::CLOSE_SIDEBAR: {
                    float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
                    bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                    g_gal.ui.relayoutKeepingAnchor(g_gal.timeline, gridW, hasBanner,
                                                   (float)g_gal.windowH, act.targetPath);
                    break;
                }
                case GalleryUIAction::CLOSE_FULLSCREEN:
                    closeFullscreenAndRevealSelection();
                    break;
                case GalleryUIAction::OPEN_IN_VIEWER:
                    launchViewer(act.targetPath);
                    break;
                case GalleryUIAction::TOGGLE_STAR:
                    if (g_gal.db.toggleStarred(act.targetPath)) {
                        // The database, record vector, grid item and inspector
                        // are separate copies. Keep all of them authoritative
                        // immediately so reselecting a photo cannot undo the
                        // visual favorite state.
                        for (auto& rec : g_gal.currentRecords) {
                            if (rec.path == act.targetPath) rec.starred = act.starred;
                        }
                        for (auto* item : g_gal.timeline.flatAllItems) {
                            if (item && item->record.path == act.targetPath) {
                                item->record.starred = act.starred;
                            }
                        }
                        if (g_gal.ui.selectedPath == act.targetPath) {
                            g_gal.ui.selectedRecord.starred = act.starred;
                        }
                    } else {
                        // Roll back the optimistic UI state if persistence
                        // failed instead of showing a favorite that was lost.
                        int previous = act.starred >= 0 ? 1 - act.starred : 0;
                        for (auto& rec : g_gal.currentRecords) {
                            if (rec.path == act.targetPath) rec.starred = previous;
                        }
                        for (auto* item : g_gal.timeline.flatAllItems) {
                            if (item && item->record.path == act.targetPath) {
                                item->record.starred = previous;
                            }
                        }
                        if (g_gal.ui.selectedPath == act.targetPath) {
                            g_gal.ui.selectedRecord.starred = previous;
                        }
                    }
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
    g_gal.redrawPending = true;
    if (g_gal.ui.showSettings) {
        float maxScroll = g_gal.ui.settingsMaxScroll((float)g_gal.windowH);
        g_gal.ui.settingsScroll = std::clamp(g_gal.ui.settingsScroll - (float)yoffset * 48.0f,
                                             0.0f, maxScroll);
        return;
    }

    bool isCtrl = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

    if (g_gal.ui.isFullScreenView) {
        // Zoom in/out in fullscreen
        const SilverConfig& cfg = SilverConfig::get();
        float zoomDelta = (float)yoffset * cfg.num("fullscreen.zoomStep", 0.15f);
        g_gal.ui.fsZoom = std::clamp(g_gal.ui.fsZoom + zoomDelta,
                                     cfg.num("fullscreen.minZoom", 0.2f),
                                     cfg.num("fullscreen.maxZoom", 10.0f));
        return;
    }

    if (isCtrl) {
        // Continuous smooth zoom on Touchpad Pinch / Ctrl + Scroll wheel!
        float zoomDelta = (float)yoffset * SilverConfig::get().num("zoom.wheelStep", 0.04f);
        float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
        bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
        g_gal.ui.withViewAnchor(g_gal.timeline, (float)g_gal.windowH, [&] {
            g_gal.timeline.applyZoomDelta(zoomDelta, gridW, g_gal.currentRecords, hasBanner);
        });
        g_gal.ui.showZoomPopup = true;
        g_gal.ui.zoomPopupAutoCloseTimer = g_gal.ui.L.zoomAutoCloseSeconds;
        return;
    }

    g_gal.ui.handleScroll((float)yoffset, g_gal.timeline.totalContentHeight, (float)g_gal.windowH);
}

// Text entry for the settings search box.
static void charCallback(GLFWwindow* window, unsigned int codepoint) {
    g_gal.redrawPending = true;

    if (g_gal.ui.showSettings) {
        if (codepoint < 32 || codepoint > 126) return;   // settings filter is ASCII
        g_gal.ui.settingsQuery += (char)codepoint;
        g_gal.ui.rebuildSettingsFilter();
        return;
    }

    if (g_gal.ui.searchActive) {
        // Photo search accepts anything typeable; encode back to UTF-8 so
        // non-ASCII filenames can be searched for.
        if (codepoint < 32) return;
        if (codepoint < 0x80) {
            g_gal.ui.searchAppend((char)codepoint);
        } else if (codepoint < 0x800) {
            g_gal.ui.searchAppend((char)(0xC0 | (codepoint >> 6)));
            g_gal.ui.searchAppend((char)(0x80 | (codepoint & 0x3F)));
        } else if (codepoint < 0x10000) {
            g_gal.ui.searchAppend((char)(0xE0 | (codepoint >> 12)));
            g_gal.ui.searchAppend((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            g_gal.ui.searchAppend((char)(0x80 | (codepoint & 0x3F)));
        } else {
            g_gal.ui.searchAppend((char)(0xF0 | (codepoint >> 18)));
            g_gal.ui.searchAppend((char)(0x80 | ((codepoint >> 12) & 0x3F)));
            g_gal.ui.searchAppend((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            g_gal.ui.searchAppend((char)(0x80 | (codepoint & 0x3F)));
        }
    }
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    g_gal.redrawPending = true;
    // GLFW reports physical key positions, so a compositor-level remap such as
    // XKB's caps:swapescape never reaches us - the Caps position still arrives
    // as GLFW_KEY_CAPS_LOCK. Let the config tell us it means Escape.
    if (key == GLFW_KEY_CAPS_LOCK &&
        SilverConfig::get().flag("keys.capsLockActsAsEscape", false)) {
        key = GLFW_KEY_ESCAPE;
    }

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        // The settings palette is modal: while it is open it owns the keyboard.
        if (g_gal.ui.showSettings) {
            GalleryUI& ui = g_gal.ui;
            float listH = ui.settingsListHeight((float)g_gal.windowH);

            if (key == GLFW_KEY_ESCAPE) {
                if (ui.settingsOpenChoice >= 0) ui.settingsOpenChoice = -1;
                else if (!ui.settingsQuery.empty()) {
                    ui.settingsQuery.clear();
                    ui.rebuildSettingsFilter();
                } else {
                    ui.showSettings = false;
                }
            } else if (key == GLFW_KEY_DOWN) {
                ui.moveSettingsCursor(1, listH);
            } else if (key == GLFW_KEY_UP) {
                ui.moveSettingsCursor(-1, listH);
            } else if (key == GLFW_KEY_PAGE_DOWN) {
                ui.moveSettingsCursor(5, listH);
            } else if (key == GLFW_KEY_PAGE_UP) {
                ui.moveSettingsCursor(-5, listH);
            } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE) {
                if (const SettingSpec* spec = ui.settingAtFiltered(ui.settingsCursor)) {
                    settingActivate(*spec);
                    ui.commitSettingChange();
                }
            } else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
                if (const SettingSpec* spec = ui.settingAtFiltered(ui.settingsCursor)) {
                    int dir = (key == GLFW_KEY_RIGHT) ? 1 : -1;
                    if (spec->kind == SETTING_STEPPER || spec->kind == SETTING_SLIDER) {
                        settingNudge(*spec, dir);
                    } else if (spec->kind == SETTING_CHOICE) {
                        settingSetChoice(*spec, settingChoiceIndex(*spec) + dir);
                    } else if (spec->kind == SETTING_TOGGLE) {
                        settingToggle(*spec);
                    }
                    ui.commitSettingChange();
                }
            } else if (key == GLFW_KEY_BACKSPACE) {
                if (!ui.settingsQuery.empty()) {
                    ui.settingsQuery.pop_back();
                    ui.rebuildSettingsFilter();
                }
            } else if (key == GLFW_KEY_DELETE) {
                // Delete reverts the highlighted setting.
                if (const SettingSpec* spec = ui.settingAtFiltered(ui.settingsCursor)) {
                    SilverConfig::get().resetToDefault(spec->path);
                    ui.commitSettingChange();
                }
            }
            return;
        }

        // Search field has the keyboard while it is focused.
        if (g_gal.ui.searchActive) {
            if (key == GLFW_KEY_ESCAPE) {
                g_gal.ui.closeSearch(/*clearQuery=*/true);
                return;
            }
            if (key == GLFW_KEY_BACKSPACE) {
                g_gal.ui.searchBackspace();
                return;
            }
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                g_gal.ui.searchActive = false;   // keep the results, drop focus
                return;
            }
            // Printable input arrives through charCallback. Every remaining
            // physical key is consumed here so F, Space, arrows, Tab, and
            // other gallery bindings cannot fire while the field has focus.
            return;
        }

        if ((key == GLFW_KEY_F && (mods & GLFW_MOD_CONTROL)) ||
            (key == GLFW_KEY_SLASH && !(mods & GLFW_MOD_SHIFT))) {
            g_gal.ui.openSearch();
            return;
        }

        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_BACKSPACE) {
            // Escape unwinds one layer per press. On auto-repeat a single held
            // key would otherwise cascade through the whole chain - closing the
            // photo, the sidebar, the filter, the tab, and finally the app.
            if (action != GLFW_PRESS &&
                !SilverConfig::get().flag("keys.escapeRepeat", false)) {
                return;
            }
            // Escape always leaves full-screen view first - nothing else can
            // swallow it while a photo is open.
            if (g_gal.ui.isFullScreenView) {
                closeFullscreenAndRevealSelection();
            } else if (g_gal.ui.showShortcuts) {
                g_gal.ui.showShortcuts = false;
            } else if (g_gal.ui.showThemeMenu) {
                g_gal.ui.showThemeMenu = false;
            } else if (g_gal.ui.showZoomPopup) {
                g_gal.ui.showZoomPopup = false;
            } else if (g_gal.ui.showSidebar) {
                g_gal.ui.showSidebar = false;
                std::string anchor = g_gal.ui.selectedPath;
                g_gal.timeline.clearSelection();
                float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
                bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                g_gal.ui.relayoutKeepingAnchor(g_gal.timeline, gridW, hasBanner,
                                               (float)g_gal.windowH, anchor);
            } else if (!g_gal.ui.searchQuery.empty()) {
                g_gal.ui.closeSearch(/*clearQuery=*/true);
            } else if (!g_gal.ui.activeFolderFilter.empty()) {
                g_gal.ui.activeFolderFilter.clear();
                g_gal.ui.folderFilterRecursive = false;
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
                selectFlatIndex(nextIdx);
            }
        } else if (key == GLFW_KEY_RIGHT) {
            if (g_gal.ui.isFullScreenView) {
                if (g_gal.ui.fullScreenIndex + 1 < (int)g_gal.currentRecords.size()) {
                    g_gal.ui.openFullScreen(g_gal.ui.fullScreenIndex + 1, g_gal.currentRecords, g_gal.timeline);
                }
            } else if (!g_gal.timeline.flatAllItems.empty()) {
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::min((int)g_gal.timeline.flatAllItems.size() - 1, curIdx + 1);
                selectFlatIndex(nextIdx);
            }
        } else if (key == GLFW_KEY_UP) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::max(0, curIdx - cols);
                selectFlatIndex(nextIdx);
            }
        } else if (key == GLFW_KEY_DOWN) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::min((int)g_gal.timeline.flatAllItems.size() - 1, curIdx + cols);
                selectFlatIndex(nextIdx);
            }
        } else if (key == GLFW_KEY_PAGE_UP) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::max(0, curIdx - cols * 3);
                selectFlatIndex(nextIdx);
            }
        } else if (key == GLFW_KEY_PAGE_DOWN) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                int cols = std::max(1, g_gal.timeline.columns);
                int curIdx = g_gal.timeline.selectedFlatIndex;
                int nextIdx = (curIdx < 0) ? 0 : std::min((int)g_gal.timeline.flatAllItems.size() - 1, curIdx + cols * 3);
                selectFlatIndex(nextIdx);
            }
        } else if (key == GLFW_KEY_HOME) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                selectFlatIndex(0);
            }
        } else if (key == GLFW_KEY_END) {
            if (!g_gal.ui.isFullScreenView && !g_gal.timeline.flatAllItems.empty()) {
                selectFlatIndex((int)g_gal.timeline.flatAllItems.size() - 1);
            }
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_F || key == GLFW_KEY_SPACE) {
            if (!g_gal.currentRecords.empty()) {
                int index = g_gal.timeline.selectedFlatIndex >= 0
                          ? g_gal.timeline.selectedFlatIndex : 0;
                g_gal.ui.openFullScreen(index, g_gal.currentRecords, g_gal.timeline);
            }
        } else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
            float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
            bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
            g_gal.ui.withViewAnchor(g_gal.timeline, (float)g_gal.windowH, [&] {
                g_gal.timeline.zoomGrid(1, gridW, g_gal.currentRecords, hasBanner);
            });
            g_gal.ui.showZoomPopup = true;
            g_gal.ui.zoomPopupAutoCloseTimer = g_gal.ui.L.zoomAutoCloseSeconds;
        } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
            bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
            g_gal.ui.withViewAnchor(g_gal.timeline, (float)g_gal.windowH, [&] {
                g_gal.timeline.zoomGrid(-1, gridW, g_gal.currentRecords, hasBanner);
            });
            g_gal.ui.showZoomPopup = true;
            g_gal.ui.zoomPopupAutoCloseTimer = g_gal.ui.L.zoomAutoCloseSeconds;
        } else if ((key == GLFW_KEY_0 || key == GLFW_KEY_KP_0) && (mods & GLFW_MOD_CONTROL)) {
            float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
            bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
            g_gal.ui.withViewAnchor(g_gal.timeline, (float)g_gal.windowH, [&] {
                g_gal.timeline.resetGridZoom(gridW, g_gal.currentRecords, hasBanner);
            });
            g_gal.ui.showZoomPopup = true;
            g_gal.ui.zoomPopupAutoCloseTimer = g_gal.ui.L.zoomAutoCloseSeconds;
        } else if (key == GLFW_KEY_F5 || (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))) {
            g_gal.scanner.startScan(g_gal.db, true);
        } else if (key == GLFW_KEY_COMMA && (mods & GLFW_MOD_CONTROL)) {
            g_gal.ui.openSettings();
        } else if (key == GLFW_KEY_F1 ||
                   (key == GLFW_KEY_SLASH && (mods & GLFW_MOD_SHIFT))) {
            g_gal.ui.showShortcuts = !g_gal.ui.showShortcuts;
        } else if (key == GLFW_KEY_TAB) {
            // Tab / Shift+Tab cycle the tabs.
            int dir = (mods & GLFW_MOD_SHIFT) ? -1 : 1;
            int next = ((int)g_gal.ui.currentTab + dir + kGalleryTabCount) % kGalleryTabCount;
            switchTab((GalleryTab)next);
        } else if ((mods & GLFW_MOD_CONTROL) &&
                   key >= GLFW_KEY_1 && key < GLFW_KEY_1 + kGalleryTabCount) {
            switchTab((GalleryTab)(key - GLFW_KEY_1));
        } else if (key == GLFW_KEY_T) {
            g_gal.ui.theme.cycleThemeMode();
            g_gal.ui.themeToastText = "Theme: " + g_gal.ui.theme.getThemeModeName();
            g_gal.ui.themeToastTimer = silveranim::rates().toastSeconds;
        }
    }
}

// -----------------------------------------------------------------------------
// Main Application
// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::cout << "Starting SilverGallery (Timeline Image Organizer & Fast SQLite Engine)..." << std::endl;

    // Load every layout / spacing / animation value from JSON before anything
    // is sized or animated.
    SilverConfig::get().init(argv[0]);
    silveranim::reloadFromConfig();
    {
        const char* stats = getenv("SILVER_RENDER_STATS");
        g_gal.showRenderStats = (stats && stats[0] == '1');
        const char* tstats = getenv("SILVER_THUMB_STATS");
        g_gal.showThumbStats = (tstats && tstats[0] == '1');
    }
    silvercodec::gifMaxFrames() = std::max(1, SilverConfig::get().integer("gif.maxFrames", 300));
    silvercodec::gifMaxBytes()  = (size_t)std::max(8, SilverConfig::get().integer("gif.maxDecodeMegabytes", 256))
                                  * 1024ull * 1024ull;
    std::cout << "Config: " << SilverConfig::get().sourcePath << std::endl;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    int msaaSamples = std::clamp(SilverConfig::get().integer("display.msaaSamples", silver::defaults::msaaSamples),
                                 0, silver::limits::maxMsaaSamples);
    glfwWindowHint(GLFW_SAMPLES, msaaSamples);

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
    glfwSetWindowContentScaleCallback(g_gal.window, contentScaleCallback);
    glfwSetCursorPosCallback(g_gal.window, cursorPosCallback);
    glfwSetMouseButtonCallback(g_gal.window, mouseButtonCallback);
    glfwSetScrollCallback(g_gal.window, scrollCallback);
    glfwSetKeyCallback(g_gal.window, keyCallback);
    glfwSetCharCallback(g_gal.window, charCallback);

    // Initialize Subsystems
    g_gal.db.init();
    g_gal.font.init();
    g_gal.iconAtlas.init();
    syncPixelScale();
    g_gal.bgShader.init();
    if (msaaSamples > 0) glEnable(GL_MULTISAMPLE);

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

    // Pick up any config values that need the initialized subsystems.
    g_gal.ui.applyConfig();
    g_gal.timeline.applyConfig();

    // A settings edit must reach the grid too: layout metrics live on the
    // timeline, and changing them requires a reflow.
    g_gal.ui.onSearchChanged = []() { refreshRecords(); };

    g_gal.ui.onConfigChanged = []() {
        // Font size, hinting and icon weight rebuild their atlases in place, so
        // these apply live rather than needing a restart.
        g_gal.font.applyTextConfig();
        g_gal.iconAtlas.applyConfig();
        g_gal.timeline.applyConfig();
        float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
        bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
        g_gal.ui.relayoutKeepingAnchor(g_gal.timeline, gridW, hasBanner,
                                       (float)g_gal.windowH, g_gal.ui.selectedPath);
    };

    // Load initial cached records immediately from SQLite (< 5ms)
    refreshRecords();

    // ---------------------------------------------------------------------
    // Warm the first screenful of thumbnails BEFORE the first frame, so the
    // window opens already scrolled to the top showing real photos instead of
    // loading skeletons.
    // ---------------------------------------------------------------------
    {
        const SilverConfig& cfg = SilverConfig::get();
        int primeCount = cfg.integer("thumbnails.startupPrimeCount", 120);
        int primeMillis = cfg.integer("thumbnails.startupPrimeMillis", 900);

        // The window manager may still be maximizing us. Pump events and take
        // the real size before choosing a thumbnail tier - priming at the
        // default 1200x800 size cached a tier the maximized grid never asks for,
        // so every visible tile had to be decoded a second time.
        for (int i = 0; i < 3; ++i) {
            glfwPollEvents();
            glfwGetWindowSize(g_gal.window, &g_gal.windowW, &g_gal.windowH);
        }
        glfwGetFramebufferSize(g_gal.window, &g_gal.fbW, &g_gal.fbH);
        glViewport(0, 0, g_gal.fbW, g_gal.fbH);
        syncPixelScale();

        float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
        bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
        g_gal.timeline.relayout(gridW, hasBanner);

        int edge = g_gal.ui.thumbs.quantizeEdge(g_gal.timeline.itemSize);
        g_gal.timeline.updateVisibility(0.0f, (float)g_gal.windowH, -1.0f, -1.0f, 0.0f);

        // Enough tiles to cover the viewport, plus one row of lookahead.
        int perRow = std::max(1, g_gal.timeline.columns);
        float step = g_gal.timeline.itemSize + g_gal.timeline.gridGap;
        int rows = (step > 1.0f) ? (int)std::ceil((float)g_gal.windowH / step) + 1 : 4;
        int wanted = std::min(primeCount, perRow * std::max(1, rows));

        std::vector<std::string> firstScreen;
        for (auto* itm : g_gal.timeline.flatAllItems) {
            if ((int)firstScreen.size() >= wanted) break;
            firstScreen.push_back(itm->record.path);
        }

        if (!firstScreen.empty() && primeMillis > 0) {
            auto t0 = std::chrono::steady_clock::now();
            int ready = g_gal.ui.thumbs.primeStartup(firstScreen, edge, primeMillis);
            float ms = std::chrono::duration<float, std::milli>(
                           std::chrono::steady_clock::now() - t0).count();
            std::cout << "[SilverGallery] Primed " << ready << "/" << firstScreen.size()
                      << " thumbnails (tier " << edge << ") in " << (int)ms << " ms" << std::endl;
        }

        // Then keep generating the rest of the library's cache in the background
        // at the lowest priority so later scrolling never decodes anything.
        if (cfg.flag("thumbnails.prewarmLibrary", true) && !g_gal.currentRecords.empty()) {
            g_gal.ui.thumbs.prewarmLibrary(collectAllPaths(), edge);
        }

        // Clear out per-tier files left by the old cache layout. Off the main
        // thread: it walks the whole cache directory once.
        std::thread([]() { g_gal.ui.thumbs.pruneLegacyTiers(); }).detach();
    }

    // Start background folder scan for incremental updates
    g_gal.scanner.startScan(g_gal.db, true);

    auto lastTime = std::chrono::high_resolution_clock::now();

    // A worker finishing a decode must be able to wake the event loop, or the
    // thumbnail would not appear until the idle timeout expired.
    g_gal.ui.thumbs.onWorkReady = []() { glfwPostEmptyEvent(); };

    // Main Render Loop
    while (!glfwWindowShouldClose(g_gal.window)) {
        // Idle instead of spinning. A photo gallery redrawing at the refresh
        // rate with a completely static grid is pure battery burn; there is
        // nothing to draw between events unless something is animating.
        if (g_gal.redrawPending) {
            glfwPollEvents();
        } else {
            // The timeout also paces the theme manager's dconf poll, which
            // wants roughly 5 Hz.
            glfwWaitEventsTimeout(0.2);
        }

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::min(dt, 0.1f);

        // Fold in freshly indexed photos while a scan is still running, but at
        // most a couple of times a second so the grid never stutters.
        if (g_gal.scanner.hasFreshData.load()) {
            static float refreshCooldown = 0.0f;
            refreshCooldown -= dt;
            if (refreshCooldown <= 0.0f) {
                g_gal.scanner.hasFreshData.store(false);
                refreshCooldown = SilverConfig::get().num("scanner.refreshIntervalSeconds", 0.6f);
                g_gal.needsTimelineRebuild = true;
            }
        }

        // Live config reload: edit config/silver.json and the UI updates in place.
        static float configPoll = 0.0f;
        configPoll -= dt;
        if (configPoll <= 0.0f) {
            configPoll = 0.5f;
            if (SilverConfig::get().reloadIfChanged()) {
                g_gal.ui.applyConfig();
                g_gal.timeline.applyConfig();
                float gridW = g_gal.ui.gridWidth((float)g_gal.windowW);
                bool hasBanner = (!g_gal.ui.activeFolderFilter.empty() && g_gal.ui.currentTab != TAB_FOLDERS);
                g_gal.timeline.relayout(gridW, hasBanner);
                std::cout << "[SilverGallery] Reloaded config" << std::endl;
            }
        }

        // Zoom no longer invalidates the cache: the background pass produces one
        // master image per photo and every zoom level is derived from it, so
        // there is nothing to requeue when the tier changes.

        // Persist blurred placeholders produced while thumbnails decoded. Batched
        // on a timer: individually they would be thousands of tiny writes.
        {
            static float previewFlush = 0.0f;
            previewFlush -= dt;
            if (previewFlush <= 0.0f) {
                previewFlush = 2.0f;
                std::vector<std::pair<std::string, std::vector<unsigned char>>> batch;
                g_gal.ui.thumbs.drainPreviews(batch);
                if (!batch.empty()) g_gal.db.storePreviews(batch);
            }
        }

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
        // Everything below queues into one vertex buffer; the GPU is touched
        // once per frame in endFrame() instead of once per UI element.
        g_gal.font.beginFrame(g_gal.windowW, g_gal.windowH);
        g_gal.ui.render(g_gal.windowW, g_gal.windowH, (float)g_gal.mouseX, (float)g_gal.mouseY,
                        g_gal.timeline, g_gal.db, g_gal.scanner, g_gal.font, g_gal.iconAtlas,
                        g_gal.currentRecords);
        g_gal.font.endFrame();

        if (g_gal.showThumbStats) {
            static float thumbStatTimer = 0.0f;
            thumbStatTimer -= dt;
            if (thumbStatTimer <= 0.0f) {
                thumbStatTimer = 1.0f;
                std::cout << "[SilverGallery] " << g_gal.ui.thumbs.statsLine() << std::endl;
            }
        }

        if (g_gal.showRenderStats) {
            static float statTimer = 0.0f;
            statTimer -= dt;
            if (statTimer <= 0.0f) {
                statTimer = 1.0f;
                std::cout << "[SilverGallery] draw calls " << g_gal.font.drawCallsLastFrame
                          << " from " << g_gal.font.batchesLastFrame << " batches, "
                          << g_gal.font.verticesLastFrame << " verts" << std::endl;
            }
        }

        glfwSwapBuffers(g_gal.window);

        // Decide whether the next iteration needs to draw immediately.
        g_gal.redrawPending = g_gal.ui.isAnimating(g_gal.timeline,
                                                   g_gal.scanner.isScanning.load());
    }

    {
        // Do not lose the last batch on exit.
        std::vector<std::pair<std::string, std::vector<unsigned char>>> batch;
        g_gal.ui.thumbs.drainPreviews(batch);
        if (!batch.empty()) g_gal.db.storePreviews(batch);
    }

    g_gal.scanner.stop();
    g_gal.db.close();
    glfwDestroyWindow(g_gal.window);
    glfwTerminate();
    return 0;
}
