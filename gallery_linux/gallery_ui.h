#pragma once

#include "db.h"
#include "scanner.h"
#include "timeline.h"
#include "folder_tree.h"
#include "../viewer_linux/theme.h"
#include "../viewer_linux/font.h"
#include "../viewer_linux/icons.h"
#include "../viewer_linux/thumbnails.h"
#include "../viewer_linux/image_loader.h"
#include "../viewer_linux/async_loader.h"
#include "silver_config.h"
#include "silver_anim.h"
#include "silver_settings.h"
#include "silver_constants.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>

enum GalleryTab {
    TAB_ALL = 0,
    TAB_FAVORITES,
    TAB_FOLDERS
};

// Tabs are indexed by GalleryTab, so this is both the count and the array size.
static const int kGalleryTabCount = 3;

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
    int starred = -1;              // explicit post-toggle state for cache sync
};

// Every pixel metric the gallery chrome uses, sourced from config/silver.json.
struct GalleryLayout {
    // chrome
    float topBarH = 60.0f;
    float sidebarWidth = 340.0f;
    float sidebarInnerPad = 16.0f;
    float statusPillBottomMargin = 16.0f;
    float zoomPillBottomMargin = 18.0f;
    float zoomPillRightMargin = 20.0f;

    // photo tiles
    float tileRadius = 8.0f;
    float tileImagePad = 2.0f;
    float hoverLift = 3.5f;
    float shadowOffset = 2.0f;
    float shadowAlpha = 0.20f;
    float shadowHoverAlpha = 0.16f;
    float selBorderW = 3.0f;
    float hoverBorderW = 2.0f;
    float captionH = 30.0f;
    float starSize = 26.0f;
    float starMargin = 6.0f;

    // folders tab
    float folderSidePad = 24.0f;
    float folderGap = 16.0f;
    float folderCardW = 280.0f;
    float folderCardH = 80.0f;
    float folderCardRadius = 10.0f;
    float folderCardBorderW = 1.0f;
    float folderIconSize = 32.0f;

    // sidebar
    float sbPreviewH = 190.0f;
    float sbPreviewRadius = 8.0f;
    float sbButtonH = 40.0f;
    float sbButtonRadius = 6.0f;
    float sbMetaRowH = 24.0f;
    float sbPathLineH = 20.0f;
    int   sbPathMaxLines = 6;
    float sbSectionSpacing = 14.0f;
    float sbBorderW = 1.0f;

    // fullscreen lightbox
    float fsTopBarH = 64.0f;
    float fsImageMargin = 40.0f;
    float fsArrowSize = 48.0f;
    float fsButtonSize = 40.0f;

    // scrollbar / scrubber
    float scrubW = 16.0f;
    float scrubHandleH = 30.0f;
    float scrubTopMargin = 40.0f;
    float scrubBottomMargin = 80.0f;
    float scrubRadius = 4.0f;

    // zoom HUD
    float zoomPopupW = 208.0f;
    float zoomPopupH = 214.0f;
    float zoomPopupRadius = 10.0f;
    float zoomAutoCloseSeconds = 3.5f;

    // Every metric here is multiplied by the UI scale, so raising the font size
    // grows bars, rows, paddings and controls with it instead of letting text
    // overflow the boxes it sits in.
    float uiScale = 1.0f;

    void load() {
        const SilverConfig& c = SilverConfig::get();
        uiScale = silverUiScale();
        const float k = uiScale;

        topBarH                = c.num("layout.topBarHeight", 60.0f);
        sidebarWidth           = c.num("layout.sidebarWidth", 340.0f);
        sidebarInnerPad        = c.num("layout.sidebarInnerPadding", 16.0f);
        statusPillBottomMargin = c.num("layout.statusPillBottomMargin", 16.0f);
        zoomPillBottomMargin   = c.num("layout.zoomPillBottomMargin", 18.0f);
        zoomPillRightMargin    = c.num("layout.zoomPillRightMargin", 20.0f);

        tileRadius        = c.num("grid.tileRadius", 8.0f);
        tileImagePad      = c.num("grid.tileImagePadding", 2.0f);
        hoverLift         = c.num("grid.hoverLift", 3.5f);
        shadowOffset      = c.num("grid.shadowOffset", 2.0f);
        shadowAlpha       = c.num("grid.shadowAlpha", 0.20f);
        shadowHoverAlpha  = c.num("grid.shadowHoverAlpha", 0.16f);
        selBorderW        = c.num("grid.selectedBorderWidth", 3.0f);
        hoverBorderW      = c.num("grid.hoverBorderWidth", 2.0f);
        captionH          = c.num("grid.captionHeight", 30.0f);
        starSize          = c.num("grid.starBadgeSize", 26.0f);
        starMargin        = c.num("grid.starBadgeMargin", 6.0f);

        folderSidePad     = c.num("folders.sidePadding", 24.0f);
        folderGap         = c.num("folders.gap", 16.0f);
        folderCardW       = c.num("folders.cardWidth", 280.0f);
        folderCardH       = c.num("folders.cardHeight", 80.0f);
        folderCardRadius  = c.num("folders.cardRadius", 10.0f);
        folderCardBorderW = c.num("folders.cardBorderWidth", 1.0f);
        folderIconSize    = c.num("folders.iconSize", 32.0f);

        sbPreviewH        = c.num("sidebar.previewHeight", 190.0f);
        sbPreviewRadius   = c.num("sidebar.previewRadius", 8.0f);
        sbButtonH         = std::max(40.0f, c.num("sidebar.buttonHeight", 40.0f));
        sbButtonRadius    = c.num("sidebar.buttonRadius", 6.0f);
        sbMetaRowH        = c.num("sidebar.metaRowHeight", 24.0f);
        sbPathLineH       = c.num("sidebar.pathLineHeight", 20.0f);
        sbPathMaxLines    = std::max(1, c.integer("sidebar.pathMaxLines", 6));
        sbSectionSpacing  = c.num("sidebar.sectionSpacing", 14.0f);
        sbBorderW         = c.num("sidebar.borderWidth", 1.0f);

        fsTopBarH         = c.num("fullscreen.topBarHeight", 64.0f);
        fsImageMargin     = c.num("fullscreen.imageMargin", 40.0f);
        fsArrowSize       = c.num("fullscreen.arrowSize", 48.0f);
        fsButtonSize      = std::max(40.0f, c.num("fullscreen.buttonSize", 40.0f));

        scrubW            = c.num("scrollbar.width", 16.0f);
        scrubHandleH      = c.num("scrollbar.handleHeight", 30.0f);
        scrubTopMargin    = c.num("scrollbar.topMargin", 40.0f);
        scrubBottomMargin = c.num("scrollbar.bottomMargin", 80.0f);
        scrubRadius       = c.num("scrollbar.radius", 4.0f);

        zoomPopupW           = c.num("zoom.popupWidth", 208.0f);
        zoomPopupH           = c.num("zoom.popupHeight", 214.0f);
        zoomPopupRadius      = c.num("zoom.popupRadius", 10.0f);
        zoomAutoCloseSeconds = c.num("zoom.autoCloseSeconds", 3.5f);

        if (k != 1.0f) scaleAll(k);
    }

    // Scale everything that is a distance in points. Alphas, border widths and
    // timings are deliberately left alone.
    void scaleAll(float k) {
        float* metrics[] = {
            &topBarH, &sidebarWidth, &sidebarInnerPad, &statusPillBottomMargin,
            &zoomPillBottomMargin, &zoomPillRightMargin,
            &tileRadius, &tileImagePad, &hoverLift, &captionH, &starSize, &starMargin,
            &folderSidePad, &folderGap, &folderCardW, &folderCardH, &folderCardRadius, &folderIconSize,
            &sbPreviewH, &sbPreviewRadius, &sbButtonH, &sbButtonRadius,
            &sbMetaRowH, &sbPathLineH, &sbSectionSpacing,
            &fsTopBarH, &fsImageMargin, &fsArrowSize, &fsButtonSize,
            &scrubW, &scrubHandleH, &scrubTopMargin, &scrubBottomMargin, &scrubRadius,
            &zoomPopupW, &zoomPopupH, &zoomPopupRadius,
        };
        for (float* m : metrics) *m *= k;
    }
};

class GalleryUI {
public:
    GalleryLayout L;
    GalleryTab currentTab = TAB_ALL;
    std::string searchQuery;
    std::string activeFolderFilter;
    bool folderFilterRecursive = false;   // include subfolders
    bool isSearching = false;

    // Selection & Sidebar
    bool showSidebar = false;
    float sidebarWidth = 340.0f;   // mirrors L.sidebarWidth; kept for call sites
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

    UIRect tabRects[kGalleryTabCount];
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

    // Folders tab: browse one level at a time instead of listing every
    // photo-bearing directory on the machine at once.
    FolderTree folderTree;
    std::string browsePath;                 // empty = the roots
    std::vector<int> browseChildren;        // node indices shown right now
    std::vector<UIRect> breadcrumbRects;
    std::vector<int> breadcrumbNodes;
    UIRect browseUpRect;
    UIRect browseShowAllRect;

    // Search
    bool searchActive = false;
    UIRect searchBarRect;
    UIRect searchClearRect;
    std::function<void()> onSearchChanged;
    float searchDebounce = 0.0f;
    bool searchDirty = false;

    void openSearch() {
        searchActive = true;
    }

    void closeSearch(bool clearQuery) {
        searchActive = false;
        if (clearQuery && !searchQuery.empty()) {
            searchQuery.clear();
            searchDirty = true;
            searchDebounce = 0.0f;
        }
    }

    void searchAppend(char c) {
        searchQuery += c;
        searchDirty = true;
        searchDebounce = 0.25f;   // re-query once typing pauses
    }

    void searchBackspace() {
        if (searchQuery.empty()) return;
        // Never leave half a UTF-8 sequence behind.
        size_t n = searchQuery.size();
        while (n > 0 && ((unsigned char)searchQuery[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0) n--;
        searchQuery.resize(n);
        searchDirty = true;
        searchDebounce = 0.25f;
    }

    void enterFolder(const std::string& path) {
        browsePath = path;
        browseChildren = folderTree.childrenOf(path);
        breadcrumbNodes = folderTree.breadcrumb(path);
        targetScrollY = 0.0f;
        scrollY = 0.0f;
        scrollVel = 0.0f;
        scrollPrevTarget = 0.0f;
    }

    void folderGoUp() {
        if (browsePath.empty()) return;
        const FolderNode* node = folderTree.find(browsePath);
        if (node && node->parent >= 0) enterFolder(folderTree.nodes[(size_t)node->parent].path);
        else enterFolder("");
    }

    // Cached aggregates. These were being recomputed every frame: the status
    // pill ran COUNT(*) and SUM() over the whole table, and the Folders tab ran
    // a GROUP BY - three full scans per frame at 60 fps. They only change when
    // the record set does, so main() invalidates them from refreshRecords().
    bool dbAggregatesValid = false;
    int cachedTotalCount = 0;
    int64_t cachedTotalBytes = 0;
    bool folderListValid = false;

    size_t lastVisibleSig = 0;   // guards the per-frame thumbnail request

    void invalidateDbCache() {
        dbAggregatesValid = false;
        folderListValid = false;
        lastVisibleSig = 0;   // force the next frame to re-queue
    }

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
    UIRect groupRects[4];   // Day / Week / Month / Year granularity chips

    // Settings palette (Ctrl+,)
    bool showSettings = false;
    float settingsAnim = 0.0f;
    float settingsVel = 0.0f;
    float settingsPrevTarget = 0.0f;
    std::string settingsQuery;
    int settingsCursor = 0;        // index into the filtered list
    float settingsScroll = 0.0f;
    int settingsOpenChoice = -1;   // filtered index whose dropdown is expanded
    bool settingsDraggingSlider = false;
    int settingsDragRow = -1;
    std::vector<int> settingsFiltered;     // schema indices passing the search
    std::vector<UIRect> settingsRowRects;
    std::vector<UIRect> settingsCtrlRects;
    std::vector<UIRect> settingsRevertRects;
    UIRect settingsCloseRect;
    UIRect settingsSearchRect;

    // Keyboard shortcuts overlay (F1 or ?)
    bool showShortcuts = false;
    float shortcutsAnim = 0.0f;
    float shortcutsVel = 0.0f;
    float shortcutsPrevTarget = 0.0f;

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

    // Smooth Animation States (value + spring velocity for each channel)
    float sidebarAnim = 0.0f;       // 0.0 = closed, 1.0 = open
    float tabAnimX = 0.0f;          // Smooth sliding active tab indicator X
    float tabAnimW = 0.0f;          // Smooth sliding active tab indicator W
    float fsAnim = 0.0f;            // 0.0 = grid view, 1.0 = fullscreen lightbox
    float zoomPopupAnim = 0.0f;     // 0.0 = hidden, 1.0 = popup visible

    // Spring velocity + last target, so the `response` parameter can anticipate.
    float scrollVel = 0.0f, scrollPrevTarget = 0.0f;
    float sidebarVel = 0.0f, sidebarPrevTarget = 0.0f;
    float tabXVel = 0.0f, tabXPrevTarget = 0.0f;
    float tabWVel = 0.0f, tabWPrevTarget = 0.0f;
    float fsVel = 0.0f, fsPrevTarget = 0.0f;
    float zoomPopupVel = 0.0f, zoomPopupPrevTarget = 0.0f;
    float themeMenuVel = 0.0f, themeMenuPrevTarget = 0.0f;

    // High resolution preview texture for sidebar and fullscreen
    ImageTexture highResPreview;

    // Double-click detection
    int lastClickIndex = -1;
    double lastClickTimeSec = 0.0;

    // Tiny GL textures for the blurred placeholders.
    //
    // Each is 8x8 RGB (192 bytes), so even a few hundred cost well under a
    // megabyte. Only tiles currently on screen get one; the pool is trimmed
    // rather than grown to the size of the library.
    std::unordered_map<std::string, GLuint> previewTextures;
    std::vector<std::string> previewOrder;
    size_t maxPreviewTextures = 512;

    GLuint previewTextureFor(const GalleryRecord& rec) {
        if (!rec.hasPreview) return 0;

        auto it = previewTextures.find(rec.path);
        if (it != previewTextures.end()) return it->second;

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     GalleryRecord::kPreviewDim, GalleryRecord::kPreviewDim, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, rec.preview);
        // Bilinear on an 8x8 source is what turns it into a soft gradient
        // rather than visible blocks.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        previewTextures[rec.path] = tex;
        previewOrder.push_back(rec.path);

        // Oldest-first trim; these are cheap to recreate.
        while (previewOrder.size() > maxPreviewTextures) {
            const std::string& victim = previewOrder.front();
            auto vit = previewTextures.find(victim);
            if (vit != previewTextures.end()) {
                glDeleteTextures(1, &vit->second);
                previewTextures.erase(vit);
            }
            previewOrder.erase(previewOrder.begin());
        }
        return tex;
    }

    void clearPreviewTextures() {
        for (auto& kv : previewTextures) {
            if (kv.second) glDeleteTextures(1, &kv.second);
        }
        previewTextures.clear();
        previewOrder.clear();
    }

    // Width reserved for the scroll track. The grid never lays out into it, so
    // the scrollbar has a column of its own instead of floating over photos.
    float scrollbarGutter() const {
        return L.scrubW + 10.0f * L.uiScale;
    }

    // The single definition of how wide the photo grid is.
    //
    // This expression used to be copy-pasted at thirteen call sites, which is
    // how the scrollbar ended up overlapping the tiles: the layout width and the
    // scrollbar position were computed from different formulas.
    float gridWidth(float windowW) const {
        float w = windowW;
        if (showSidebar && windowW >= 750.0f) w -= L.sidebarWidth;
        w -= scrollbarGutter();
        return std::max(200.0f, w);
    }

    // True while anything still needs to be redrawn. When this goes false the
    // main loop sleeps on events instead of spinning at the refresh rate.
    bool isAnimating(const TimelineManager& timeline, bool scanning) const {
        const float eps = 0.002f;
        auto moving = [&](float value, float target) {
            return std::abs(value - target) > eps;
        };

        if (scanning) return true;                       // spinner + incoming photos
        if (moving(scrollY, targetScrollY)) return true;
        if (scrollbarAlpha > eps && scrollbarAlpha < 1.0f) return true;
        if (moving(sidebarAnim, (showSidebar && !selectedPath.empty()) ? 1.0f : 0.0f)) return true;
        if (moving(fsAnim, isFullScreenView ? 1.0f : 0.0f)) return true;
        if (moving(zoomPopupAnim, (showZoomPopup || isDraggingZoomSlider) ? 1.0f : 0.0f)) return true;
        if (moving(themeMenuAnim, showThemeMenu ? 1.0f : 0.0f)) return true;
        if (moving(shortcutsAnim, showShortcuts ? 1.0f : 0.0f)) return true;
        if (moving(settingsAnim, showSettings ? 1.0f : 0.0f)) return true;
        if (themeToastTimer > 0.0f || zoomPopupAutoCloseTimer > 0.0f) return true;
        if (thumbs.hasReadyUploads()) return true;       // drain bounded GPU uploads
        if (timeline.hasActiveMotion()) return true;
        return false;
    }

    // Multiply a palette colour by a panel's animation alpha.
    //
    // Popups fade by scaling their own alpha, but any child drawn with a raw
    // palette colour stays fully opaque and hangs in the air after the card
    // behind it has dissolved. Everything inside an animated panel goes through
    // this.
    static Color4 fade(const Color4& c, float a) {
        return Color4(c.r, c.g, c.b, c.a * a);
    }

    // Neutral inset image edge: pure white in dark mode and pure black in
    // light mode. A themed/tinted edge picks up the surrounding surface and
    // makes pale photographs look dirty.
    static Color4 imageOutline(const ThemePalette& pal, float alpha = 1.0f) {
        return pal.isDark ? Color4(1, 1, 1, 0.10f * alpha)
                          : Color4(0, 0, 0, 0.10f * alpha);
    }

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
                // Same boundary-aware binary search as the plain path; the old
                // loop walked back one byte at a time and could split a
                // multi-byte character in the middle of the stem.
                size_t keep = font.longestPrefixThatFits(stem, maxW, ell + ext);
                // Nothing fits: still show one whole character rather than an
                // empty stem (and never half a character).
                if (keep == 0) keep = FontRenderer::utf8SequenceLength(stem, 0);
                return stem.substr(0, keep) + ell + ext;
            }
        }

        // Plain end truncation is shared with the viewer.
        return font.fitWithEllipsis(text, maxW);
    }

    // Breaks a full filesystem path into lines that fit maxW, preferring to
    // split on path separators so the whole path stays readable.
    static std::vector<std::string> wrapPath(FontRenderer& font, const std::string& path, float maxW, int maxLines = 6) {
        std::vector<std::string> lines;
        if (path.empty() || maxW <= 10.0f) {
            lines.push_back(path);
            return lines;
        }

        std::string cur;
        size_t i = 0;
        while (i < path.length()) {
            // Take the next path segment (including its leading separator).
            size_t next = path.find_first_of("/\\", i + 1);
            std::string seg = (next == std::string::npos) ? path.substr(i) : path.substr(i, next - i);
            i = (next == std::string::npos) ? path.length() : next;

            if (cur.empty()) {
                cur = seg;
            } else if (font.measureText(cur + seg) <= maxW) {
                cur += seg;
            } else {
                lines.push_back(cur);
                cur = seg;
            }

            // A single segment can still overflow (very long filenames) - hard wrap it.
            while (font.measureText(cur) > maxW && cur.length() > 1) {
                std::string head = cur;
                while (head.length() > 1 && font.measureText(head) > maxW) head.pop_back();
                lines.push_back(head);
                cur = cur.substr(head.length());
            }
        }
        if (!cur.empty()) lines.push_back(cur);
        if (lines.empty()) lines.push_back(path);
        if (maxLines > 0 && (int)lines.size() > maxLines) {
            lines.resize((size_t)maxLines);
            lines.back() += "...";
        }
        return lines;
    }

    // Re-read every metric from the JSON config (also called on hot reload).
    void applyConfig() {
        L.load();
        sidebarWidth = L.sidebarWidth;

        const float k = L.uiScale;
        settingsRowH    = 54.0f * k;
        settingsCardW   = 760.0f * k;
        settingsCardH   = 620.0f * k;
        settingsSearchH = 52.0f * k;
        settingsFooterH = 38.0f * k;
        settingsCtrlW   = 200.0f * k;
        silveranim::reloadFromConfig();
        thumbs.applyConfig();
        fullResLoader.maxDecodeEdge = SilverConfig::get().integer(
            "thumbnails.previewMaxEdge", silver::defaults::fullResolutionDecode);
    }

    GalleryUI() {
        applyConfig();
        theme.init();
        thumbs.setAtlasEnabled(true);
        thumbs.init();
        fullResLoader.init();
    }

    void update(float dt, float totalContentHeight, float windowH, bool isScanning) {
        theme.update(dt);
        thumbs.updateGL(dt);

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

        const silveranim::Rates& anim = silveranim::rates();

        // 1. Smooth scroll interpolation
        float diff = targetScrollY - scrollY;
        silveranim::driveTrackedPos(scrollY, scrollVel, scrollPrevTarget, targetScrollY, anim.chScroll, dt);

        // Clamp scroll bounds
        float maxScroll = std::max(0.0f, totalContentHeight - windowH);
        if (targetScrollY < 0.0f) targetScrollY = 0.0f;
        if (targetScrollY > maxScroll) targetScrollY = maxScroll;
        if (scrollY < 0.0f) { scrollY = 0.0f; scrollVel = 0.0f; }
        if (scrollY > maxScroll) { scrollY = maxScroll; scrollVel = 0.0f; }

        // Fade scrollbar
        if (std::abs(diff) > 1.0f || isDraggingScrubber) {
            scrollbarAlpha = std::min(1.0f, scrollbarAlpha + dt * 8.0f);
        } else {
            scrollbarAlpha = std::max(0.0f, scrollbarAlpha - dt * 2.0f);
        }

        // 2. Sidebar slide animation
        float targetSb = (showSidebar && !selectedPath.empty()) ? 1.0f : 0.0f;
        silveranim::driveTrackedFade(sidebarAnim, sidebarVel, sidebarPrevTarget, targetSb, anim.chSidebar, dt);
        if (sidebarAnim == targetSb && !showSidebar) {
            selectedPath.clear();
        }

        // 3. Fullscreen lightbox transition animation
        silveranim::driveTrackedFade(fsAnim, fsVel, fsPrevTarget, isFullScreenView ? 1.0f : 0.0f, anim.chFullscreen, dt);

        // 4. Zoom Popup HUD animation & auto-close timer
        if (zoomPopupAutoCloseTimer > 0.0f) {
            silveranim::tickTimer(zoomPopupAutoCloseTimer, dt);
            if (zoomPopupAutoCloseTimer == 0.0f && !isDraggingZoomSlider) {
                showZoomPopup = false;
            }
        }
        float targetZoom = (showZoomPopup || isDraggingZoomSlider) ? 1.0f : 0.0f;
        silveranim::driveTrackedFade(zoomPopupAnim, zoomPopupVel, zoomPopupPrevTarget, targetZoom, anim.chZoomPopup, dt);

        // 5. Theme Mode Menu animation & toast timer
        silveranim::driveTrackedFade(themeMenuAnim, themeMenuVel, themeMenuPrevTarget, showThemeMenu ? 1.0f : 0.0f, anim.chThemeMenu, dt);
        silveranim::driveTrackedFade(shortcutsAnim, shortcutsVel, shortcutsPrevTarget, showShortcuts ? 1.0f : 0.0f, anim.chZoomPopup, dt);
        silveranim::driveTrackedFade(settingsAnim, settingsVel, settingsPrevTarget, showSettings ? 1.0f : 0.0f, anim.chZoomPopup, dt);
        silveranim::tickTimer(themeToastTimer, dt);

        // Re-run the query once typing pauses, rather than on every keystroke.
        if (searchDirty && searchDebounce > 0.0f) {
            searchDebounce -= dt;
            if (searchDebounce <= 0.0f) {
                searchDebounce = 0.0f;
                searchDirty = false;
                if (onSearchChanged) onSearchChanged();
            }
        } else if (searchDirty) {
            searchDirty = false;
            if (onSearchChanged) onSearchChanged();
        }

        // 6. Sliding Tab Indicator
        if (currentTab >= 0 && currentTab < kGalleryTabCount && tabRects[currentTab].w > 0.0f) {
            float targetTabX = tabRects[currentTab].x;
            float targetTabW = tabRects[currentTab].w;
            if (tabAnimW <= 0.0f) {
                tabAnimX = tabXPrevTarget = targetTabX;
                tabAnimW = tabWPrevTarget = targetTabW;
            } else {
                silveranim::driveTrackedPos(tabAnimX, tabXVel, tabXPrevTarget, targetTabX, anim.chTabIndicator, dt);
                silveranim::driveTrackedPos(tabAnimW, tabWVel, tabWPrevTarget, targetTabW, anim.chTabIndicator, dt);
            }
        }
    }

    void handleScroll(float dy, float totalContentHeight, float windowH) {
        if (isFullScreenView) {
            // Zoom in/out in fullscreen
            const SilverConfig& cfg = SilverConfig::get();
            float zoomDelta = dy * cfg.num("fullscreen.zoomStep", 0.15f);
            fsZoom = std::clamp(fsZoom + zoomDelta,
                                cfg.num("fullscreen.minZoom", 0.2f),
                                cfg.num("fullscreen.maxZoom", 10.0f));
            return;
        }

        float maxScroll = std::max(0.0f, totalContentHeight - windowH);
        targetScrollY -= dy * silveranim::rates().scrollWheelStep;
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

    // A point of reference in the grid, held across a layout change.
    //
    // flatIndex is stable across both relayout() and regroup(): the photo order
    // never changes, only where each tile sits. Zooming without this pinned the
    // scroll offset instead of the content, so once the user had scrolled down,
    // changing zoom moved everything they were looking at far outside the
    // animation band - which is why the glide only appeared at the top.
    struct ViewAnchor {
        int flatIndex = -1;
        float screenY = 0.0f;
        bool valid = false;
    };

    ViewAnchor captureViewAnchor(TimelineManager& timeline, float windowH) {
        ViewAnchor a;
        if (TimelineItem* itm = timeline.itemNearestViewport(scrollY, windowH)) {
            a.flatIndex = itm->flatIndex;
            a.screenY = itm->y - scrollY;
            a.valid = true;
        }
        return a;
    }

    void restoreViewAnchor(TimelineManager& timeline, const ViewAnchor& a, float windowH) {
        if (!a.valid || a.flatIndex < 0 ||
            a.flatIndex >= (int)timeline.flatAllItems.size()) return;

        const TimelineItem* itm = timeline.flatAllItems[(size_t)a.flatIndex];
        float maxScroll = std::max(0.0f, timeline.totalContentHeight - windowH);
        float wanted = std::clamp(itm->y - a.screenY, 0.0f, maxScroll);

        // Move the animating tiles with the scroll change so each one starts its
        // glide from the screen position it already occupied.
        timeline.shiftAnimatedPositions(wanted - scrollY);

        // Snap: the tiles are already animating to their new slots, and letting
        // the scroll spring run at the same time reads as two competing motions.
        targetScrollY = wanted;
        scrollY = wanted;
        scrollVel = 0.0f;
        scrollPrevTarget = wanted;
    }

    // Run a layout-changing operation with the viewport held steady.
    template <typename Fn>
    void withViewAnchor(TimelineManager& timeline, float windowH, Fn&& fn) {
        ViewAnchor a = captureViewAnchor(timeline, windowH);
        TimelineGrouping oldGrouping = timeline.grouping;
        float oldScrollY = scrollY;
        fn();
        restoreViewAnchor(timeline, a, windowH);
        if (timeline.grouping != oldGrouping)
            timeline.beginVisibleRegroupAnimation(oldScrollY, scrollY, windowH);
    }

    // Re-lay out the grid while keeping `anchorPath` visually pinned.
    //
    // Opening the sidebar narrows the grid, which changes the column count and
    // moves every tile. Without an anchor the photo you just clicked can end up
    // far off screen - you select an image and it vanishes from the grid.
    void relayoutKeepingAnchor(TimelineManager& timeline, float gridW, bool hasBanner,
                               float windowH, const std::string& anchorPath,
                               bool snapAll = false) {
        float anchorScreenY = 0.0f;
        bool haveAnchor = false;

        if (!anchorPath.empty()) {
            if (const TimelineItem* before = timeline.findItem(anchorPath)) {
                anchorScreenY = before->y - scrollY;
                haveAnchor = true;
            }
        }

        timeline.relayout(gridW, hasBanner, snapAll);

        if (haveAnchor) {
            if (const TimelineItem* after = timeline.findItem(anchorPath)) {
                // Snap rather than animate: the layout changed under the user,
                // so a glide here reads as the grid drifting on its own.
                float maxScroll = std::max(0.0f, timeline.totalContentHeight - windowH);
                float wanted = std::clamp(after->y - anchorScreenY, 0.0f, maxScroll);
                if (!snapAll)
                    timeline.shiftAnimatedPositions(wanted - scrollY);
                targetScrollY = wanted;
                scrollY = targetScrollY;
                scrollVel = 0.0f;
                scrollPrevTarget = targetScrollY;

                // If pinning still leaves it clipped, pull it fully into view.
                ensureItemVisible(after->y, after->h, timeline.totalContentHeight, windowH);
            }
        } else {
            // Resizing can drastically reduce total height. Never retain a
            // scroll offset beyond the new document, which otherwise exposes
            // an empty or partially populated frame during fast maximize.
            float maxScroll = std::max(0.0f, timeline.totalContentHeight - windowH);
            scrollY = std::clamp(scrollY, 0.0f, maxScroll);
            targetScrollY = scrollY;
            scrollVel = 0.0f;
            scrollPrevTarget = scrollY;
        }
    }

    // Scroll the minimum amount needed to show an item completely.
    void ensureItemVisible(float itemY, float itemH, float totalContentHeight, float windowH) {
        float topBarH = L.topBarH;
        float margin = 12.0f;
        float maxScroll = std::max(0.0f, totalContentHeight - windowH);

        float wanted = targetScrollY;
        if (itemY - margin < targetScrollY + topBarH) {
            wanted = itemY - topBarH - margin;
        } else if (itemY + itemH + margin > targetScrollY + windowH) {
            wanted = itemY + itemH + margin - windowH;
        }

        wanted = std::clamp(wanted, 0.0f, maxScroll);
        if (std::abs(wanted - targetScrollY) > 0.5f) {
            targetScrollY = wanted;
            scrollY = wanted;
            scrollVel = 0.0f;
            scrollPrevTarget = wanted;
            // Emphasize only when keyboard navigation actually scrolls the
            // viewport. Merely moving selection inside the visible area must
            // not flash the scrollbar on every key press.
            scrollbarAlpha = 1.0f;
        }
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

        std::vector<std::string> nearby;
        nearby.reserve(7);
        nearby.push_back(selectedPath);
        for (int distance = 1; distance <= 3; ++distance) {
            if (index + distance < (int)records.size())
                nearby.push_back(records[(size_t)(index + distance)].path);
            if (index - distance >= 0)
                nearby.push_back(records[(size_t)(index - distance)].path);
        }
        fullResLoader.updatePreloadPaths(nearby);
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
        // 1.4 SETTINGS PALETTE (modal - swallows everything behind it)
        // -------------------------------------------------------------
        if (showSettings) {
            handleSettingsClick(mx, my, windowW, windowH);
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
            // The sidebar starts below the top bar. Absorbing the full column
            // made the refresh and theme buttons unclickable whenever it was
            // open, because they sit inside the same x range.
            if (mx >= sbX && my > L.topBarH) {
                // Close Sidebar Button
                if (isInside(mx, my, sbCloseRect.x, sbCloseRect.y, sbCloseRect.w, sbCloseRect.h)) {
                    std::string closingPath = selectedPath;
                    showSidebar = false;
                    // The explicit close button should close immediately. The
                    // old slide-out kept intercepting/rendering for several
                    // frames, which made the X appear not to work.
                    sidebarAnim = 0.0f;
                    sidebarVel = 0.0f;
                    sidebarPrevTarget = 0.0f;
                    selectedPath.clear();
                    act.targetPath = closingPath;   // anchor for the reflow
                    timeline.clearSelection();
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
                    act.starred = selectedRecord.starred;
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
                zoomPopupAutoCloseTimer = L.zoomAutoCloseSeconds;
            }
            return act;
        }

        if (showZoomPopup) {
            if (isInside(mx, my, zoomPopupRect.x, zoomPopupRect.y, zoomPopupRect.w, zoomPopupRect.h)) {
                zoomPopupAutoCloseTimer = L.zoomAutoCloseSeconds;
                float gridW = gridWidth(windowW);
                bool hasBanner = (!activeFolderFilter.empty() && currentTab != TAB_FOLDERS);

                // Slider Track Drag
                if (isInside(mx, my, zoomSliderTrackRect.x, zoomSliderTrackRect.y, zoomSliderTrackRect.w, zoomSliderTrackRect.h)) {
                    isDraggingZoomSlider = true;
                    float ratio = 1.0f - std::clamp((my - zoomSliderTrackRect.y) / zoomSliderTrackRect.h, 0.0f, 1.0f);
                    withViewAnchor(timeline, windowH, [&] {
                        timeline.setZoomScale(ratio, gridW, records, hasBanner);
                    });
                    return act;
                }

                // Timeline granularity chips
                for (int gi = 0; gi < 4; ++gi) {
                    if (groupRects[gi].w > 0.0f &&
                        isInside(mx, my, groupRects[gi].x, groupRects[gi].y, groupRects[gi].w, groupRects[gi].h)) {
                        withViewAnchor(timeline, windowH, [&] {
                            timeline.setGrouping((TimelineGrouping)gi, gridW, records, hasBanner);
                        });
                        return act;
                    }
                }

                // Check Presets
                if (isInside(mx, my, presetXLRect.x, presetXLRect.y, presetXLRect.w, presetXLRect.h)) {
                    withViewAnchor(timeline, windowH, [&] {
                        timeline.setPreset(PRESET_XL, gridW, records, hasBanner);
                    });
                    return act;
                }
                if (isInside(mx, my, presetLargeRect.x, presetLargeRect.y, presetLargeRect.w, presetLargeRect.h)) {
                    withViewAnchor(timeline, windowH, [&] {
                        timeline.setPreset(PRESET_LARGE, gridW, records, hasBanner);
                    });
                    return act;
                }
                if (isInside(mx, my, presetMediumRect.x, presetMediumRect.y, presetMediumRect.w, presetMediumRect.h)) {
                    withViewAnchor(timeline, windowH, [&] {
                        timeline.setPreset(PRESET_MEDIUM, gridW, records, hasBanner);
                    });
                    return act;
                }
                if (isInside(mx, my, presetSmallRect.x, presetSmallRect.y, presetSmallRect.w, presetSmallRect.h)) {
                    withViewAnchor(timeline, windowH, [&] {
                        timeline.setPreset(PRESET_SMALL, gridW, records, hasBanner);
                    });
                    return act;
                }
                return act; // Event absorbed by popup
            } else {
                showZoomPopup = false;
            }
        }

        // -------------------------------------------------------------
        // 2.9 SEARCH BAR
        // -------------------------------------------------------------
        if (searchClearRect.w > 0.0f &&
            isInside(mx, my, searchClearRect.x, searchClearRect.y, searchClearRect.w, searchClearRect.h)) {
            closeSearch(/*clearQuery=*/true);
            return act;
        }
        if (searchBarRect.w > 0.0f &&
            isInside(mx, my, searchBarRect.x, searchBarRect.y, searchBarRect.w, searchBarRect.h)) {
            searchActive = true;
            return act;
        }
        // Clicking elsewhere commits the current query and returns keyboard
        // ownership to the gallery without clearing the filtered results.
        if (searchActive) searchActive = false;

        // -------------------------------------------------------------
        // 3. TOP BAR TAB & BUTTON CLICKS
        // -------------------------------------------------------------
        for (int i = 0; i < kGalleryTabCount; ++i) {
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
            folderFilterRecursive = false;
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
            float visibleFrac = std::clamp(windowH / std::max(1.0f, timeline.totalContentHeight), 0.05f, 1.0f);
            float handleH = std::max(L.scrubHandleH, scrubberRect.h * visibleFrac);
            float usable = std::max(1.0f, scrubberRect.h - handleH);
            float ratio = std::clamp((my - scrubberRect.y - handleH * 0.5f) / usable, 0.0f, 1.0f);
            targetScrollY = timeline.getScrollForScrubRatio(ratio, windowH);
            return act;
        }

        // -------------------------------------------------------------
        // 5. FOLDERS TAB ITEM CLICKS
        // -------------------------------------------------------------
        if (currentTab == TAB_FOLDERS) {
            // Breadcrumb: slot 0 is the root listing, the rest are ancestors.
            for (size_t i = 0; i < breadcrumbRects.size(); ++i) {
                const UIRect& r = breadcrumbRects[i];
                if (r.w <= 0.0f) continue;
                if (!isInside(mx, my, r.x, r.y, r.w, r.h)) continue;

                if (i == 0) enterFolder("");
                else if (i - 1 < breadcrumbNodes.size()) {
                    enterFolder(folderTree.nodes[(size_t)breadcrumbNodes[i - 1]].path);
                }
                return act;
            }

            // "View all N photos" - everything beneath this folder.
            if (browseShowAllRect.w > 0.0f &&
                isInside(mx, my, browseShowAllRect.x, browseShowAllRect.y,
                         browseShowAllRect.w, browseShowAllRect.h)) {
                activeFolderFilter = browsePath;
                folderFilterRecursive = true;
                currentTab = TAB_ALL;
                showSidebar = false;
                timeline.clearSelection();
                act.type = GalleryUIAction::SELECT_FOLDER;
                act.folderFilter = browsePath;
                targetScrollY = 0.0f;
                return act;
            }

            for (size_t i = 0; i < folderItemRects.size() && i < browseChildren.size(); ++i) {
                if (!isInside(mx, my, folderItemRects[i].x, folderItemRects[i].y,
                              folderItemRects[i].w, folderItemRects[i].h)) continue;

                const FolderNode& node = folderTree.nodes[(size_t)browseChildren[i]];

                // A folder with subfolders is a place to go into; a leaf is a
                // set of photos to look at.
                if (!node.children.empty()) {
                    enterFolder(node.path);
                } else {
                    activeFolderFilter = node.path;
                    folderFilterRecursive = true;
                    currentTab = TAB_ALL;
                    showSidebar = false;
                    timeline.clearSelection();
                    act.type = GalleryUIAction::SELECT_FOLDER;
                    act.folderFilter = node.path;
                    targetScrollY = 0.0f;
                }
                return act;
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
                act.starred = itm->record.starred;
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
            act.targetPath = selectedPath;   // anchor for the reflow
            timeline.clearSelection();
            act.type = GalleryUIAction::CLOSE_SIDEBAR;
            return act;
        }

        return act;
    }

    // Returns true when the click was consumed by the palette.
    void handleSettingsClick(float mx, float my, float windowW, float windowH) {
        if (isInside(mx, my, settingsCloseRect.x, settingsCloseRect.y,
                     settingsCloseRect.w, settingsCloseRect.h)) {
            showSettings = false;
            return;
        }

        // An open dropdown gets first refusal on the click.
        if (settingsOpenChoice >= 0 && settingsOpenChoice < (int)settingsCtrlRects.size()) {
            const SettingSpec* spec = settingAtFiltered(settingsOpenChoice);
            const UIRect& anchor = settingsCtrlRects[(size_t)settingsOpenChoice];
            if (spec && spec->kind == SETTING_CHOICE) {
                float itemH = 30.0f * L.uiScale;
                float menuH = (float)spec->choices.size() * itemH + 10.0f * L.uiScale;
                float menuY = anchor.y + anchor.h + 4.0f;
                if (menuY + menuH > windowH) menuY = anchor.y - menuH - 4.0f;

                for (size_t i = 0; i < spec->choices.size(); ++i) {
                    float iy = menuY + 5.0f * L.uiScale + (float)i * itemH;
                    if (isInside(mx, my, anchor.x, iy, anchor.w, itemH)) {
                        settingSetChoice(*spec, (int)i);
                        settingsOpenChoice = -1;
                        commitSettingChange();
                        return;
                    }
                }
            }
            settingsOpenChoice = -1;
            return;
        }

        // Belt and braces: never walk past the shortest of the parallel arrays.
        size_t rowCount = std::min({ settingsFiltered.size(), settingsRowRects.size(),
                                     settingsCtrlRects.size(), settingsRevertRects.size() });
        for (size_t i = 0; i < rowCount; ++i) {
            const SettingSpec& spec = settingsSchema()[(size_t)settingsFiltered[i]];

            const UIRect& rev = settingsRevertRects[i];
            if (rev.w > 0.0f && isInside(mx, my, rev.x, rev.y, rev.w, rev.h)) {
                SilverConfig::get().resetToDefault(spec.path);
                settingsCursor = (int)i;
                commitSettingChange();
                return;
            }

            const UIRect& c = settingsCtrlRects[i];
            if (c.w <= 0.0f) continue;
            const UIRect& row = settingsRowRects[i];
            bool onRow = row.w > 0.0f && isInside(mx, my, row.x, row.y, row.w, row.h);
            if (!onRow) continue;

            settingsCursor = (int)i;

            if (!isInside(mx, my, c.x, c.y, c.w, c.h)) return;  // row selected, control untouched

            switch (spec.kind) {
                case SETTING_TOGGLE:
                    settingToggle(spec);
                    commitSettingChange();
                    return;

                case SETTING_STEPPER: {
                    float bw = 26.0f * L.uiScale;
                    if (mx < c.x + bw)             settingNudge(spec, -1);
                    else if (mx > c.x + c.w - bw)  settingNudge(spec, +1);
                    else return;
                    commitSettingChange();
                    return;
                }

                case SETTING_SLIDER: {
                    settingsDraggingSlider = true;
                    settingsDragRow = (int)i;
                    applySliderFromMouse(spec, c, mx);
                    commitSettingChange();
                    return;
                }

                case SETTING_CHOICE:
                    settingsOpenChoice = (settingsOpenChoice == (int)i) ? -1 : (int)i;
                    return;
            }
        }
    }

    void applySliderFromMouse(const SettingSpec& spec, const UIRect& track, float mx) {
        float t = std::clamp((mx - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
        float value = spec.min + t * (spec.max - spec.min);
        if (spec.step > 0.0f) value = std::round(value / spec.step) * spec.step;
        settingSetNumber(spec, value);
    }

    void handleMouseDrag(float mx, float my, TimelineManager& timeline, float windowW, float windowH,
                         const std::vector<GalleryRecord>& records) {
        if (settingsDraggingSlider && settingsDragRow >= 0 &&
            settingsDragRow < (int)settingsCtrlRects.size()) {
            if (const SettingSpec* spec = settingAtFiltered(settingsDragRow)) {
                applySliderFromMouse(*spec, settingsCtrlRects[(size_t)settingsDragRow], mx);
                applySettingsLive();   // live preview; the file is written on release
            }
            return;
        }

        if (isFullScreenView && isFsDragging) {
            fsPanX = fsOrigPanX + (mx - fsDragStartX);
            fsPanY = fsOrigPanY + (my - fsDragStartY);
            return;
        }

        if (isDraggingZoomSlider && zoomSliderTrackRect.h > 0.0f) {
            float gridW = gridWidth(windowW);
            bool hasBanner = (!activeFolderFilter.empty() && currentTab != TAB_FOLDERS);
            float ratio = 1.0f - std::clamp((my - zoomSliderTrackRect.y) / zoomSliderTrackRect.h, 0.0f, 1.0f);
            withViewAnchor(timeline, windowH, [&] {
                timeline.setZoomScale(ratio, gridW, records, hasBanner);
            });
            return;
        }

        if (isDraggingScrubber) {
            // Account for the handle's own length, or the content lags the
            // cursor by half a handle at the ends of the track.
            float visibleFrac = std::clamp(windowH / std::max(1.0f, timeline.totalContentHeight), 0.05f, 1.0f);
            float handleH = std::max(L.scrubHandleH, scrubberRect.h * visibleFrac);
            float usable = std::max(1.0f, scrubberRect.h - handleH);
            float ratio = std::clamp((my - scrubberRect.y - handleH * 0.5f) / usable, 0.0f, 1.0f);
            targetScrollY = timeline.getScrollForScrubRatio(ratio, windowH);
        }
    }

    void handleMouseUp() {
        if (settingsDraggingSlider) {
            settingsDraggingSlider = false;
            settingsDragRow = -1;
            commitSettingChange();
        }
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

        float topBarH = L.topBarH;
        float curGridAreaW = (windowW >= 750) ? ((float)windowW - sidebarAnim * L.sidebarWidth) : (float)windowW;

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
                font.addTextVCentered(timeline.sidePadding + 14.0f * L.uiScale, bcY, bcH, bcLabel, pal.textPrimary);
                font.render(windowW, windowH);

                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_ARROW_LEFT, breadcrumbClearRect.x + 8.0f, breadcrumbClearRect.y + 8.0f, 14.0f, 14.0f, pal.accent);
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                font.addTextVCentered(breadcrumbClearRect.x + 26.0f, breadcrumbClearRect.y, breadcrumbClearRect.h,
                                      "Back to Folders", pal.accent);
                font.render(windowW, windowH);
            }

            // Only the tiles on screen get queued, and only at the resolution the
            // current grid size actually needs. Scrolling away cancels the rest.
            //
            // Rebuilding the path list every frame meant ~120 string copies at
            // 60 fps for no benefit while the view is still. Skip the whole
            // thing unless the visible range or the tier actually changed.
            int thumbEdge = thumbs.quantizeEdge(timeline.itemSize);
            size_t visibleSig = (size_t)thumbEdge * 1000003u;
            visibleSig ^= timeline.flatVisibleItems.size() * 31u;
            if (!timeline.flatVisibleItems.empty()) {
                visibleSig ^= (size_t)timeline.flatVisibleItems.front()->flatIndex * 7919u;
                visibleSig ^= (size_t)timeline.flatVisibleItems.back()->flatIndex * 104729u;
            }

            if (visibleSig != lastVisibleSig) {
                lastVisibleSig = visibleSig;

                std::vector<std::string> visiblePaths;
                visiblePaths.reserve(timeline.flatVisibleItems.size());
                for (auto* itm : timeline.flatVisibleItems) {
                    if (!itm->isVisible) continue;
                    visiblePaths.push_back(itm->record.path);
                }
                thumbs.requestVisibleSet(visiblePaths, thumbEdge);
            }

            thumbs.lastVisibleTiles = (int)timeline.flatVisibleItems.size();
            thumbs.lastResolvedTiles = 0;

            // 1. Draw Section Headers. With day grouping a large library has
            // hundreds of sections, so seek to the first one on screen instead
            // of walking them all every frame.
            int firstSec = timeline.firstSectionAtOrAfter(scrollY);
            for (int si = std::max(0, firstSec - 1); si < (int)timeline.sections.size(); ++si) {
                const auto& sec = timeline.sections[si];
                float secY = sec.startY - scrollY;
                if (secY > windowH) break;
                if (secY + sec.height < topBarH) continue;

                font.beginBatch();
                float headTextY = secY + (timeline.headerHeight - font.textHeight()) * 0.5f;
                font.addText(timeline.sidePadding, headTextY, sec.title, pal.textPrimary);
                float titleW = font.measureText(sec.title);
                font.addText(timeline.sidePadding + titleW + 16.0f * L.uiScale, headTextY,
                             sec.subtitle, pal.textSecondary);
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
                float lift = itm->hoverAnim * L.hoverLift;
                rx -= lift * 0.5f;
                ry -= lift * 0.5f;
                rw += lift;
                rh += lift;

                // Tile Background / Animated Elevation Shadow
                font.beginBatch();
                float shadowAlpha = L.shadowAlpha + L.shadowHoverAlpha * itm->hoverAnim;
                font.addRoundedRect(rx + L.shadowOffset, ry + L.shadowOffset + lift, rw, rh, L.tileRadius, Color4(0, 0, 0, shadowAlpha));
                font.addRoundedRect(rx, ry, rw, rh, L.tileRadius, pal.cardBg);
                font.render(windowW, windowH);

                // Thumbnail Texture with Aspect-Fill UV Calculation
                auto it = thumbs.cache.find(itm->record.path);
                if (it != thumbs.cache.end() && it->second.ready && it->second.texId) {
                    it->second.lastUsedFrame = thumbs.frameCounter; // keep it resident
                    thumbs.lastResolvedTiles++;
                    font.beginBatch();
                    float pad = L.tileImagePad;
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

                    if (it->second.atlasSlot >= 0) {
                        float au0 = it->second.atlasU0;
                        float av0 = it->second.atlasV0;
                        float du = it->second.atlasU1 - au0;
                        float dv = it->second.atlasV1 - av0;
                        u0 = au0 + u0 * du;
                        u1 = au0 + u1 * du;
                        v0 = av0 + v0 * dv;
                        v1 = av0 + v1 * dv;
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

                    font.beginBatch();
                    font.addRoundedBorder(ix, iy, iw, ih,
                                          std::max(0.0f, L.tileRadius - pad),
                                          1.0f, imageOutline(pal));
                    font.render(windowW, windowH);
                } else if (GLuint prev = previewTextureFor(itm->record)) {
                    // Blurred 8x8 impression of the photo, upscaled. The grid
                    // paints something recognisable the instant it opens rather
                    // than a field of empty rectangles.
                    float pad = L.tileImagePad;
                    float ix = rx + pad, iy = ry + pad;
                    float iw = rw - pad * 2.0f, ih = rh - pad * 2.0f;

                    font.beginBatch();
                    UIVertex v[6] = {
                        { ix,      iy,      0.0f, 0.0f, 1, 1, 1, 1, 2.0f },
                        { ix + iw, iy,      1.0f, 0.0f, 1, 1, 1, 1, 2.0f },
                        { ix + iw, iy + ih, 1.0f, 1.0f, 1, 1, 1, 1, 2.0f },

                        { ix,      iy,      0.0f, 0.0f, 1, 1, 1, 1, 2.0f },
                        { ix + iw, iy + ih, 1.0f, 1.0f, 1, 1, 1, 1, 2.0f },
                        { ix,      iy + ih, 0.0f, 1.0f, 1, 1, 1, 1, 2.0f },
                    };
                    font.vertices.insert(font.vertices.end(), v, v + 6);
                    font.render(windowW, windowH, prev);
                } else {
                    // Nothing cached yet: breathing skeleton.
                    float pulse = silveranim::pulse((float)glfwGetTime());
                    font.beginBatch();
                    Color4 skelCol = pal.isDark ? Color4::Hex(0x1F222C, pulse * 0.9f) : Color4::Hex(0xE5E7EB, pulse * 0.9f);
                    font.addRoundedRect(rx + L.tileImagePad, ry + L.tileImagePad,
                                        rw - L.tileImagePad * 2.0f, rh - L.tileImagePad * 2.0f,
                                        L.tileRadius * 0.75f, skelCol);
                    font.render(windowW, windowH);
                }

                // Smooth Selected / Hover Accent Border
                float activeBorderAlpha = std::max(itm->selectAnim, itm->hoverAnim);
                if (activeBorderAlpha > 0.01f) {
                    font.beginBatch();
                    float bThickness = itm->selectAnim > 0.5f ? L.selBorderW : L.hoverBorderW;
                    font.addRoundedBorder(rx, ry, rw, rh, L.tileRadius, bThickness, Color4(pal.accent.r, pal.accent.g, pal.accent.b, activeBorderAlpha));
                    font.render(windowW, windowH);
                }

                // Filename overlay gradient on hover (smooth alpha fade)
                if (itm->hoverAnim > 0.01f) {
                    float capH = std::max(L.captionH, font.textHeight() + 12.0f);
                    float capY = ry + rh - capH;

                    font.beginBatch();
                    font.addRoundedRect(rx, capY, rw, capH, L.tileRadius, Color4(0, 0, 0, 0.72f * itm->hoverAnim));
                    font.render(windowW, windowH);

                    font.beginBatch();
                    float capPad = 8.0f * L.uiScale;
                    std::string fName = fitTextWithEllipsis(font, itm->record.filename, rw - capPad * 2.0f);
                    font.addTextVCentered(rx + capPad, capY, capH, fName,
                                          Color4(1.0f, 1.0f, 1.0f, itm->hoverAnim));
                    font.render(windowW, windowH);
                }

                // Favorite / Star Icon Badge (smooth alpha fade)
                float starAlpha = itm->record.starred ? 1.0f : itm->hoverAnim;
                if (starAlpha > 0.01f) {
                    float starSize = L.starSize;
                    float starX = rx + rw - starSize - L.starMargin;
                    float starY = ry + L.starMargin;

                    font.beginBatch();
                    // A flat black disc reads as a hole on a light theme; use the
                    // theme's own scrim so the badge sits on the palette.
                    Color4 idleBadge = pal.isDark ? Color4(0.0f, 0.0f, 0.0f, 0.55f * starAlpha)
                                                  : Color4(1.0f, 1.0f, 1.0f, 0.85f * starAlpha);
                    Color4 sBg = itm->record.starred ? Color4::Hex(0xEF4444, 0.95f * starAlpha) : idleBadge;
                    font.addRoundedRect(starX, starY, starSize, starSize, 13.0f, sBg);
                    font.render(windowW, windowH);

                    font.beginBatch();
                    float heartS = starSize * 0.62f;
                    float heartPad = (starSize - heartS) * 0.5f;
                    iconAtlas.drawIcon(font, itm->record.starred ? ICON_HEART_FILLED : ICON_HEART,
                                       starX + heartPad, starY + heartPad, heartS, heartS,
                                       itm->record.starred
                                           ? Color4(1.0f, 1.0f, 1.0f, starAlpha)
                                           : Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, starAlpha));
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
                float gapY = font.lineHeight() + 8.0f * L.uiScale;
                font.addText(cx - nw * 0.5f, cy, noFavTitle, pal.textPrimary);
                font.addText(cx - sw * 0.5f, cy + gapY, noFavSub, pal.textSecondary);
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // B. FOLDERS TAB VIEW (Responsive Auto-Scaling Symmetrical Grid)
        // -------------------------------------------------------------
        if (currentTab == TAB_FOLDERS) {
            if (!folderListValid) {
                folderList = db.getFolderStats();
                folderTree.build(folderList);
                folderListValid = true;
                // Re-resolve the current location against the rebuilt tree.
                browseChildren = folderTree.childrenOf(browsePath);
                breadcrumbNodes = folderTree.breadcrumb(browsePath);
                if (!browsePath.empty() && !folderTree.find(browsePath)) enterFolder("");
            }
            folderItemRects.clear();

            float sidePad = L.folderSidePad;
            float availW = curGridAreaW - sidePad * 2.0f;
            float fGap = L.folderGap;

            // Responsive card scaling based on zoom scale
            float zoomFactor = 0.8f + timeline.zoomScale * 0.6f; // 0.8 to 1.4
            float targetCardW = L.folderCardW * zoomFactor;
            int fCols = std::max(1, (int)((availW + fGap) / (targetCardW + fGap)));
            float fCardW = (availW - (fCols - 1) * fGap) / (float)fCols;
            float fCardH = L.folderCardH + (zoomFactor - 1.0f) * 16.0f;
            // Breadcrumb strip: where we are, and a way back up.
            float crumbH = font.textHeight() + 16.0f * L.uiScale;
            float crumbY = topBarH + 10.0f;
            breadcrumbRects.assign(breadcrumbNodes.size() + 1, UIRect());

            {
                float cx = sidePad;
                font.beginBatch();
                font.addRoundedRect(sidePad, crumbY, curGridAreaW - sidePad * 2.0f, crumbH,
                                    6.0f, pal.cardBg);
                font.render(windowW, windowH);

                auto crumb = [&](const std::string& label, int slot) {
                    float w = font.measureText(label) + 18.0f * L.uiScale;
                    UIRect r{ cx, crumbY, w, crumbH, false };
                    r.isHovered = isInside(mouseX, mouseY, cx, crumbY, w, crumbH);
                    if (slot < (int)breadcrumbRects.size()) breadcrumbRects[(size_t)slot] = r;

                    if (r.isHovered) {
                        font.beginBatch();
                        font.addRoundedRect(cx, crumbY + 2.0f, w, crumbH - 4.0f, 5.0f, pal.btnHover);
                        font.render(windowW, windowH);
                    }
                    font.beginBatch();
                    font.addTextCenteredIn(cx, crumbY, w, crumbH, label,
                                           r.isHovered ? pal.textPrimary : pal.textAccent);
                    font.render(windowW, windowH);
                    cx += w;

                    font.beginBatch();
                    font.addTextVCentered(cx, crumbY, crumbH, "/", pal.textSecondary);
                    font.render(windowW, windowH);
                    cx += font.measureText("/") + 4.0f * L.uiScale;
                };

                crumb("All Folders", 0);
                for (size_t i = 0; i < breadcrumbNodes.size(); ++i) {
                    const FolderNode& n = folderTree.nodes[(size_t)breadcrumbNodes[i]];
                    if (cx > curGridAreaW - 200.0f * L.uiScale) break;   // ran out of room
                    crumb(n.name, (int)i + 1);
                }

                // Everything under the current folder, in one view.
                if (!browsePath.empty()) {
                    const FolderNode* here = folderTree.find(browsePath);
                    int total = here ? here->totalCount : 0;
                    std::string label = "View all " + std::to_string(total) +
                                        (total == 1 ? " photo" : " photos");
                    float bw = font.measureText(label) + 26.0f * L.uiScale;
                    float bx = sidePad + curGridAreaW - sidePad * 2.0f - bw - 6.0f;
                    browseShowAllRect = { bx, crumbY + 3.0f, bw, crumbH - 6.0f, false };
                    browseShowAllRect.isHovered = isInside(mouseX, mouseY, bx, browseShowAllRect.y, bw, browseShowAllRect.h);

                    font.beginBatch();
                    font.addRoundedRect(bx, browseShowAllRect.y, bw, browseShowAllRect.h, 5.0f,
                                        browseShowAllRect.isHovered ? pal.accent : pal.btnBg);
                    font.render(windowW, windowH);
                    font.beginBatch();
                    font.addTextCenteredIn(bx, browseShowAllRect.y, bw, browseShowAllRect.h, label,
                                           browseShowAllRect.isHovered ? Color4(1, 1, 1, 1) : pal.textPrimary);
                    font.render(windowW, windowH);
                } else {
                    browseShowAllRect = UIRect();
                }
            }

            float fStartY = topBarH + crumbH + 24.0f - scrollY;

            int totalRows = (int)((browseChildren.size() + fCols - 1) / fCols);
            float totalFoldersH = fStartY + scrollY + totalRows * (fCardH + fGap) + 40.0f;
            timeline.totalContentHeight = std::max((float)windowH, totalFoldersH);

            for (size_t i = 0; i < browseChildren.size(); ++i) {
                const FolderNode& node = folderTree.nodes[(size_t)browseChildren[i]];
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
                font.addRoundedRect(fx + 2, fy + 2, fCardW, fCardH, L.folderCardRadius, Color4(0, 0, 0, 0.25f));
                font.addRoundedRect(fx, fy, fCardW, fCardH, L.folderCardRadius, fr.isHovered ? pal.btnHover : pal.cardBg);
                font.addRoundedBorder(fx, fy, fCardW, fCardH, L.folderCardRadius, L.folderCardBorderW, fr.isHovered ? pal.accent : pal.cardBorder);
                font.render(windowW, windowH);

                // Folder Icon
                font.beginBatch();
                float iconSize = L.folderIconSize;
                float iconY = fy + (fCardH - iconSize) * 0.5f;
                iconAtlas.drawIcon(font, ICON_FOLDER, fx + 16.0f, iconY, iconSize, iconSize, pal.accent);
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                std::string folderName = node.name;

                float textX = fx + L.folderIconSize + 26.0f * L.uiScale;
                float lineH = font.textHeight();
                float blockH = lineH * 2.0f + 6.0f * L.uiScale;
                float textY = fy + (fCardH - blockH) * 0.5f;

                std::string dispName = fitTextWithEllipsis(font, folderName, fx + fCardW - textX - 12.0f * L.uiScale);
                font.addText(textX, textY, dispName, pal.textPrimary);

                // Show the recursive total: a parent folder saying "0 photos"
                // because its own pictures live in subfolders is misleading.
                int shown = node.totalCount;
                std::string countStr = std::to_string(shown) + (shown == 1 ? " photo" : " photos");
                if (!node.children.empty()) {
                    countStr += "  -  " + std::to_string((int)node.children.size()) +
                                (node.children.size() == 1 ? " folder" : " folders");
                }
                font.addText(textX, textY + lineH + 6.0f * L.uiScale, countStr + "  ->", pal.accent);
                font.render(windowW, windowH);
            }

            if (browseChildren.empty()) {
                float cx = curGridAreaW * 0.5f;
                float cy = (windowH + topBarH) * 0.45f;
                font.beginBatch();
                std::string emptyTitle = "No Folders Found";
                std::string emptySub = "Scan your Pictures or Home directory to discover photo folders";
                float nw = font.measureText(emptyTitle);
                float sw = font.measureText(emptySub);
                float gapY = font.lineHeight() + 8.0f * L.uiScale;
                font.addText(cx - nw * 0.5f, cy, emptyTitle, pal.textPrimary);
                font.addText(cx - sw * 0.5f, cy + gapY, emptySub, pal.textSecondary);
                font.render(windowW, windowH);
            }
        }

        // -------------------------------------------------------------
        // C. RIGHT-SIDE FAST TIMELINE SCRUBBER
        // -------------------------------------------------------------
        // Shown whenever the content overflows. It used to be suppressed with the
        // sidebar open on windows under 1100px, which is the "sometimes it does
        // not show up at all" case - and it has its own gutter now, so there is
        // nothing for it to collide with.
        if (currentTab != TAB_FOLDERS && timeline.totalContentHeight > windowH) {
            float scH = windowH - topBarH - L.scrubBottomMargin;
            float scW = L.scrubW;
            // Sits in its own gutter, immediately right of the grid content.
            float scX = curGridAreaW - scrollbarGutter() + (scrollbarGutter() - scW) * 0.5f;
            float scY = topBarH + L.scrubTopMargin;

            scrubberRect.x = scX; scrubberRect.y = scY; scrubberRect.w = scW; scrubberRect.h = scH;
            scrubberRect.isHovered = isInside(mouseX, mouseY, scX - 6.0f, scY, scW + 12.0f, scH);

            // The track stays faintly visible so the scroll position is always
            // readable; only the emphasis fades.
            float emphasis = std::max(scrollbarAlpha,
                                      (scrubberRect.isHovered || isDraggingScrubber) ? 1.0f : 0.25f);

            float trackW = std::max(4.0f, scW * 0.45f);
            float trackX = scX + (scW - trackW) * 0.5f;

            font.beginBatch();
            font.addRoundedRect(trackX, scY, trackW, scH, trackW * 0.5f,
                                pal.isDark ? Color4(1, 1, 1, 0.07f * emphasis)
                                           : Color4(0, 0, 0, 0.07f * emphasis));

            // Handle length reflects how much of the library is on screen, the
            // way every scrollbar does - a fixed-height pill tells you nothing.
            float visibleFrac = std::clamp(windowH / std::max(1.0f, timeline.totalContentHeight), 0.05f, 1.0f);
            float handleH = std::max(L.scrubHandleH, scH * visibleFrac);
            float handleRatio = std::clamp(scrollY / std::max(1.0f, timeline.totalContentHeight - windowH), 0.0f, 1.0f);
            float handleY = scY + handleRatio * (scH - handleH);
            font.addRoundedRect(scX, handleY, scW, handleH, scW * 0.5f,
                                Color4(pal.accent.r, pal.accent.g, pal.accent.b, emphasis));
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
                font.addTextVCentered(bx + 12.0f * L.uiScale, by, bh, bText, pal.textAccent);
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
        font.addHorizontalEdgeShadow(topBarH, 0.0f, (float)windowW, 6.0f * L.uiScale,
                                     pal.isDark ? 0.28f : 0.12f, /*towardUp=*/false);
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

        const char* tabNamesFull[kGalleryTabCount] = { "All Photos", "Favorites", "Folders" };
        const char* tabNamesShort[kGalleryTabCount] = { "Photos", "Favs", "Folders" };

        // Size the tab group from the measured labels rather than a fixed width,
        // so a larger interface font cannot make them collide.
        float tabPadX = 22.0f * L.uiScale;
        auto groupWidthFor = [&](const char** names) {
            float total = 6.0f;
            for (int i = 0; i < kGalleryTabCount; ++i) {
                total += font.measureText(names[i]) + tabPadX * 2.0f;
            }
            return total;
        };

        const char** activeTabNames = tabNamesFull;
        float neededTabW = groupWidthFor(activeTabNames);
        if (neededTabW > availTabSpace - 16.0f) {
            activeTabNames = tabNamesShort;
            neededTabW = groupWidthFor(activeTabNames);
        }

        float tabH = std::min(topBarH - 12.0f, font.textHeight() + 18.0f * L.uiScale);
        float tabY = (topBarH - tabH) * 0.5f;
        float totalTabW = std::min(neededTabW, std::max(120.0f, availTabSpace - 16.0f));
        float tabX = leftBoundary + (availTabSpace - totalTabW) * 0.5f;

        font.beginBatch();
        font.addRoundedRect(tabX, tabY, totalTabW, tabH, 8.0f, pal.cardBg);
        font.addRoundedBorder(tabX, tabY, totalTabW, tabH, 8.0f, 1.0f, pal.cardBorder);
        font.render(windowW, windowH);

        // Pre-compute tab rects
        float curTabX = tabX + 3.0f;
        float tw = (totalTabW - 6.0f - 6.0f) / (float)kGalleryTabCount;
        for (int i = 0; i < kGalleryTabCount; ++i) {
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
        for (int i = 0; i < kGalleryTabCount; ++i) {
            bool isSel = (currentTab == (GalleryTab)i);

            if (!isSel && tabRects[i].isHovered) {
                font.beginBatch();
                font.addRoundedRect(tabRects[i].x, tabRects[i].y, tabRects[i].w, tabRects[i].h, 6.0f, pal.btnHover);
                font.render(windowW, windowH);
            }

            font.beginBatch();
            Color4 textCol = isSel ? Color4(1, 1, 1, 1) : (tabRects[i].isHovered ? pal.textPrimary : pal.textSecondary);
            font.addTextCenteredIn(tabRects[i].x, tabRects[i].y, tabRects[i].w, tabRects[i].h,
                                   activeTabNames[i], textCol);
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
                if (!dbAggregatesValid) {
                    cachedTotalCount = db.getTotalCount();
                    cachedTotalBytes = db.getTotalBytes();
                    dbAggregatesValid = true;
                }
                int total = cachedTotalCount;
                int64_t bytes = cachedTotalBytes;
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
                float ty = (float)windowH - th - L.statusPillBottomMargin;

                font.beginBatch();
                font.addRoundedRect(tx + 2, ty + 2, tw, th, 17.0f, Color4(0, 0, 0, 0.35f));
                font.addRoundedRect(tx, ty, tw, th, 17.0f, pal.toastBg);
                font.addRoundedBorder(tx, ty, tw, th, 17.0f, 1.0f, pal.toastBorder);
                font.render(windowW, windowH);

                font.beginBatch();
                font.addTextVCentered(tx + 18.0f, ty, th, statusText, pal.textAccent);
                font.render(windowW, windowH);
            }
        }

        // 2. Bottom Right Zoom Percentage Pill Button & Vertical Slider Popup
        {
            char pctBuf[48];
            snprintf(pctBuf, sizeof(pctBuf), "%s - %d%%", timeline.groupingName(), timeline.getZoomPercentage());
            float pillH = 32.0f;
            float pillW = std::max(96.0f, font.measureText(pctBuf) + 28.0f);
            float pillX = curGridAreaW - pillW - L.zoomPillRightMargin;
            float pillY = (float)windowH - pillH - L.zoomPillBottomMargin;
            zoomPillBtnRect.x = pillX;
            zoomPillBtnRect.y = pillY;
            zoomPillBtnRect.w = pillW;
            zoomPillBtnRect.h = pillH;
            zoomPillBtnRect.isHovered = isInside(mouseX, mouseY, pillX, pillY, pillW, pillH);

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
            font.addTextCenteredIn(pillX, pillY, pillW, pillH, pctBuf,
                                   (showZoomPopup || zoomPillBtnRect.isHovered) ? Color4(1, 1, 1, 1) : pal.textPrimary);
            font.render(windowW, windowH);

            // 3. Zoom Popup HUD / Menu & Vertical Slider (Smooth Slide-Up & Alpha Fade)
            if (zoomPopupAnim > 0.002f) {
                // The Day/Week/Month/Year chips are sized from measured text, so
                // the popup has to be at least as wide as they need. A fixed
                // quarter-width chip clipped "Month" and pushed its label out of
                // the pill once real font metrics replaced the ASCII bake.
                static const char* kGroupLabels[4] = { "Day", "Week", "Month", "Year" };
                const float popupPad = 10.0f * L.uiScale;
                const float headerH = std::max(38.0f * L.uiScale,
                                               font.textHeight() + 16.0f * L.uiScale);
                const float dividerGap = 7.0f * L.uiScale;
                const float chipGap = 5.0f * L.uiScale;
                const float chipPadX = 9.0f * L.uiScale;
                const float chipH = std::max(26.0f * L.uiScale,
                                             font.textHeight() + 10.0f * L.uiScale);
                const float itemH = std::max(40.0f, std::max(32.0f * L.uiScale,
                                             font.textHeight() + 14.0f * L.uiScale));
                const float itemGap = 3.0f * L.uiScale;
                const float listTop = popupPad + headerH + dividerGap;
                const float footerPadY = 9.0f * L.uiScale;
                float chipLabelW[4];
                float chipsNatural = 0.0f;
                for (int gi = 0; gi < 4; ++gi) {
                    chipLabelW[gi] = font.measureText(kGroupLabels[gi]);
                    chipsNatural += chipLabelW[gi] + chipPadX * 2.0f;
                }
                float chipsNeeded = chipsNatural + chipGap * 3.0f + popupPad * 2.0f;

                // Height must follow its contents. Previously only the card
                // height scaled with a small font while these rows stayed at
                // fixed pixel sizes, so Small and the grouping chips collided.
                float listH = itemH * 4.0f + itemGap * 3.0f;
                float contentNeededH = listTop + listH + dividerGap +
                                       footerPadY * 2.0f + chipH;

                float popW = std::max(L.zoomPopupW, chipsNeeded);
                float popH = std::max(L.zoomPopupH, contentNeededH);
                float popX = std::max(16.0f, pillX + pillW - popW);
                float popTargetY = pillY - popH - 10.0f;

                zoomPopupRect.x = popX;
                zoomPopupRect.y = popTargetY;
                zoomPopupRect.w = popW;
                zoomPopupRect.h = popH;

                float popupAlpha = zoomPopupAnim;

                // Collapse toward the pill it belongs to. A pure cross-fade
                // reads as the panel evaporating in place; shrinking it into its
                // own trigger reads as it being put away.
                float shrink = (1.0f - zoomPopupAnim) * 0.10f;
                float dw = popW * shrink;
                float dh = popH * shrink;
                popX += dw;                      // anchored bottom-right, at the pill
                popW -= dw;
                popH -= dh;
                float popY = popTargetY + dh + (1.0f - zoomPopupAnim) * 8.0f;

                // Background & Shadow
                font.beginBatch();
                font.addRoundedRect(popX + 3, popY + 4, popW, popH, 10.0f, Color4(0, 0, 0, 0.50f * popupAlpha));
                font.addRoundedRect(popX, popY, popW, popH, 10.0f, pal.isDark ? Color4::Hex(0x181A20, 0.98f * popupAlpha) : Color4::Hex(0xFFFFFF, 0.98f * popupAlpha));
                font.addRoundedBorder(popX, popY, popW, popH, 10.0f, 1.2f, pal.isDark ? Color4::Hex(0x2D323E, popupAlpha) : Color4::Hex(0xE0E3E8, popupAlpha));
                font.render(windowW, windowH);

                // FilePilot-style compact title bar: the component explains
                // itself and keeps the live zoom value visible while dragging.
                const float headerY = popY + popupPad;
                const float headerIcon = 17.0f * L.uiScale;
                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_GRID, popX + popupPad,
                                   headerY + (headerH - headerIcon) * 0.5f,
                                   headerIcon, headerIcon,
                                   fade(pal.textSecondary, popupAlpha));
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                char zoomValue[16];
                snprintf(zoomValue, sizeof(zoomValue), "%d%%", timeline.getZoomPercentage());
                font.beginBatch();
                font.addTextVCentered(popX + popupPad + 27.0f * L.uiScale,
                                      headerY, headerH, "View size",
                                      fade(pal.textPrimary, popupAlpha));
                font.addTextVCentered(popX + popW - popupPad - font.measureText(zoomValue),
                                      headerY, headerH, zoomValue,
                                      fade(pal.textSecondary, popupAlpha));
                const float dividerY = popY + popupPad + headerH;
                font.addRect(popX + popupPad, dividerY,
                             popW - popupPad * 2.0f, std::max(1.0f, L.uiScale),
                             pal.isDark ? Color4::Hex(0x303540, 0.75f * popupAlpha)
                                        : Color4::Hex(0xE4E7EC, 0.90f * popupAlpha));
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
                    { PRESET_LARGE, ICON_GRID_2X2, "Large", &presetLargeRect },
                    { PRESET_MEDIUM, ICON_GRID, "Medium", &presetMediumRect },
                    { PRESET_SMALL, ICON_GRID_4X4, "Small", &presetSmallRect }
                };

                const float sliderLaneW = 25.0f * L.uiScale;
                float itemX = popX + popupPad;
                float itemW = popW - popupPad * 2.0f - sliderLaneW;
                float curItemY = popY + listTop;

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
                        // Everything inside the popup has to fade with it -
                        // using the raw accent left active rows fully opaque
                        // while the card behind them dissolved.
                        Color4 accentFaded(pal.accent.r, pal.accent.g, pal.accent.b, pal.accent.a * popupAlpha);
                        Color4 bgCol = isActive ? accentFaded
                                                : (pal.isDark ? Color4::Hex(0x252830, 0.90f * popupAlpha)
                                                              : Color4::Hex(0xEEF0F4, 0.90f * popupAlpha));
                        font.addRoundedRect(itemX, curItemY, itemW, itemH, 6.0f, bgCol);
                        font.render(windowW, windowH);
                    }

                    // Icon
                    Color4 iconCol = isActive ? Color4(1, 1, 1, popupAlpha)
                                              : fade(pi.rect->isHovered ? pal.textPrimary : pal.textSecondary, popupAlpha);
                    font.beginBatch();
                    float presetIcon = 18.0f * L.uiScale;
                    iconAtlas.drawIcon(font, pi.icon, itemX + 8.0f * L.uiScale,
                                       curItemY + (itemH - presetIcon) * 0.5f,
                                       presetIcon, presetIcon, iconCol);
                    font.render(windowW, windowH, 0, iconAtlas.textureId);

                    // Text
                    Color4 textCol = isActive ? Color4(1, 1, 1, popupAlpha)
                                              : fade(pal.textPrimary, popupAlpha);
                    font.beginBatch();
                    font.addTextVCentered(itemX + 32.0f * L.uiScale, curItemY, itemH, pi.label, textCol);
                    font.render(windowW, windowH);

                    curItemY += itemH + itemGap;
                }

                // Vertical Slider (Right column)
                float laneX = popX + popW - popupPad - sliderLaneW + 5.0f * L.uiScale;
                float trackX = laneX + (sliderLaneW - 10.0f * L.uiScale) * 0.5f;
                float trackY = popY + listTop;
                float trackW = 6.0f * L.uiScale;
                float trackH = listH;

                zoomSliderTrackRect.x = trackX - 8.0f * L.uiScale;
                zoomSliderTrackRect.y = trackY;
                zoomSliderTrackRect.w = 22.0f * L.uiScale;
                zoomSliderTrackRect.h = trackH;

                float thumbY = trackY + (1.0f - timeline.zoomScale) * trackH;
                float thumbW = 12.0f * L.uiScale;
                float thumbH = 22.0f * L.uiScale;
                float thumbX = trackX + (trackW - thumbW) * 0.5f;

                zoomSliderThumbRect.x = thumbX;
                zoomSliderThumbRect.y = thumbY - thumbH * 0.5f;
                zoomSliderThumbRect.w = thumbW;
                zoomSliderThumbRect.h = thumbH;

                font.beginBatch();
                // A dedicated inset lane prevents the handle from looking as
                // if it floats over the menu commands.
                font.addRoundedRect(laneX, trackY, sliderLaneW,
                                    trackH, 7.0f * L.uiScale,
                                    pal.isDark ? Color4::Hex(0x14171D, 0.62f * popupAlpha)
                                               : Color4::Hex(0xF2F4F7, 0.90f * popupAlpha));
                // Rail background
                Color4 railBg = pal.isDark ? Color4::Hex(0x2A2E38, popupAlpha) : Color4::Hex(0xDFE2E8, popupAlpha);
                font.addRoundedRect(trackX, trackY, trackW, trackH, 3.0f, railBg);

                // Blue track fill from bottom up to thumbY
                float fillBottom = trackY + trackH;
                if (fillBottom > thumbY) {
                    font.addRoundedRect(trackX, thumbY, trackW, fillBottom - thumbY, 3.0f, Color4(pal.accent.r, pal.accent.g, pal.accent.b, popupAlpha));
                }

                // Thumb pill handle
                font.addRoundedRect(thumbX, thumbY - thumbH * 0.5f, thumbW, thumbH, 6.0f, fade(pal.accent, popupAlpha));
                font.addRoundedBorder(thumbX, thumbY - thumbH * 0.5f, thumbW, thumbH, 6.0f, 1.2f, Color4(1, 1, 1, 0.40f * popupAlpha));
                font.render(windowW, windowH);

                // Timeline granularity chips - the same Day / Week / Month / Year
                // steps a pinch gesture walks through on a phone gallery.
                float chipY = popY + popH - chipH - footerPadY;

                // Visually separate grouping (layout) from sizing (zoom).
                font.beginBatch();
                font.addRect(popX + popupPad, chipY - footerPadY,
                             popW - popupPad * 2.0f, std::max(1.0f, L.uiScale),
                             pal.isDark ? Color4::Hex(0x303540, 0.75f * popupAlpha)
                                        : Color4::Hex(0xE4E7EC, 0.90f * popupAlpha));
                font.render(windowW, windowH);

                // Give every chip its natural width, then share any slack evenly
                // so the row still fills the popup.
                float chipsAvail = popW - popupPad * 2.0f - chipGap * 3.0f;
                float chipExtra = std::max(0.0f, chipsAvail - chipsNatural) * 0.25f;

                float chipX = popX + popupPad;
                for (int gi = 0; gi < 4; ++gi) {
                    float chipW = chipLabelW[gi] + chipPadX * 2.0f + chipExtra;
                    groupRects[gi].x = chipX;
                    groupRects[gi].y = chipY - (popY - popTargetY); // hit-test at the settled position
                    groupRects[gi].w = chipW;
                    groupRects[gi].h = chipH;
                    groupRects[gi].isHovered = isInside(mouseX, mouseY, groupRects[gi].x, groupRects[gi].y, chipW, chipH);

                    bool isActiveGroup = ((int)timeline.grouping == gi);

                    font.beginBatch();
                    Color4 chipAccent(pal.accent.r, pal.accent.g, pal.accent.b, pal.accent.a * popupAlpha);
                    Color4 chipBg = isActiveGroup ? chipAccent
                                  : (groupRects[gi].isHovered ? (pal.isDark ? Color4::Hex(0x252830, 0.95f * popupAlpha) : Color4::Hex(0xEEF0F4, 0.95f * popupAlpha))
                                                             : (pal.isDark ? Color4::Hex(0x1D2029, 0.85f * popupAlpha) : Color4::Hex(0xF4F6F9, 0.90f * popupAlpha)));
                    font.addRoundedRect(chipX, chipY, chipW, chipH, 6.0f, chipBg);
                    font.render(windowW, windowH);


                    font.beginBatch();
                    Color4 chipText = isActiveGroup ? Color4(1, 1, 1, popupAlpha)
                                                    : fade(pal.textSecondary, popupAlpha);
                    font.addTextCenteredIn(chipX, chipY, chipW, chipH, kGroupLabels[gi], chipText);
                    font.render(windowW, windowH);

                    chipX += chipW + chipGap;
                }
            } else {
                for (int gi = 0; gi < 4; ++gi) groupRects[gi] = UIRect();
            }
        }

        // -------------------------------------------------------------
        // F.9 SEARCH BAR
        // -------------------------------------------------------------
        if (searchActive || !searchQuery.empty()) {
            float barH = font.textHeight() + 20.0f * L.uiScale;
            float barW = std::min(560.0f * L.uiScale, curGridAreaW - 48.0f);
            float barX = (curGridAreaW - barW) * 0.5f;
            float barY = topBarH + 10.0f;

            searchBarRect = { barX, barY, barW, barH, false };

            font.beginBatch();
            font.addRoundedRect(barX + 2, barY + 3, barW, barH, 8.0f, Color4(0, 0, 0, 0.28f));
            font.addRoundedRect(barX, barY, barW, barH, 8.0f, pal.cardBg);
            font.addRoundedBorder(barX, barY, barW, barH, 8.0f, 1.4f,
                                  searchActive ? pal.accent : pal.cardBorder);
            font.render(windowW, windowH);

            float icon = font.textHeight();
            font.beginBatch();
            iconAtlas.drawIcon(font, ICON_SEARCH, barX + 12.0f * L.uiScale,
                               barY + (barH - icon) * 0.5f, icon, icon, pal.textSecondary);
            font.render(windowW, windowH, 0, iconAtlas.textureId);

            float textX = barX + 12.0f * L.uiScale + icon + 10.0f * L.uiScale;
            std::string shown = searchQuery.empty() ? "Search photos, folders and dates..." : searchQuery;
            Color4 col = searchQuery.empty()
                ? Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, 0.75f)
                : pal.textPrimary;

            font.beginBatch();
            font.addTextVCentered(textX, barY, barH,
                                  fitTextWithEllipsis(font, shown, barW - (textX - barX) - 70.0f * L.uiScale),
                                  col);
            font.render(windowW, windowH);

            // A caret, so an empty focused field still looks like an input.
            if (searchActive) {
                float caretX = textX + (searchQuery.empty() ? 0.0f : font.measureText(searchQuery)) + 2.0f;
                float blink = (std::fmod((float)glfwGetTime(), 1.0f) < 0.5f) ? 1.0f : 0.0f;
                if (blink > 0.0f) {
                    font.beginBatch();
                    font.addRect(caretX, barY + (barH - icon) * 0.5f, 1.5f * L.uiScale, icon, pal.textPrimary);
                    font.render(windowW, windowH);
                }
            }

            if (!searchQuery.empty()) {
                float cs = font.textHeight();
                searchClearRect = { barX + barW - cs - 14.0f * L.uiScale,
                                    barY + (barH - cs) * 0.5f, cs, cs, false };
                searchClearRect.isHovered = isInside(mouseX, mouseY, searchClearRect.x, searchClearRect.y, cs, cs);
                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_CLOSE, searchClearRect.x, searchClearRect.y, cs, cs,
                                   searchClearRect.isHovered ? pal.textPrimary : pal.textSecondary);
                font.render(windowW, windowH, 0, iconAtlas.textureId);
            } else {
                searchClearRect = UIRect();
            }
        } else {
            searchBarRect = UIRect();
            searchClearRect = UIRect();
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
            font.addTextVCentered(tx + 18.0f, ty, th, themeToastText, Color4(pal.textAccent.r, pal.textAccent.g, pal.textAccent.b, toastAlpha));
            font.render(windowW, windowH);
        }

        // -------------------------------------------------------------
        // G.5 THEME MENU POPOVER (Top Z-Index Floating Layer)
        // -------------------------------------------------------------
        if (themeMenuAnim > 0.002f) {
            float tmW = 160.0f;
            float tmH = 118.0f;
            float tmX = std::clamp(themeBtnRect.x + themeBtnRect.w - tmW, 12.0f, (float)windowW - tmW - 14.0f);

            themeMenuRect.x = tmX;
            themeMenuRect.y = topBarH + 6.0f;
            themeMenuRect.w = tmW;
            themeMenuRect.h = tmH;

            float tmAlpha = themeMenuAnim;

            // Collapse upward into the theme button.
            float tmShrink = (1.0f - themeMenuAnim) * 0.10f;
            float tmDW = tmW * tmShrink;
            float tmDH = tmH * tmShrink;
            tmX += tmDW;
            tmW -= tmDW;
            tmH -= tmDH;
            float tmY = topBarH + 6.0f - (1.0f - themeMenuAnim) * 8.0f;

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
                    Color4 bgCol = isActive ? fade(pal.accent, tmAlpha)
                                            : (pal.isDark ? Color4::Hex(0x282B34, 0.85f * tmAlpha)
                                                          : Color4::Hex(0xEEF0F4, 0.85f * tmAlpha));
                    font.addRoundedRect(optX, optY, optW, optH, 6.0f, bgCol);
                    font.render(windowW, windowH);
                }

                Color4 iconCol = isActive ? Color4(1, 1, 1, tmAlpha)
                                          : fade(to.rect->isHovered ? pal.textPrimary : pal.textSecondary, tmAlpha);
                font.beginBatch();
                iconAtlas.drawIcon(font, to.icon, optX + 8.0f, optY + 7.0f, 18.0f, 18.0f, iconCol);
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                Color4 textCol = isActive ? Color4(1, 1, 1, tmAlpha) : fade(pal.textPrimary, tmAlpha);
                font.beginBatch();
                font.addTextVCentered(optX + 32.0f * L.uiScale, optY, optH, to.label, textCol);
                font.render(windowW, windowH);

                optY += optH + 3.0f;
            }
        }

        // -------------------------------------------------------------
        // G.8 SETTINGS PALETTE
        // -------------------------------------------------------------
        if (settingsAnim > 0.002f) {
            renderSettings(windowW, windowH, mouseX, mouseY, font, iconAtlas);
        }

        // -------------------------------------------------------------
        // G.9 KEYBOARD SHORTCUTS OVERLAY
        // -------------------------------------------------------------
        if (shortcutsAnim > 0.002f) {
            renderShortcuts(windowW, windowH, font);
        }

        // -------------------------------------------------------------
        // H. FULLSCREEN IN-GALLERY VIEW OVERLAY (Animated Transition)
        // -------------------------------------------------------------
        if (fsAnim > 0.002f) {
            renderFullScreenView(windowW, windowH, mouseX, mouseY, records, font, iconAtlas, fsAnim);
        }
    }

    // -----------------------------------------------------------------
    // SETTINGS PALETTE
    // -----------------------------------------------------------------
    // Panel geometry, scaled with the interface font (see applyConfig).
    float settingsRowH = 54.0f;
    float settingsCardW = 760.0f;
    float settingsCardH = 620.0f;
    float settingsSearchH = 52.0f;
    float settingsFooterH = 38.0f;
    float settingsCtrlW = 200.0f;

    void rebuildSettingsFilter() {
        std::string q = settingsQuery;
        for (char& c : q) c = (char)tolower((unsigned char)c);

        settingsFiltered.clear();
        const auto& schema = settingsSchema();
        for (size_t i = 0; i < schema.size(); ++i) {
            if (settingMatches(schema[i], q)) settingsFiltered.push_back((int)i);
        }

        // Keep the hit-test rectangles in lockstep with the filter. They are
        // filled in during render, but a click can arrive first (the filter
        // changes on a keystroke), and the click handler indexes them by the
        // same i as settingsFiltered.
        settingsRowRects.assign(settingsFiltered.size(), UIRect());
        settingsCtrlRects.assign(settingsFiltered.size(), UIRect());
        settingsRevertRects.assign(settingsFiltered.size(), UIRect());
        if (settingsCursor >= (int)settingsFiltered.size()) {
            settingsCursor = (int)settingsFiltered.size() - 1;
        }
        if (settingsCursor < 0) settingsCursor = 0;
        settingsOpenChoice = -1;
    }

    void openSettings() {
        showSettings = true;
        settingsQuery.clear();
        settingsCursor = 0;
        settingsScroll = 0.0f;
        settingsOpenChoice = -1;
        rebuildSettingsFilter();
    }

    const SettingSpec* settingAtFiltered(int filteredIndex) const {
        if (filteredIndex < 0 || filteredIndex >= (int)settingsFiltered.size()) return nullptr;
        return &settingsSchema()[(size_t)settingsFiltered[(size_t)filteredIndex]];
    }

    // Invoked after any settings edit. main() hooks this up to re-apply the
    // timeline metrics and reflow the grid, which applyConfig() alone cannot do
    // because the UI does not own the timeline.
    std::function<void()> onConfigChanged;

    // Apply a settings edit everywhere, then persist it.
    //
    // save() deliberately records its own mtime so the hot-reload poll does not
    // re-read the file we just wrote - which means nothing else will apply the
    // change for us, and this has to do it all.
    void applySettingsLive() {
        applyConfig();
        if (onConfigChanged) onConfigChanged();
    }

    void commitSettingChange() {
        applySettingsLive();
        SilverConfig::get().save();
    }

    // Geometry the panel and its input handlers must agree on.
    float settingsListHeight(float windowH) const {
        float cardH = std::min(settingsCardH, windowH - 80.0f);
        return cardH - settingsSearchH - settingsFooterH;
    }

    float settingsMaxScroll(float windowH) const {
        return std::max(0.0f, settingsFiltered.size() * settingsRowH - settingsListHeight(windowH));
    }

    void moveSettingsCursor(int delta, float listH) {
        if (settingsFiltered.empty()) return;
        settingsCursor = std::clamp(settingsCursor + delta, 0, (int)settingsFiltered.size() - 1);

        // Keep the cursor inside the scrolled viewport.
        float rowTop = settingsCursor * settingsRowH;
        float rowBottom = rowTop + settingsRowH;
        if (rowTop < settingsScroll) settingsScroll = rowTop;
        else if (rowBottom > settingsScroll + listH) settingsScroll = rowBottom - listH;

        float maxScroll = std::max(0.0f, settingsFiltered.size() * settingsRowH - listH);
        settingsScroll = std::clamp(settingsScroll, 0.0f, maxScroll);
    }

    void renderSettings(int windowW, int windowH, float mouseX, float mouseY,
                        FontRenderer& font, IconAtlas& iconAtlas) {
        const ThemePalette& pal = theme.current;
        float a = settingsAnim;

        float cardW = std::min(settingsCardW, (float)windowW - 80.0f);
        float cardH = std::min(settingsCardH, (float)windowH - 80.0f);
        float cardX = ((float)windowW - cardW) * 0.5f;
        float cardY = ((float)windowH - cardH) * 0.5f + (1.0f - a) * 12.0f;

        float searchH = settingsSearchH;
        float footerH = settingsFooterH;
        float listY = cardY + searchH;
        float listH = cardH - searchH - footerH;

        // Backdrop + card
        font.beginBatch();
        font.addRect(0, 0, (float)windowW, (float)windowH, Color4(0, 0, 0, 0.55f * a));
        font.render(windowW, windowH);

        font.beginBatch();
        font.addRoundedRect(cardX + 3, cardY + 6, cardW, cardH, 12.0f, Color4(0, 0, 0, 0.5f * a));
        font.addRoundedRect(cardX, cardY, cardW, cardH, 12.0f,
                            pal.isDark ? Color4::Hex(0x16181E, 0.99f * a) : Color4::Hex(0xFFFFFF, 0.99f * a));
        font.addRoundedBorder(cardX, cardY, cardW, cardH, 12.0f, 1.2f,
                              pal.isDark ? Color4::Hex(0x2D323E, a) : Color4::Hex(0xE0E3E8, a));
        font.addRect(cardX, cardY + searchH - 1.0f, cardW, 1.0f,
                     pal.isDark ? Color4::Hex(0x2D323E, a) : Color4::Hex(0xE6E9EE, a));
        font.render(windowW, windowH);

        // Search row
        settingsSearchRect = { cardX, cardY, cardW - 44.0f, searchH, false };
        font.beginBatch();
        float searchIcon = 18.0f * L.uiScale;
        iconAtlas.drawIcon(font, ICON_SEARCH, cardX + 18.0f * L.uiScale,
                           cardY + (searchH - searchIcon) * 0.5f, searchIcon, searchIcon,
                           Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, a));
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        font.beginBatch();
        bool empty = settingsQuery.empty();
        std::string shown = empty ? "Select an option..." : settingsQuery;
        Color4 queryCol = empty ? Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, 0.75f * a)
                                : Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, a);
        font.addTextVCentered(cardX + 46.0f * L.uiScale, cardY, searchH, shown, queryCol);
        font.render(windowW, windowH);

        float closeS = 26.0f * L.uiScale;
        settingsCloseRect = { cardX + cardW - closeS - 14.0f * L.uiScale, cardY + (searchH - closeS) * 0.5f, closeS, closeS, false };
        settingsCloseRect.isHovered = isInside(mouseX, mouseY, settingsCloseRect.x, settingsCloseRect.y, closeS, closeS);
        if (settingsCloseRect.isHovered) {
            font.beginBatch();
            font.addRoundedRect(settingsCloseRect.x, settingsCloseRect.y, closeS, closeS, 6.0f, pal.btnHover);
            font.render(windowW, windowH);
        }
        font.beginBatch();
        float closeIcon = closeS * 0.55f;
        iconAtlas.drawIcon(font, ICON_CLOSE, settingsCloseRect.x + (closeS - closeIcon) * 0.5f,
                           settingsCloseRect.y + (closeS - closeIcon) * 0.5f, closeIcon, closeIcon,
                           Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, a));
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        // Rows
        settingsRowRects.assign(settingsFiltered.size(), UIRect());
        settingsCtrlRects.assign(settingsFiltered.size(), UIRect());
        settingsRevertRects.assign(settingsFiltered.size(), UIRect());

        float ctrlW = settingsCtrlW;
        float ctrlX = cardX + cardW - ctrlW - 22.0f * L.uiScale;

        // Mask the list so scrolled rows cannot paint over the search field or
        // the footer hint bar.
        font.pushClip(cardX, listY, cardW, listH);

        for (size_t i = 0; i < settingsFiltered.size(); ++i) {
            const SettingSpec& spec = settingsSchema()[(size_t)settingsFiltered[i]];
            float rowY = listY + (float)i * settingsRowH - settingsScroll;
            if (rowY + settingsRowH < listY - 4.0f || rowY > listY + listH + 4.0f) continue;

            UIRect row{ cardX + 8.0f, rowY, cardW - 16.0f, settingsRowH - 4.0f, false };
            row.isHovered = isInside(mouseX, mouseY, row.x, row.y, row.w, row.h) &&
                            mouseY >= listY && mouseY <= listY + listH;
            settingsRowRects[i] = row;

            bool isCursor = ((int)i == settingsCursor);
            if (isCursor || row.isHovered) {
                font.beginBatch();
                Color4 bg = isCursor
                    ? (pal.isDark ? Color4::Hex(0x1E2430, 0.95f * a) : Color4::Hex(0xEEF3FB, 0.9f * a))
                    : Color4(pal.btnHover.r, pal.btnHover.g, pal.btnHover.b, 0.5f * a);
                font.addRoundedRect(row.x, row.y, row.w, row.h, 7.0f, bg);
                if (isCursor) {
                    font.addRoundedBorder(row.x, row.y, row.w, row.h, 7.0f, 1.4f,
                                          Color4(pal.accent.r, pal.accent.g, pal.accent.b, a));
                }
                font.render(windowW, windowH);
            }

            // Stack label over description using real text metrics, so the two
            // lines cannot collide at any font size.
            float lineH = font.textHeight();
            float blockH = lineH * 2.0f + 6.0f;
            float textTop = rowY + (settingsRowH - 4.0f - blockH) * 0.5f;
            float padL = 14.0f * L.uiScale;

            font.beginBatch();
            font.addText(row.x + padL, textTop, spec.label,
                         Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, a));
            std::string desc = fitTextWithEllipsis(font, spec.description,
                                                   ctrlX - row.x - padL - 16.0f);
            font.addText(row.x + padL, textTop + lineH + 6.0f, desc,
                         Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, 0.9f * a));
            font.render(windowW, windowH);

            // Revert affordance, only when the value differs from the default.
            if (settingModified(spec)) {
                float rs = 20.0f;
                UIRect rev{ ctrlX - rs - 12.0f, rowY + (settingsRowH - 4.0f - rs) * 0.5f, rs, rs, false };
                rev.isHovered = isInside(mouseX, mouseY, rev.x, rev.y, rs, rs);
                settingsRevertRects[i] = rev;
                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_REFRESH, rev.x, rev.y, rs, rs,
                                   Color4(pal.accent.r, pal.accent.g, pal.accent.b, (rev.isHovered ? 1.0f : 0.65f) * a));
                font.render(windowW, windowH, 0, iconAtlas.textureId);
            }

            float cy = rowY + (settingsRowH - 4.0f) * 0.5f;   // controls stay centred
            renderSettingControl(spec, i, ctrlX, cy, ctrlW, mouseX, mouseY, windowW, windowH, font, iconAtlas, a);
        }

        font.popClip();

        // The dropdown floats above the rows and is deliberately not clipped.
        renderSettingsDropdown(windowW, windowH, mouseX, mouseY, font, iconAtlas);

        // Footer hint bar
        float footY = cardY + cardH - footerH;
        font.beginBatch();
        font.addRect(cardX, footY, cardW, 1.0f,
                     pal.isDark ? Color4::Hex(0x2D323E, a) : Color4::Hex(0xE6E9EE, a));
        font.render(windowW, windowH);

        struct Hint { const char* key; const char* what; };
        static const Hint hints[] = {
            { "Up/Down", "to navigate" }, { "Left/Right", "to adjust" },
            { "Enter", "to use" }, { "Escape", "to dismiss" }
        };
        float hx = cardX + 20.0f * L.uiScale;
        float chipH = font.textHeight() + 6.0f * L.uiScale;
        float chipY = footY + (footerH - chipH) * 0.5f;
        for (const Hint& h : hints) {
            float kw = font.measureText(h.key) + 14.0f * L.uiScale;
            font.beginBatch();
            font.addRoundedRect(hx, chipY, kw, chipH, 4.0f,
                                pal.isDark ? Color4::Hex(0x252A34, a) : Color4::Hex(0xEDEFF3, a));
            font.render(windowW, windowH);
            font.beginBatch();
            font.addTextCenteredIn(hx, chipY, kw, chipH, h.key,
                                   Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, a));
            font.render(windowW, windowH);
            hx += kw + 6.0f * L.uiScale;

            font.beginBatch();
            font.addTextVCentered(hx, footY, footerH, h.what,
                                  Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, 0.8f * a));
            font.render(windowW, windowH);
            hx += font.measureText(h.what) + 18.0f * L.uiScale;
        }
    }

    void renderSettingControl(const SettingSpec& spec, size_t index,
                              float x, float centerY, float w,
                              float mouseX, float mouseY, int windowW, int windowH,
                              FontRenderer& font, IconAtlas& iconAtlas, float a) {
        const ThemePalette& pal = theme.current;
        const float k = L.uiScale;

        switch (spec.kind) {
            case SETTING_TOGGLE: {
                float tw = 44.0f * k, th = 24.0f * k;
                UIRect c{ x + w - tw, centerY - th * 0.5f, tw, th, false };
                c.isHovered = isInside(mouseX, mouseY, c.x, c.y, tw, th);
                settingsCtrlRects[index] = c;

                bool on = settingFlag(spec);
                font.beginBatch();
                Color4 track = on ? Color4(pal.accent.r, pal.accent.g, pal.accent.b, a)
                                  : (pal.isDark ? Color4::Hex(0x3A404C, a) : Color4::Hex(0xCBD2DA, a));
                font.addRoundedRect(c.x, c.y, tw, th, th * 0.5f, track);
                float inset = 3.0f * k;
                float knob = th - inset * 2.0f;
                float kx = on ? (c.x + tw - knob - inset) : (c.x + inset);
                font.addRoundedRect(kx, c.y + inset, knob, knob, knob * 0.5f, Color4(1, 1, 1, a));
                font.render(windowW, windowH);
                break;
            }

            case SETTING_STEPPER: {
                float bh = 26.0f * k, bw = 26.0f * k, fieldW = 96.0f * k;
                float totalW = bw * 2.0f + fieldW;
                UIRect c{ x + w - totalW, centerY - bh * 0.5f, totalW, bh, false };
                settingsCtrlRects[index] = c;

                font.beginBatch();
                Color4 fieldBg = pal.isDark ? Color4::Hex(0x22262F, a) : Color4::Hex(0xF1F3F7, a);
                font.addRoundedRect(c.x, c.y, totalW, bh, 6.0f, fieldBg);
                font.render(windowW, windowH);

                font.beginBatch();
                float chev = 14.0f * k;
                float chevY = c.y + (bh - chev) * 0.5f;
                iconAtlas.drawIcon(font, ICON_CHEVRON_LEFT, c.x + (bw - chev) * 0.5f, chevY, chev, chev,
                                   Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, a));
                iconAtlas.drawIcon(font, ICON_CHEVRON_RIGHT, c.x + totalW - bw + (bw - chev) * 0.5f, chevY, chev, chev,
                                   Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, a));
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                font.addTextCenteredIn(c.x + bw, c.y, fieldW, bh, settingDisplayValue(spec),
                                       Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, a));
                font.render(windowW, windowH);
                break;
            }

            case SETTING_SLIDER: {
                float sh = 6.0f * k, sw = w - 60.0f * k;
                UIRect c{ x + w - sw, centerY - 11.0f * k, sw, 22.0f * k, false };
                settingsCtrlRects[index] = c;

                float span = std::max(0.0001f, spec.max - spec.min);
                float t = std::clamp((settingNumber(spec) - spec.min) / span, 0.0f, 1.0f);

                font.beginBatch();
                font.addRoundedRect(c.x, centerY - sh * 0.5f, sw, sh, sh * 0.5f,
                                    pal.isDark ? Color4::Hex(0x3A404C, a) : Color4::Hex(0xD7DDE5, a));
                font.addRoundedRect(c.x, centerY - sh * 0.5f, sw * t, sh, sh * 0.5f,
                                    Color4(pal.accent.r, pal.accent.g, pal.accent.b, a));
                float knob = 16.0f * k;
                font.addRoundedRect(c.x + sw * t - knob * 0.5f, centerY - knob * 0.5f, knob, knob, knob * 0.5f,
                                    Color4(1, 1, 1, a));
                font.render(windowW, windowH);
                break;
            }

            case SETTING_CHOICE: {
                float bw2 = 170.0f * k, bh2 = 28.0f * k;
                UIRect c{ x + w - bw2, centerY - bh2 * 0.5f, bw2, bh2, false };
                c.isHovered = isInside(mouseX, mouseY, c.x, c.y, bw2, bh2);
                settingsCtrlRects[index] = c;

                bool open = (settingsOpenChoice == (int)index);
                font.beginBatch();
                Color4 bg = (open || c.isHovered) ? Color4(pal.accent.r, pal.accent.g, pal.accent.b, a)
                                                  : (pal.isDark ? Color4::Hex(0x22262F, a) : Color4::Hex(0xF1F3F7, a));
                font.addRoundedRect(c.x, c.y, bw2, bh2, 6.0f, bg);
                font.render(windowW, windowH);

                Color4 fg = (open || c.isHovered) ? Color4(1, 1, 1, a)
                                                  : Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, a);
                font.beginBatch();
                font.addTextCenteredIn(c.x, c.y, bw2 - 18.0f * k, bh2, settingDisplayValue(spec), fg);
                font.render(windowW, windowH);

                font.beginBatch();
                float dchev = 14.0f * k;
                iconAtlas.drawIcon(font, open ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN,
                                   c.x + bw2 - dchev - 8.0f * k, c.y + (bh2 - dchev) * 0.5f, dchev, dchev, fg);
                font.render(windowW, windowH, 0, iconAtlas.textureId);
                break;
            }
        }
    }

    // The expanded dropdown is drawn after every row so it floats above them.
    void renderSettingsDropdown(int windowW, int windowH, float mouseX, float mouseY,
                                FontRenderer& font, IconAtlas& iconAtlas) {
        if (settingsOpenChoice < 0 || settingsOpenChoice >= (int)settingsCtrlRects.size()) return;
        const SettingSpec* spec = settingAtFiltered(settingsOpenChoice);
        if (!spec || spec->kind != SETTING_CHOICE) return;

        const ThemePalette& pal = theme.current;
        const UIRect& anchor = settingsCtrlRects[(size_t)settingsOpenChoice];
        float itemH = 30.0f * L.uiScale;
        float menuH = (float)spec->choices.size() * itemH + 10.0f * L.uiScale;
        float menuY = anchor.y + anchor.h + 4.0f;
        if (menuY + menuH > (float)windowH) menuY = anchor.y - menuH - 4.0f;

        font.beginBatch();
        font.addRoundedRect(anchor.x + 2, menuY + 3, anchor.w, menuH, 8.0f, Color4(0, 0, 0, 0.45f));
        font.addRoundedRect(anchor.x, menuY, anchor.w, menuH, 8.0f,
                            pal.isDark ? Color4::Hex(0x1D2129, 1.0f) : Color4::Hex(0xFFFFFF, 1.0f));
        font.addRoundedBorder(anchor.x, menuY, anchor.w, menuH, 8.0f, 1.0f,
                              pal.isDark ? Color4::Hex(0x333A47, 1.0f) : Color4::Hex(0xDDE1E8, 1.0f));
        font.render(windowW, windowH);

        int current = settingChoiceIndex(*spec);
        for (size_t i = 0; i < spec->choices.size(); ++i) {
            float iy = menuY + 5.0f * L.uiScale + (float)i * itemH;
            bool hov = isInside(mouseX, mouseY, anchor.x, iy, anchor.w, itemH);
            bool sel = ((int)i == current);

            if (hov || sel) {
                font.beginBatch();
                Color4 bg = hov ? Color4(pal.accent.r, pal.accent.g, pal.accent.b, 1.0f)
                                : (pal.isDark ? Color4::Hex(0x252A34, 1.0f) : Color4::Hex(0xEDF1F7, 1.0f));
                font.addRoundedRect(anchor.x + 4.0f, iy, anchor.w - 8.0f, itemH, 5.0f, bg);
                font.render(windowW, windowH);
            }

            Color4 fg = hov ? Color4(1, 1, 1, 1) : (sel ? pal.textPrimary : pal.textSecondary);
            if (sel) {
                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_CHECK, anchor.x + 10.0f, iy + 8.0f, 14.0f, 14.0f, fg);
                font.render(windowW, windowH, 0, iconAtlas.textureId);
            }
            font.beginBatch();
            font.addTextVCentered(anchor.x + 32.0f, iy, itemH, spec->choices[i].label, fg);
            font.render(windowW, windowH);
        }
    }

    // -----------------------------------------------------------------
    // RENDER: KEYBOARD SHORTCUTS OVERLAY
    // -----------------------------------------------------------------
    void renderShortcuts(int windowW, int windowH, FontRenderer& font) {
        const ThemePalette& pal = theme.current;
        float a = shortcutsAnim;

        struct Row { const char* keys; const char* what; };
        static const Row rows[] = {
            { "Arrow keys",      "Move between photos" },
            { "Page Up / Down",  "Jump three rows" },
            { "Home / End",      "First / last photo" },
            { "Enter or F",      "Open full screen" },
            { "Space",           "Star the selected photo" },
            { "Escape",          "Close view, sidebar, or filter" },
            { "Ctrl + 1 / 2 / 3","Switch tab" },
            { "Tab",             "Next tab" },
            { "Ctrl + scroll",   "Zoom the grid" },
            { "+ / -",           "Zoom in / out" },
            { "Ctrl + 0",        "Reset zoom to automatic" },
            { "T",               "Cycle theme" },
            { "F5 or Ctrl + R",  "Rescan folders" },
            { "Ctrl + F or /",   "Search photos" },
            { "Ctrl + ,",        "Open settings" },
            { "F1 or ?",         "Show this help" },
        };
        const int rowCount = (int)(sizeof(rows) / sizeof(rows[0]));

        // Sized from the widest key label and the real line height, so the panel
        // fits its content at any font size instead of clipping it.
        float rowH = font.lineHeight() + 8.0f * L.uiScale;
        float padX = 28.0f * L.uiScale;
        float padTop = font.textHeight() + 40.0f * L.uiScale;

        float keyColW = 0.0f;
        float whatColW = 0.0f;
        for (const Row& r : rows) {
            keyColW = std::max(keyColW, font.measureText(r.keys));
            whatColW = std::max(whatColW, font.measureText(r.what));
        }
        keyColW += 24.0f * L.uiScale;

        float cardW = padX * 2.0f + keyColW + whatColW;
        float cardH = padTop + rowCount * rowH + font.lineHeight() + 20.0f * L.uiScale;
        float cardX = ((float)windowW - cardW) * 0.5f;
        float cardY = ((float)windowH - cardH) * 0.5f + (1.0f - a) * 10.0f;

        // Dim the app behind the panel.
        font.beginBatch();
        font.addRect(0, 0, (float)windowW, (float)windowH, Color4(0, 0, 0, 0.55f * a));
        font.render(windowW, windowH);

        font.beginBatch();
        font.addRoundedRect(cardX + 3, cardY + 5, cardW, cardH, 12.0f, Color4(0, 0, 0, 0.5f * a));
        font.addRoundedRect(cardX, cardY, cardW, cardH, 12.0f,
                            pal.isDark ? Color4::Hex(0x181A20, 0.99f * a) : Color4::Hex(0xFFFFFF, 0.99f * a));
        font.addRoundedBorder(cardX, cardY, cardW, cardH, 12.0f, 1.2f,
                              pal.isDark ? Color4::Hex(0x2D323E, a) : Color4::Hex(0xE0E3E8, a));
        font.render(windowW, windowH);

        font.beginBatch();
        font.addText(cardX + padX, cardY + (padTop - font.textHeight()) * 0.5f, "Keyboard Shortcuts",
                     Color4(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b, a));
        font.render(windowW, windowH);

        float rowY = cardY + padTop;
        for (int i = 0; i < rowCount; ++i) {
            font.beginBatch();
            font.addText(cardX + padX, rowY, rows[i].keys,
                         Color4(pal.textAccent.r, pal.textAccent.g, pal.textAccent.b, a));
            font.addText(cardX + padX + keyColW, rowY, rows[i].what,
                         Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, a));
            font.render(windowW, windowH);
            rowY += rowH;
        }

        font.beginBatch();
        std::string hint = "Press Escape or F1 to close";
        float hw = font.measureText(hint);
        font.addText(cardX + (cardW - hw) * 0.5f, cardY + cardH - font.lineHeight() - 8.0f * L.uiScale, hint,
                     Color4(pal.textSecondary.r, pal.textSecondary.g, pal.textSecondary.b, 0.8f * a));
        font.render(windowW, windowH);
    }

    // -----------------------------------------------------------------
    // RENDER: FILEPILOT RIGHT SIDEBAR PREVIEW COLUMN
    // -----------------------------------------------------------------
    void renderSidebar(int windowW, int windowH, float mouseX, float mouseY,
                       FontRenderer& font, IconAtlas& iconAtlas) {
        const ThemePalette& pal = theme.current;
        float sbSlideOffset = (1.0f - sidebarAnim) * L.sidebarWidth;
        float sbX = (float)windowW - L.sidebarWidth + sbSlideOffset;
        float topBarH = L.topBarH;
        float sbH = (float)windowH - topBarH;

        // Background & Shadow with smooth slide and fade
        font.beginBatch();
        font.addVerticalEdgeShadow(sbX, topBarH, sbH, 10.0f * L.uiScale,
                                   (pal.isDark ? 0.30f : 0.14f) * sidebarAnim,
                                   /*towardLeft=*/true);
        font.addRect(sbX, topBarH, L.sidebarWidth, sbH, pal.cardBg);
        font.addRect(sbX, topBarH, L.sbBorderW, sbH, pal.cardBorder);
        font.render(windowW, windowH);

        float curY = topBarH + 14.0f;
        float innerX = sbX + L.sidebarInnerPad;
        float innerW = L.sidebarWidth - L.sidebarInnerPad * 2.0f;

        // 1. Header: Title & Polished Close Button
        float btnCloseSize = 28.0f;
        sbCloseRect.x = sbX + L.sidebarWidth - btnCloseSize - L.sidebarInnerPad;
        sbCloseRect.y = curY;
        sbCloseRect.w = btnCloseSize;
        sbCloseRect.h = btnCloseSize;
        sbCloseRect.isHovered = isInside(mouseX, mouseY, sbCloseRect.x, sbCloseRect.y, btnCloseSize, btnCloseSize);

        font.beginBatch();
        font.addTextVCentered(innerX, curY, btnCloseSize, "Preview & Info", pal.textPrimary);
        // Styled rounded close button pill
        font.addRoundedRect(sbCloseRect.x, sbCloseRect.y, btnCloseSize, btnCloseSize, 6.0f, sbCloseRect.isHovered ? pal.btnHover : pal.btnBg);
        font.addRoundedBorder(sbCloseRect.x, sbCloseRect.y, btnCloseSize, btnCloseSize, 6.0f, 1.0f, sbCloseRect.isHovered ? pal.accent : pal.btnBorder);
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_CLOSE, sbCloseRect.x + 7.0f, sbCloseRect.y + 7.0f, 14.0f, 14.0f, sbCloseRect.isHovered ? pal.textPrimary : pal.textSecondary);
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        curY += 38.0f;

        // 2. Large Image Preview Box (Aspect-Fit with Recessed Matte Frame)
        float prevH = L.sbPreviewH;
        sbPreviewRect.x = innerX;
        sbPreviewRect.y = curY;
        sbPreviewRect.w = innerW;
        sbPreviewRect.h = prevH;
        sbPreviewRect.isHovered = isInside(mouseX, mouseY, innerX, curY, innerW, prevH);

        Color4 previewMatteBg = pal.isDark ? Color4::Hex(0x0C0D10, 1.0f) : Color4::Hex(0xEAEEF3, 1.0f);
        font.beginBatch();
        font.addRoundedRect(innerX, curY, innerW, prevH, L.sbPreviewRadius, previewMatteBg);
        font.addRoundedBorder(innerX, curY, innerW, prevH, L.sbPreviewRadius, L.sbBorderW, sbPreviewRect.isHovered ? pal.accent : pal.cardBorder);
        font.render(windowW, windowH);

        // Draw High-Res / Thumbnail Preview
        GLuint drawTexId = highResPreview.id ? highResPreview.id : 0;
        float drawU0 = 0.0f, drawV0 = 0.0f, drawU1 = 1.0f, drawV1 = 1.0f;
        int imgW = highResPreview.width > 0 ? highResPreview.width : selectedRecord.width;
        int imgH = highResPreview.height > 0 ? highResPreview.height : selectedRecord.height;

        if (!drawTexId) {
            auto it = thumbs.cache.find(selectedPath);
            if (it != thumbs.cache.end() && it->second.ready) {
                drawTexId = it->second.texId;
                if (it->second.atlasSlot >= 0) {
                    drawU0 = it->second.atlasU0; drawV0 = it->second.atlasV0;
                    drawU1 = it->second.atlasU1; drawV1 = it->second.atlasV1;
                }
                if (imgW <= 0) imgW = it->second.width;
                if (imgH <= 0) imgH = it->second.height;
            }
        }

        // A fast navigation can beat both GPU thumbnail upload and the full-res
        // decoder. The database preview is always safe to show for the selected
        // record and avoids a blank canvas containing only navigation arrows.
        if (!drawTexId && selectedRecord.hasPreview) {
            drawTexId = previewTextureFor(selectedRecord);
            if (imgW <= 0) imgW = GalleryRecord::kPreviewDim;
            if (imgH <= 0) imgH = GalleryRecord::kPreviewDim;
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
                { px, py, drawU0, drawV0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px + finalW, py, drawU1, drawV0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px + finalW, py + finalH, drawU1, drawV1, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },

                { px, py, drawU0, drawV0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px + finalW, py + finalH, drawU1, drawV1, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
                { px, py + finalH, drawU0, drawV1, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f },
            };
            font.vertices.insert(font.vertices.end(), v, v + 6);
            font.render(windowW, windowH, drawTexId);

            font.beginBatch();
            font.addRoundedBorder(px, py, finalW, finalH,
                                  std::max(0.0f, L.sbPreviewRadius - 6.0f),
                                  1.0f, imageOutline(pal));
            font.render(windowW, windowH);

            // Hover overlay "Click to Fullscreen"
            if (sbPreviewRect.isHovered) {
                // A full-width bar along the bottom overhangs the letterboxed
                // image and cuts the preview in half. Dim the whole preview and
                // centre a pill on it instead - the pattern every modern gallery
                // uses, and it cannot overflow because it is sized from its own
                // contents.
                font.beginBatch();
                font.addRoundedRect(innerX, curY, innerW, prevH, L.sbPreviewRadius,
                                    Color4(0.0f, 0.0f, 0.0f, 0.42f));
                font.render(windowW, windowH);

                const char* hoverLabel = "Click for Fullscreen";
                float iconS = std::round(font.textHeight());
                float gap = 8.0f * L.uiScale;
                float padX = 16.0f * L.uiScale;
                float padY = 10.0f * L.uiScale;

                float labelW = font.measureText(hoverLabel);
                float pillW = std::min(innerW - 16.0f * L.uiScale,
                                       iconS + gap + labelW + padX * 2.0f);
                float pillH = font.textHeight() + padY * 2.0f;
                float pillX = innerX + (innerW - pillW) * 0.5f;
                float pillY = curY + (prevH - pillH) * 0.5f;

                font.beginBatch();
                font.addRoundedRect(pillX, pillY, pillW, pillH, pillH * 0.5f,
                                    Color4(0.0f, 0.0f, 0.0f, 0.72f));
                font.addRoundedBorder(pillX, pillY, pillW, pillH, pillH * 0.5f, 1.0f,
                                      Color4(1.0f, 1.0f, 1.0f, 0.22f));
                font.render(windowW, windowH);

                float contentW = iconS + gap + labelW;
                float cx = pillX + (pillW - contentW) * 0.5f;

                font.beginBatch();
                iconAtlas.drawIcon(font, ICON_FIT, cx, pillY + (pillH - iconS) * 0.5f,
                                   iconS, iconS, Color4(1, 1, 1, 1));
                font.render(windowW, windowH, 0, iconAtlas.textureId);

                font.beginBatch();
                font.addTextVCentered(cx + iconS + gap, pillY, pillH, hoverLabel, Color4(1, 1, 1, 1));
                font.render(windowW, windowH);
            }
        }

        curY += prevH + L.sbSectionSpacing;

        // 3. Action Buttons Row: [Star] [Fullscreen] [Viewer]
        float btnH = L.sbButtonH;
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

        // Icon + label as one centred group, sized from the actual text. The old
        // fixed +8/+28 offsets were unscaled, so a larger font pushed "Starred"
        // straight out of its button and into the next one.
        float actIcon = std::min(btnH - 10.0f, 16.0f * L.uiScale);
        float actGap = 6.0f * L.uiScale;

        auto drawActionButton = [&](const UIRect& r, IconType icon, const std::string& label,
                                    Color4 fg) {
            float maxText = r.w - actIcon - actGap - 12.0f * L.uiScale;
            std::string shown = fitTextWithEllipsis(font, label, std::max(0.0f, maxText));
            float textW = font.measureText(shown);
            float groupW = actIcon + actGap + textW;
            float gx = r.x + std::max(6.0f * L.uiScale, (r.w - groupW) * 0.5f);

            font.beginBatch();
            iconAtlas.drawIcon(font, icon, gx, r.y + (btnH - actIcon) * 0.5f, actIcon, actIcon, fg);
            font.render(windowW, windowH, 0, iconAtlas.textureId);

            font.beginBatch();
            font.addTextVCentered(gx + actIcon + actGap, r.y, btnH, shown, fg);
            font.render(windowW, windowH);
        };

        drawActionButton(sbFavBtnRect,
                         selectedRecord.starred ? ICON_HEART_FILLED : ICON_HEART,
                         selectedRecord.starred ? "Starred" : "Star",
                         selectedRecord.starred ? Color4(1, 1, 1, 1) : pal.textPrimary);
        drawActionButton(sbFullscreenBtnRect, ICON_FIT, "View", pal.textPrimary);
        drawActionButton(sbViewerBtnRect, ICON_EXTERNAL_LINK, "Open", pal.textPrimary);

        curY += btnH + L.sbSectionSpacing;

        // 4. FilePilot Rich Metadata Cards
        auto renderMetaSection = [&](const std::string& secTitle, const std::vector<std::pair<std::string, std::string>>& entries) {
            font.beginBatch();
            font.addText(innerX, curY, secTitle, pal.textAccent);
            font.render(windowW, windowH);
            curY += 22.0f;

            float blockH = (float)entries.size() * L.sbMetaRowH + 12.0f;
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
                rowY += L.sbMetaRowH;
            }
            curY += blockH + L.sbSectionSpacing;
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

        // Section C: Location - full, untruncated path across as many lines as needed
        {
            font.beginBatch();
            font.addText(innerX, curY, "LOCATION", pal.textAccent);
            font.render(windowW, windowH);
            curY += 22.0f;

            std::vector<std::string> pathLines = wrapPath(font, selectedRecord.path, innerW - 20.0f, L.sbPathMaxLines);
            float lineH = L.sbPathLineH;
            float blockH = (float)pathLines.size() * lineH + 16.0f;

            font.beginBatch();
            font.addRoundedRect(innerX, curY, innerW, blockH, 6.0f, pal.cardBg);
            font.addRoundedBorder(innerX, curY, innerW, blockH, 6.0f, 1.0f, pal.cardBorder);
            font.render(windowW, windowH);

            float rowY = curY + 8.0f;
            for (const auto& line : pathLines) {
                font.beginBatch();
                font.addText(innerX + 10.0f, rowY, line, pal.textPrimary);
                font.render(windowW, windowH);
                rowY += lineH;
            }
            curY += blockH + L.sbSectionSpacing;
        }

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
        font.addTextVCentered(innerX + 34.0f, curY, 32.0f, "Copy Full File Path", pal.accent);
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
        // Never draw the previous full-resolution texture with the newly
        // selected record's geometry.  That mismatch caused a very visible
        // grow/shrink pulse while paging quickly.  Prefer the new thumbnail
        // until its matching full-resolution image is ready.
        bool highResMatches = highResPreview.id && highResPreview.meta.filePath == selectedPath;
        GLuint drawTexId = highResMatches ? highResPreview.id : 0;
        float drawU0 = 0.0f, drawV0 = 0.0f, drawU1 = 1.0f, drawV1 = 1.0f;
        int imgW = highResMatches ? highResPreview.width : selectedRecord.width;
        int imgH = highResMatches ? highResPreview.height : selectedRecord.height;

        if (!drawTexId) {
            auto it = thumbs.cache.find(selectedPath);
            if (it != thumbs.cache.end() && it->second.ready) {
                drawTexId = it->second.texId;
                if (it->second.atlasSlot >= 0) {
                    drawU0 = it->second.atlasU0; drawV0 = it->second.atlasV0;
                    drawU1 = it->second.atlasU1; drawV1 = it->second.atlasV1;
                }
                if (imgW <= 0) imgW = it->second.width;
                if (imgH <= 0) imgH = it->second.height;
            }
        }

        if (!drawTexId && selectedRecord.hasPreview) {
            drawTexId = previewTextureFor(selectedRecord);
            if (imgW <= 0) imgW = GalleryRecord::kPreviewDim;
            if (imgH <= 0) imgH = GalleryRecord::kPreviewDim;
        }

        float topBarH = L.fsTopBarH;
        float viewH = (float)windowH - topBarH;

        if (drawTexId && imgW > 0 && imgH > 0) {
            float aspect = (float)imgW / (float)imgH;
            float fitW = (float)windowW - L.fsImageMargin;
            float fitH = viewH - L.fsImageMargin;
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

            // Keep the settled image bounds on physical pixels. This avoids a
            // half-pixel softness at fractional desktop scales while the image
            // texture itself continues to use smooth filtered sampling.
            if (anim >= 0.999f && !isFsDragging) {
                float right = font.snapToPixel(ix + scaleW);
                float bottom = font.snapToPixel(iy + scaleH);
                ix = font.snapToPixel(ix);
                iy = font.snapToPixel(iy);
                scaleW = right - ix;
                scaleH = bottom - iy;
            }

            font.beginBatch();
            UIVertex v[6] = {
                { ix, iy, drawU0, drawV0, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix + scaleW, iy, drawU1, drawV0, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix + scaleW, iy + scaleH, drawU1, drawV1, 1.0f, 1.0f, 1.0f, anim, 2.0f },

                { ix, iy, drawU0, drawV0, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix + scaleW, iy + scaleH, drawU1, drawV1, 1.0f, 1.0f, 1.0f, anim, 2.0f },
                { ix, iy + scaleH, drawU0, drawV1, 1.0f, 1.0f, 1.0f, anim, 2.0f },
            };
            font.vertices.insert(font.vertices.end(), v, v + 6);
            font.render(windowW, windowH, drawTexId);

            font.beginBatch();
            font.addBorder(ix, iy, scaleW, scaleH, 1.0f,
                           imageOutline(pal, anim));
            font.render(windowW, windowH);
        }

        // 3. Navigation Arrows (< and >)
        float arrowSize = L.fsArrowSize;
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
                            fade(fsBackBtnRect.isHovered ? pal.btnHover : pal.cardBg, anim));
        font.addRoundedBorder(fsBackBtnRect.x, fsBackBtnRect.y, fsBackBtnRect.w, fsBackBtnRect.h, 18.0f, 1.0f,
                              fade(pal.cardBorder, anim));
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, ICON_ARROW_LEFT, fsBackBtnRect.x + 10.0f, fsBackBtnRect.y + 9.0f, 18.0f, 18.0f,
                           fade(pal.accent, anim));
        font.render(windowW, windowH, 0, iconAtlas.textureId);

        font.beginBatch();
        font.addTextVCentered(fsBackBtnRect.x + 34.0f, fsBackBtnRect.y, fsBackBtnRect.h, "Back", fade(pal.accent, anim));
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
        float btnS = L.fsButtonSize;
        float fsRx = (float)windowW - (btnS * 3.0f + 8.0f * 2.0f + 16.0f);

        fsFavBtnRect.x = fsRx; fsFavBtnRect.y = 14.0f; fsFavBtnRect.w = btnS; fsFavBtnRect.h = btnS;
        fsViewerBtnRect.x = fsRx + btnS + 8.0f; fsViewerBtnRect.y = 14.0f; fsViewerBtnRect.w = btnS; fsViewerBtnRect.h = btnS;
        fsCloseBtnRect.x = fsRx + (btnS + 8.0f) * 2.0f; fsCloseBtnRect.y = 14.0f; fsCloseBtnRect.w = btnS; fsCloseBtnRect.h = btnS;

        fsFavBtnRect.isHovered = isInside(mouseX, mouseY, fsFavBtnRect.x, fsFavBtnRect.y, btnS, btnS);
        fsViewerBtnRect.isHovered = isInside(mouseX, mouseY, fsViewerBtnRect.x, fsViewerBtnRect.y, btnS, btnS);
        fsCloseBtnRect.isHovered = isInside(mouseX, mouseY, fsCloseBtnRect.x, fsCloseBtnRect.y, btnS, btnS);

        font.beginBatch();
        Color4 fsFavBg = fade(selectedRecord.starred ? Color4::Hex(0xEF4444, 0.9f)
                                                     : (fsFavBtnRect.isHovered ? pal.btnHover : pal.cardBg), anim);
        font.addRoundedRect(fsFavBtnRect.x, fsFavBtnRect.y, btnS, btnS, 8.0f, fsFavBg);
        if (fsViewerBtnRect.isHovered) font.addRoundedRect(fsViewerBtnRect.x, fsViewerBtnRect.y, btnS, btnS, 8.0f, fade(pal.btnHover, anim));
        if (fsCloseBtnRect.isHovered) font.addRoundedRect(fsCloseBtnRect.x, fsCloseBtnRect.y, btnS, btnS, 8.0f, fade(pal.btnHover, anim));
        font.render(windowW, windowH);

        font.beginBatch();
        iconAtlas.drawIcon(font, selectedRecord.starred ? ICON_HEART_FILLED : ICON_HEART,
                           fsFavBtnRect.x + 8.0f, fsFavBtnRect.y + 8.0f, 20.0f, 20.0f,
                           selectedRecord.starred ? Color4(1, 1, 1, anim) : fade(pal.textPrimary, anim));
        iconAtlas.drawIcon(font, ICON_EXTERNAL_LINK,
                           fsViewerBtnRect.x + 8.0f, fsViewerBtnRect.y + 8.0f, 20.0f, 20.0f,
                           fade(pal.textPrimary, anim));
        iconAtlas.drawIcon(font, ICON_CLOSE,
                           fsCloseBtnRect.x + 8.0f, fsCloseBtnRect.y + 8.0f, 20.0f, 20.0f,
                           fade(pal.textSecondary, anim));
        font.render(windowW, windowH, 0, iconAtlas.textureId);
    }
};
