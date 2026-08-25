#pragma once

#include "db.h"
#include "silver_config.h"
#include "silver_platform.h"
#include "silver_anim.h"
#include <vector>
#include <string>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <cstdint>

struct TimelineItem {
    GalleryRecord record;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float animX = 0.0f;
    float animY = 0.0f;
    float animW = 0.0f;
    float animH = 0.0f;
    int sectionIndex = 0;
    int itemIndexInSection = 0;
    int flatIndex = 0;
    bool isVisible = false;      // inside the drawn viewport
    bool inAnimBand = false;     // close enough to the viewport to animate
    bool isHovered = false;
    bool isSelected = false;
    float hoverAnim = 0.0f;
    float selectAnim = 0.0f;

    // Spring state for the second-order motion solver.
    silveranim::RectVelocity motionVel;
    float hoverVel = 0.0f;
    float selectVel = 0.0f;
};

struct TimelineSection {
    std::string title;        // "Today", "12 - 18 August 2026", "August 2026", "2025"
    std::string subtitle;     // "48 photos - 240 MB"
    int64_t groupKey = 0;
    int year = 1970;
    int month = 1;
    int day = 1;
    int64_t captureTime = 0;
    float startY = 0.0f;
    float height = 0.0f;
    std::vector<TimelineItem> items;
};

enum GridPreset {
    PRESET_CUSTOM = 0,
    PRESET_XL,       // Extra Large (~450px)
    PRESET_LARGE,    // Large (~300px)
    PRESET_MEDIUM,   // Medium (~190px)
    PRESET_SMALL     // Small (~110px)
};

// Mobile-style timeline granularity. Zooming out widens the buckets exactly the
// way phone galleries switch between day, week, month and year views.
enum TimelineGrouping {
    GROUP_DAY = 0,
    GROUP_WEEK,
    GROUP_MONTH,
    GROUP_YEAR
};

class TimelineManager {
public:
    std::vector<TimelineSection> sections;
    std::vector<TimelineItem*> flatVisibleItems;
    std::vector<TimelineItem*> flatAllItems;
    float totalContentHeight = 0.0f;
    int totalPhotoCount = 0;
    std::string selectedPath = "";
    int selectedFlatIndex = -1;

    int columns = 5;
    float zoomScale = 0.45f; // 0.05f to 1.0f
    bool isAutoZoom = true;
    GridPreset currentPreset = PRESET_MEDIUM;
    TimelineGrouping grouping = GROUP_DAY;

    float zoomPillTimer = 0.0f;
    std::string zoomPillText = "";
    bool hasActiveFolderBanner = false;

    // Layout metrics - all sourced from config/silver.json (see applyConfig).
    float gridGap = 10.0f;
    float sidePadding = 24.0f;
    float headerHeight = 46.0f;
    float topOffset = 70.0f; // Below top bar
    float bottomPadding = 60.0f;
    float sectionSpacing = 20.0f;
    float sectionBottomPadding = 16.0f;
    float folderBannerHeight = 56.0f;
    float minItemSize = 70.0f;
    float maxItemSize = 440.0f;
    float tileMinSize = 40.0f;
    int   maxColumns = 24;

    float zoomMin = 0.05f;
    float zoomMax = 1.0f;
    float groupThreshold[3] = { 0.30f, 0.20f, 0.12f };   // day / week / month cut-offs
    float groupAnchor[4] = { 0.55f, 0.25f, 0.16f, 0.08f };
    float presetScale[4] = { 1.0f, 0.70f, 0.45f, 0.20f }; // XL / Large / Medium / Small

    // Pull every layout and zoom constant out of the JSON config.
    void applyConfig() {
        const SilverConfig& c = SilverConfig::get();
        // Spacing and header metrics follow the interface font size; tile sizes
        // do not, because those are governed by the zoom control.
        const float k = silverUiScale();

        gridGap              = c.num("grid.gap", 10.0f);
        sidePadding          = c.num("grid.sidePadding", 24.0f);
        headerHeight         = c.num("grid.sectionHeaderHeight", 46.0f);
        sectionSpacing       = c.num("grid.sectionSpacing", 20.0f);
        sectionBottomPadding = c.num("grid.sectionBottomPadding", 16.0f);
        minItemSize          = c.num("grid.minItemSize", 70.0f);
        maxItemSize          = c.num("grid.maxItemSize", 440.0f);
        tileMinSize          = c.num("grid.tileMinSize", 40.0f);
        maxColumns           = std::max(1, c.integer("grid.maxColumns", 24));

        topOffset            = c.num("layout.contentTopOffset", 70.0f);
        bottomPadding        = c.num("layout.bottomPadding", 60.0f);
        folderBannerHeight   = c.num("layout.folderBannerHeight", 56.0f);

        zoomMin              = c.num("zoom.minScale", 0.05f);
        zoomMax              = c.num("zoom.maxScale", 1.0f);
        groupThreshold[0]    = c.num("zoom.groupingThresholdDay", 0.30f);
        groupThreshold[1]    = c.num("zoom.groupingThresholdWeek", 0.20f);
        groupThreshold[2]    = c.num("zoom.groupingThresholdMonth", 0.12f);
        groupAnchor[0]       = c.num("zoom.groupingAnchorDay", 0.55f);
        groupAnchor[1]       = c.num("zoom.groupingAnchorWeek", 0.25f);
        groupAnchor[2]       = c.num("zoom.groupingAnchorMonth", 0.16f);
        groupAnchor[3]       = c.num("zoom.groupingAnchorYear", 0.08f);
        presetScale[0]       = c.num("zoom.presetXL", 1.00f);
        presetScale[1]       = c.num("zoom.presetLarge", 0.70f);
        presetScale[2]       = c.num("zoom.presetMedium", 0.45f);
        presetScale[3]       = c.num("zoom.presetSmall", 0.20f);

        if (k != 1.0f) {
            gridGap              *= k;
            sidePadding          *= k;
            headerHeight         *= k;
            sectionSpacing       *= k;
            sectionBottomPadding *= k;
            topOffset            *= k;
            bottomPadding        *= k;
            folderBannerHeight   *= k;
        }
    }

    // Cached layout inputs so a pure re-layout never needs the record list again.
    float lastGridAreaW = 1200.0f;
    bool lastHasBanner = false;
    float itemSize = 190.0f;

    // -------------------------------------------------------------------------
    // Grouping helpers
    // -------------------------------------------------------------------------
    TimelineGrouping groupingForZoom(float zoom) const {
        if (zoom >= groupThreshold[0]) return GROUP_DAY;
        if (zoom >= groupThreshold[1]) return GROUP_WEEK;
        if (zoom >= groupThreshold[2]) return GROUP_MONTH;
        return GROUP_YEAR;
    }

    const char* groupingName() const {
        switch (grouping) {
            case GROUP_DAY:   return "Day";
            case GROUP_WEEK:  return "Week";
            case GROUP_MONTH: return "Month";
            case GROUP_YEAR:  return "Year";
        }
        return "Day";
    }

    // Days since the epoch for a Y/M/D triple (proleptic Gregorian).
    static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int64_t)doe - 719468;
    }

    static void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t yr = (int64_t)yoe + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
        m = mp + (mp < 10 ? 3 : -9);
        y = (int)(yr + (m <= 2));
    }

    static const char* monthName(int month) {
        static const char* kNames[12] = { "January", "February", "March", "April", "May", "June",
                                          "July", "August", "September", "October", "November", "December" };
        if (month < 1 || month > 12) return "Unknown";
        return kNames[month - 1];
    }

    int64_t groupKeyFor(const GalleryRecord& rec) const {
        switch (grouping) {
            case GROUP_DAY:
                return (int64_t)rec.year * 10000 + rec.month * 100 + rec.day;
            case GROUP_WEEK: {
                // Bucket by week starting Monday.
                int64_t days = daysFromCivil(rec.year, (unsigned)rec.month, (unsigned)rec.day);
                int64_t weekday = (days + 3) % 7;          // 1970-01-01 was a Thursday
                if (weekday < 0) weekday += 7;
                return days - weekday;                      // Monday of that week
            }
            case GROUP_MONTH:
                return (int64_t)rec.year * 100 + rec.month;
            case GROUP_YEAR:
                return (int64_t)rec.year;
        }
        return 0;
    }

    std::string sectionTitleFor(const GalleryRecord& rec, int curYear, int curMonth, int curDay) const {
        char buf[96];
        switch (grouping) {
            case GROUP_DAY: {
                int64_t todayDays = daysFromCivil(curYear, (unsigned)curMonth, (unsigned)curDay);
                int64_t recDays = daysFromCivil(rec.year, (unsigned)rec.month, (unsigned)rec.day);
                if (recDays == todayDays) return "Today";
                if (recDays == todayDays - 1) return "Yesterday";
                if (rec.year == curYear) {
                    snprintf(buf, sizeof(buf), "%d %s", rec.day, monthName(rec.month));
                } else {
                    snprintf(buf, sizeof(buf), "%d %s %d", rec.day, monthName(rec.month), rec.year);
                }
                return buf;
            }
            case GROUP_WEEK: {
                int64_t start = groupKeyFor(rec);
                int64_t end = start + 6;
                int sy, ey; unsigned sm, sd, em, ed;
                civilFromDays(start, sy, sm, sd);
                civilFromDays(end, ey, em, ed);

                int64_t todayDays = daysFromCivil(curYear, (unsigned)curMonth, (unsigned)curDay);
                int64_t weekday = (todayDays + 3) % 7;
                if (weekday < 0) weekday += 7;
                int64_t thisWeek = todayDays - weekday;
                if (start == thisWeek) return "This Week";
                if (start == thisWeek - 7) return "Last Week";

                if (sm == em && sy == ey) {
                    snprintf(buf, sizeof(buf), "%u - %u %s %d", sd, ed, monthName((int)sm), sy);
                } else if (sy == ey) {
                    snprintf(buf, sizeof(buf), "%u %s - %u %s %d", sd, monthName((int)sm), ed, monthName((int)em), sy);
                } else {
                    snprintf(buf, sizeof(buf), "%u %s %d - %u %s %d",
                             sd, monthName((int)sm), sy, ed, monthName((int)em), ey);
                }
                return buf;
            }
            case GROUP_MONTH: {
                if (rec.year == curYear && rec.month == curMonth) return "This Month";
                snprintf(buf, sizeof(buf), "%s %d", monthName(rec.month), rec.year);
                return buf;
            }
            case GROUP_YEAR: {
                if (rec.year == curYear) return "This Year";
                snprintf(buf, sizeof(buf), "%d", rec.year);
                return buf;
            }
        }
        return "";
    }

    // -------------------------------------------------------------------------
    // Build (regroup + layout). Only needed when the record set or the grouping
    // granularity changes - plain zooming goes through relayout().
    // -------------------------------------------------------------------------
    void buildTimeline(const std::vector<GalleryRecord>& records, float gridAreaW, bool hasFolderBanner = false) {
        lastGridAreaW = gridAreaW;
        lastHasBanner = hasFolderBanner;
        hasActiveFolderBanner = hasFolderBanner;

        sections.clear();
        flatVisibleItems.clear();
        flatAllItems.clear();
        totalPhotoCount = (int)records.size();

        if (records.empty()) {
            totalContentHeight = 0.0f;
            selectedFlatIndex = -1;
            return;
        }

        time_t now = time(nullptr);
        struct tm nowBuf{};
        silverplat::localTime(now, nowBuf);
        int curYear = 1900 + nowBuf.tm_year;
        int curMonth = 1 + nowBuf.tm_mon;
        int curDay = nowBuf.tm_mday;

        // Records arrive already sorted by capture time (newest first), so a
        // single sequential pass replaces the old map-of-vectors grouping.
        sections.reserve(64);
        int64_t activeKey = 0;
        bool haveActive = false;
        int globalFlatIdx = 0;

        for (const auto& rec : records) {
            int64_t key = groupKeyFor(rec);
            if (!haveActive || key != activeKey) {
                TimelineSection sec;
                sec.groupKey = key;
                sec.title = sectionTitleFor(rec, curYear, curMonth, curDay);
                sec.year = rec.year;
                sec.month = rec.month;
                sec.day = rec.day;
                sec.captureTime = rec.captureTime;
                sections.push_back(std::move(sec));
                activeKey = key;
                haveActive = true;
            }

            TimelineSection& sec = sections.back();
            TimelineItem itm;
            itm.record = rec;
            itm.sectionIndex = (int)sections.size() - 1;
            itm.itemIndexInSection = (int)sec.items.size();
            itm.flatIndex = globalFlatIdx++;
            itm.isSelected = (!selectedPath.empty() && rec.path == selectedPath);
            sec.items.push_back(std::move(itm));
        }

        buildSectionSubtitles();

        flatAllItems.reserve(records.size());
        for (auto& sec : sections) {
            for (auto& itm : sec.items) {
                flatAllItems.push_back(&itm);
            }
        }

        relayout(gridAreaW, hasFolderBanner, /*snapAll=*/true);
    }

    // -------------------------------------------------------------------------
    // Regroup without rebuilding items.
    //
    // Changing granularity (Day <-> Week <-> Month <-> Year) only moves the
    // section boundaries: the photo order is identical, because grouping is a
    // sequential partition of the same capture-time ordering. So the existing
    // items are *moved* into new sections rather than reconstructed, which
    // avoids copying a GalleryRecord (five strings) for every photo in the
    // library - the hitch that made this transition feel laggy.
    //
    // Because each item keeps its animated position, the following relayout
    // glides them to their new slots instead of snapping.
    // -------------------------------------------------------------------------
    void regroup(float gridAreaW, bool hasFolderBanner) {
        if (sections.empty()) return;

        std::vector<TimelineItem> flat;
        flat.reserve((size_t)totalPhotoCount);
        for (auto& sec : sections) {
            for (auto& itm : sec.items) flat.push_back(std::move(itm));
        }
        if (flat.empty()) return;

        sections.clear();
        flatVisibleItems.clear();
        flatAllItems.clear();

        time_t now = time(nullptr);
        struct tm nowBuf{};
        silverplat::localTime(now, nowBuf);
        int curYear = 1900 + nowBuf.tm_year;
        int curMonth = 1 + nowBuf.tm_mon;
        int curDay = nowBuf.tm_mday;

        sections.reserve(64);
        int64_t activeKey = 0;
        bool haveActive = false;
        int globalFlatIdx = 0;

        for (auto& itm : flat) {
            int64_t key = groupKeyFor(itm.record);
            if (!haveActive || key != activeKey) {
                TimelineSection sec;
                sec.groupKey = key;
                sec.title = sectionTitleFor(itm.record, curYear, curMonth, curDay);
                sec.year = itm.record.year;
                sec.month = itm.record.month;
                sec.day = itm.record.day;
                sec.captureTime = itm.record.captureTime;
                sections.push_back(std::move(sec));
                activeKey = key;
                haveActive = true;
            }

            TimelineSection& sec = sections.back();
            itm.sectionIndex = (int)sections.size() - 1;
            itm.itemIndexInSection = (int)sec.items.size();
            itm.flatIndex = globalFlatIdx++;
            sec.items.push_back(std::move(itm));
        }

        buildSectionSubtitles();

        flatAllItems.reserve(flat.size());
        for (auto& sec : sections) {
            for (auto& itm : sec.items) flatAllItems.push_back(&itm);
        }

        relayout(gridAreaW, hasFolderBanner, /*snapAll=*/false);
    }

    void buildSectionSubtitles() {
        for (auto& sec : sections) {
            int64_t totalBytes = 0;
            for (const auto& r : sec.items) totalBytes += r.record.fileSize;

            int count = (int)sec.items.size();
            const char* noun = (count == 1) ? "photo" : "photos";
            char subBuf[128];
            if (totalBytes > 1024LL * 1024 * 1024) {
                snprintf(subBuf, sizeof(subBuf), "%d %s - %.2f GB", count, noun,
                         (float)totalBytes / (1024.0f * 1024.0f * 1024.0f));
            } else {
                snprintf(subBuf, sizeof(subBuf), "%d %s - %.1f MB", count, noun,
                         (float)totalBytes / (1024.0f * 1024.0f));
            }
            sec.subtitle = subBuf;
        }
    }

    // -------------------------------------------------------------------------
    // Relayout only: recomputes geometry without regrouping or copying records.
    // Items that are off screen snap straight to their new slot, so the glide
    // animation only ever runs on what the user is actually looking at.
    // -------------------------------------------------------------------------
    void relayout(float gridAreaW, bool hasFolderBanner, bool snapAll = false) {
        lastGridAreaW = gridAreaW;
        lastHasBanner = hasFolderBanner;
        hasActiveFolderBanner = hasFolderBanner;

        if (sections.empty()) {
            totalContentHeight = 0.0f;
            return;
        }

        float availW = gridAreaW - (sidePadding * 2.0f);
        if (availW < 100.0f) availW = 100.0f;

        if (!isAutoZoom) {
            float desiredItemSize = minItemSize + zoomScale * (maxItemSize - minItemSize);
            columns = std::clamp((int)std::round((availW + gridGap) / (desiredItemSize + gridGap)), 1, maxColumns);
        } else {
            if (availW < 360.0f) columns = 2;
            else if (availW < 560.0f) columns = 3;
            else if (availW < 840.0f) columns = 4;
            else if (availW < 1150.0f) columns = 5;
            else if (availW < 1500.0f) columns = 6;
            else columns = 7;

            float autoSize = (availW - (gridGap * (columns - 1))) / (float)columns;
            zoomScale = std::clamp((autoSize - minItemSize) / std::max(1.0f, maxItemSize - minItemSize), zoomMin, zoomMax);
            updatePresetFromScale();
        }

        itemSize = (availW - (gridGap * (columns - 1))) / (float)columns;
        if (itemSize < tileMinSize) itemSize = tileMinSize;

        // Leave room for the folder breadcrumb banner so headers never overlap.
        float curY = topOffset + (hasFolderBanner ? folderBannerHeight : 0.0f);

        selectedFlatIndex = -1;
        for (auto& sec : sections) {
            sec.startY = curY;
            curY += headerHeight;

            int count = (int)sec.items.size();
            int numRows = (count + columns - 1) / columns;

            for (int i = 0; i < count; ++i) {
                TimelineItem& itm = sec.items[i];
                int col = i % columns;
                int row = i / columns;

                itm.itemIndexInSection = i;
                itm.w = itemSize;
                itm.h = itemSize;
                itm.x = sidePadding + col * (itemSize + gridGap);
                itm.y = curY + row * (itemSize + gridGap);
                itm.isSelected = (!selectedPath.empty() && itm.record.path == selectedPath);
                if (itm.isSelected) selectedFlatIndex = itm.flatIndex;

                // Tiles inside the animation band keep their current position so
                // they glide to the new slot; everything else is teleported.
                if (snapAll || !itm.inAnimBand) {
                    itm.animX = itm.x;
                    itm.animY = itm.y;
                    itm.animW = itm.w;
                    itm.animH = itm.h;
                    itm.motionVel.clear();
                }
            }

            float secGridH = numRows * itemSize + (numRows > 1 ? (numRows - 1) * gridGap : 0.0f);
            sec.height = headerHeight + secGridH + sectionBottomPadding;
            curY += secGridH + sectionSpacing;
        }

        totalContentHeight = curY + bottomPadding;
    }

    // -------------------------------------------------------------------------
    // Zoom controls
    // -------------------------------------------------------------------------
    void applyZoom(float newScale, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        zoomScale = std::clamp(newScale, zoomMin, zoomMax);
        isAutoZoom = false;
        updatePresetFromScale();
        zoomPillTimer = silveranim::rates().zoomPillSeconds;

        TimelineGrouping wanted = groupingForZoom(zoomScale);
        if (wanted != grouping) {
            grouping = wanted;
            if (sections.empty()) buildTimeline(records, gridAreaW, hasFolderBanner);
            else                  regroup(gridAreaW, hasFolderBanner);
        } else {
            relayout(gridAreaW, hasFolderBanner);
        }
        zoomPillText = std::string(groupingName()) + " - " + std::to_string(getZoomPercentage()) + "%";
    }

    void setZoomScale(float scale, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        applyZoom(scale, gridAreaW, records, hasFolderBanner);
    }

    void applyZoomDelta(float delta, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        applyZoom(zoomScale + delta, gridAreaW, records, hasFolderBanner);
    }

    void setPreset(GridPreset preset, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        float scale = zoomScale;
        switch (preset) {
            case PRESET_XL:      scale = presetScale[0]; break;
            case PRESET_LARGE:   scale = presetScale[1]; break;
            case PRESET_MEDIUM:  scale = presetScale[2]; break;
            case PRESET_SMALL:   scale = presetScale[3]; break;
            default: break;
        }
        applyZoom(scale, gridAreaW, records, hasFolderBanner);
        currentPreset = preset;
    }

    // Jump straight to a granularity (Day / Week / Month / Year).
    void setGrouping(TimelineGrouping g, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        int gi = std::clamp((int)g, (int)GROUP_DAY, (int)GROUP_YEAR);
        applyZoom(groupAnchor[gi], gridAreaW, records, hasFolderBanner);
    }

    // Step one granularity level in or out, the way a pinch gesture snaps on mobile.
    void stepGrouping(int direction, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        int g = (int)grouping - direction; // zoom in (+1) => finer bucket
        g = std::clamp(g, (int)GROUP_DAY, (int)GROUP_YEAR);
        applyZoom(groupAnchor[g], gridAreaW, records, hasFolderBanner);
    }

    void updatePresetFromScale() {
        // Snap to whichever configured preset the current scale sits closest to.
        float midXL     = (presetScale[0] + presetScale[1]) * 0.5f;
        float midLarge  = (presetScale[1] + presetScale[2]) * 0.5f;
        float midMedium = (presetScale[2] + presetScale[3]) * 0.5f;
        if (zoomScale >= midXL) currentPreset = PRESET_XL;
        else if (zoomScale >= midLarge) currentPreset = PRESET_LARGE;
        else if (zoomScale >= midMedium) currentPreset = PRESET_MEDIUM;
        else currentPreset = PRESET_SMALL;
    }

    int getZoomPercentage() const {
        return (int)std::round(zoomScale * 100.0f);
    }

    void zoomGrid(int delta, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        applyZoomDelta(delta * SilverConfig::get().num("zoom.keyStep", 0.08f), gridAreaW, records, hasFolderBanner);
    }

    void resetGridZoom(float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        isAutoZoom = true;
        zoomPillTimer = silveranim::rates().zoomPillSeconds;
        relayout(gridAreaW, hasFolderBanner);
        TimelineGrouping wanted = groupingForZoom(zoomScale);
        if (wanted != grouping) {
            grouping = wanted;
            if (sections.empty()) buildTimeline(records, gridAreaW, hasFolderBanner);
            else                  regroup(gridAreaW, hasFolderBanner);
        }
        zoomPillText = std::string(groupingName()) + " - " + std::to_string(getZoomPercentage()) + "%";
    }

    // -------------------------------------------------------------------------
    // Visibility & animation - touches only the sections inside the viewport.
    // -------------------------------------------------------------------------
    int firstSectionAtOrAfter(float y) const {
        // sections are ordered by startY
        int lo = 0, hi = (int)sections.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (sections[mid].startY + sections[mid].height < y) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    void updateVisibility(float scrollY, float windowH, float mouseX, float mouseY, float dt = 0.016f) {
        flatVisibleItems.clear();
        if (sections.empty()) return;

        const silveranim::Rates& anim = silveranim::rates();
        float viewTop = scrollY;
        float viewBottom = scrollY + windowH;

        // Work over a band wider than the viewport so tiles that are about to
        // scroll in are already moving with the rest.
        float band = anim.animateOffscreenTiles ? 1e9f : anim.layoutMargin;
        float bandTop = viewTop - band;
        float bandBottom = viewBottom + band;

        int startIdx = firstSectionAtOrAfter(bandTop);

        for (int s = startIdx; s < (int)sections.size(); ++s) {
            TimelineSection& sec = sections[s];
            if (sec.startY > bandBottom) break;

            for (auto& itm : sec.items) {
                const bool wasVisible = itm.isVisible;
                bool wasInBand = itm.inAnimBand;
                bool targetInBand = (itm.y + itm.h >= bandTop && itm.y <= bandBottom);
                bool targetVisible = (itm.y + itm.h >= viewTop - 60.0f &&
                                      itm.y <= viewBottom + 60.0f);
                itm.inAnimBand = targetInBand;

                if (!wasInBand && itm.inAnimBand) {
                    // Just entered the band from far away - start it at its slot
                    // rather than gliding in from a stale position.
                    itm.animX = itm.x;
                    itm.animY = itm.y;
                    itm.animW = itm.w;
                    itm.animH = itm.h;
                    itm.motionVel.clear();
                }

                // If zooming moved a destination into view while its animated
                // position is still outside the viewport, showing that stale
                // trajectory makes the photo fly down from the top. Entering
                // tiles start at their real slot; tiles already on screen keep
                // their ordinary smooth reflow.
                bool animOutsideView = (itm.animY + itm.animH < viewTop - 60.0f ||
                                        itm.animY > viewBottom + 60.0f);
                float travelX = std::abs(itm.animX - itm.x);
                float travelY = std::abs(itm.animY - itm.y);
                bool travelsTooFar = std::max(travelX, travelY) > anim.maxVisibleLayoutTravel;
                // A large travel is suspicious only for a tile newly entering
                // the viewport. Tiles that were already visible must keep their
                // shared reflow animation, otherwise a zoom makes half the grid
                // glide while the other half snaps. The outside-view check
                // still prevents genuinely stale positions flying in from the
                // top or bottom.
                if (targetVisible && (animOutsideView || (travelsTooFar && !wasVisible))) {
                    silveranim::snapRect(itm.animX, itm.animY, itm.animW, itm.animH,
                                         itm.x, itm.y, itm.w, itm.h);
                    itm.motionVel.clear();
                }

                if (itm.inAnimBand) {
                    silveranim::driveRect(itm.animX, itm.animY, itm.animW, itm.animH, itm.motionVel,
                                          itm.x, itm.y, itm.w, itm.h, anim.chLayout, dt);
                }

                float curTop = itm.animY;
                float curLeft = itm.animX;
                float curBottom = curTop + itm.animH;
                float curRight = curLeft + itm.animW;

                if (targetVisible) {
                    itm.isVisible = true;
                    itm.isHovered = (mouseX >= curLeft && mouseX <= curRight &&
                                     mouseY >= curTop - scrollY && mouseY <= curBottom - scrollY &&
                                     mouseY > 60.0f);
                    itm.isSelected = (!selectedPath.empty() && itm.record.path == selectedPath);
                    flatVisibleItems.push_back(&itm);
                } else {
                    itm.isVisible = false;
                    itm.isHovered = false;
                    // Still inside the band: keep animating, just do not draw it.
                    if (!itm.inAnimBand) {
                        silveranim::snapRect(itm.animX, itm.animY, itm.animW, itm.animH,
                                             itm.x, itm.y, itm.w, itm.h);
                        itm.motionVel.clear();
                    }
                }

                silveranim::driveFade(itm.hoverAnim, itm.hoverVel, itm.isHovered ? 1.0f : 0.0f, anim.chHover, dt);
                silveranim::driveFade(itm.selectAnim, itm.selectVel, itm.isSelected ? 1.0f : 0.0f, anim.chSelect, dt);
            }
        }
    }

    // Look up a laid-out item by path. Pointers stay valid across relayout(),
    // which only moves items; buildTimeline() and regroup() rebuild them.
    TimelineItem* findItem(const std::string& path) {
        if (path.empty() || flatAllItems.empty()) return nullptr;

        // The anchor is nearly always the current selection, and we already
        // know where that is - skip the scan.
        if (path == selectedPath && selectedFlatIndex >= 0 &&
            selectedFlatIndex < (int)flatAllItems.size()) {
            return flatAllItems[(size_t)selectedFlatIndex];
        }

        for (auto* itm : flatAllItems) {
            if (itm->record.path == path) return itm;
        }
        return nullptr;
    }

    // Is any on-screen tile still moving? Used to decide whether the app needs
    // to keep drawing frames or can go back to sleep.
    bool hasActiveMotion() const {
        const float eps = 0.05f;
        for (const auto* itm : flatVisibleItems) {
            if (std::abs(itm->animX - itm->x) > eps ||
                std::abs(itm->animY - itm->y) > eps ||
                std::abs(itm->animW - itm->w) > eps ||
                std::abs(itm->animH - itm->h) > eps ||
                std::abs(itm->hoverAnim - (itm->isHovered ? 1.0f : 0.0f)) > eps ||
                std::abs(itm->selectAnim - (itm->isSelected ? 1.0f : 0.0f)) > eps) {
                return true;
            }
        }
        return zoomPillTimer > 0.0f;
    }

    // Shift the in-flight animation positions by a scroll delta.
    //
    // Animated positions are stored in world space, but the scroll offset moves
    // underneath them when the viewport is re-anchored after a relayout. Without
    // this correction a tile keeps its old world position while world space
    // itself has shifted, so it starts its glide from far off screen - which
    // looked exactly like "the animation does not run" once scrolled down.
    void shiftAnimatedPositions(float deltaScroll) {
        if (deltaScroll == 0.0f) return;
        for (auto& sec : sections) {
            for (auto& itm : sec.items) {
                if (itm.inAnimBand) itm.animY += deltaScroll;
            }
        }
    }

    // The item closest to the middle of the viewport - the natural thing to keep
    // pinned when the layout changes under the user.
    TimelineItem* itemNearestViewport(float scrollY, float windowH) {
        if (flatAllItems.empty()) return nullptr;

        float focusY = scrollY + windowH * 0.5f;
        TimelineItem* best = nullptr;
        float bestDist = 1e30f;

        int startIdx = firstSectionAtOrAfter(scrollY);
        for (int s = std::max(0, startIdx - 1); s < (int)sections.size(); ++s) {
            TimelineSection& sec = sections[s];
            if (sec.startY > scrollY + windowH) break;

            for (auto& itm : sec.items) {
                float centre = itm.y + itm.h * 0.5f;
                float dist = std::abs(centre - focusY);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = &itm;
                }
            }
        }
        return best ? best : flatAllItems.front();
    }

    TimelineItem* getItemAt(float mouseX, float mouseY, float scrollY) {
        float worldY = mouseY + scrollY;
        for (auto* itm : flatVisibleItems) {
            if (mouseX >= itm->animX && mouseX <= itm->animX + itm->animW &&
                worldY >= itm->animY && worldY <= itm->animY + itm->animH) {
                return itm;
            }
        }
        return nullptr;
    }

    // Only two items can change state, so touch only those two rather than
    // writing a flag across the whole library.
    void selectItem(const std::string& path) {
        if (selectedFlatIndex >= 0 && selectedFlatIndex < (int)flatAllItems.size()) {
            flatAllItems[(size_t)selectedFlatIndex]->isSelected = false;
        }

        selectedPath = path;
        selectedFlatIndex = -1;

        for (auto* itm : flatAllItems) {
            if (itm->record.path == path) {
                itm->isSelected = true;
                selectedFlatIndex = itm->flatIndex;
                break;
            }
        }
    }

    void clearSelection() {
        if (selectedFlatIndex >= 0 && selectedFlatIndex < (int)flatAllItems.size()) {
            flatAllItems[(size_t)selectedFlatIndex]->isSelected = false;
        }
        selectedPath.clear();
        selectedFlatIndex = -1;
    }

    int getActiveStickySection(float scrollY) {
        for (int i = (int)sections.size() - 1; i >= 0; --i) {
            if (scrollY + topOffset >= sections[i].startY) {
                return i;
            }
        }
        return sections.empty() ? -1 : 0;
    }

    float getScrollForScrubRatio(float ratio, float windowH) {
        float maxScroll = std::max(0.0f, totalContentHeight - windowH);
        return ratio * maxScroll;
    }
};
