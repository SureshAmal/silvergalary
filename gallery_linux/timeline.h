#pragma once

#include "db.h"
#include <vector>
#include <string>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <map>

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
    bool isVisible = false;
    bool isHovered = false;
    bool isSelected = false;
    float hoverAnim = 0.0f;
    float selectAnim = 0.0f;
};

struct TimelineSection {
    std::string title;        // "Today", "Yesterday", "August 2026", "2025"
    std::string subtitle;     // "48 photos · 240 MB"
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

    float zoomPillTimer = 0.0f;
    std::string zoomPillText = "";
    bool hasActiveFolderBanner = false;

    float gridGap = 10.0f;
    float sidePadding = 24.0f;
    float headerHeight = 46.0f;
    float topOffset = 70.0f; // Below top bar
    float bottomPadding = 60.0f;

    void buildTimeline(const std::vector<GalleryRecord>& records, float gridAreaW, bool hasFolderBanner = false) {
        // Save previous item positions to animate smooth layout transitions (gliding across rows/cols)
        std::map<std::string, std::vector<float>> prevPos;
        for (const auto& sec : sections) {
            for (const auto& itm : sec.items) {
                float curX = (itm.animW > 0.0f) ? itm.animX : itm.x;
                float curY = (itm.animH > 0.0f) ? itm.animY : itm.y;
                float curW = (itm.animW > 0.0f) ? itm.animW : itm.w;
                float curH = (itm.animH > 0.0f) ? itm.animH : itm.h;
                prevPos[itm.record.path] = { curX, curY, curW, curH, itm.hoverAnim, itm.selectAnim };
            }
        }

        sections.clear();
        flatVisibleItems.clear();
        flatAllItems.clear();
        totalPhotoCount = (int)records.size();
        hasActiveFolderBanner = hasFolderBanner;

        if (records.empty()) {
            totalContentHeight = 0.0f;
            selectedFlatIndex = -1;
            return;
        }

        // Get current system date for "Today" & "Yesterday" detection
        time_t now = time(nullptr);
        struct tm* nowTm = localtime(&now);
        int curYear = 1900 + nowTm->tm_year;
        int curMonth = 1 + nowTm->tm_mon;
        int curDay = nowTm->tm_mday;

        std::map<std::string, std::vector<GalleryRecord>> groupMap;
        std::vector<std::string> groupOrder;

        for (const auto& rec : records) {
            std::string key;
            if (rec.year == curYear && rec.month == curMonth && rec.day == curDay) {
                key = "Today";
            } else if (rec.year == curYear && rec.month == curMonth && rec.day == curDay - 1) {
                key = "Yesterday";
            } else {
                key = rec.dateLabel.empty() ? (std::to_string(rec.year) + "-" + std::to_string(rec.month)) : rec.dateLabel;
            }

            if (groupMap.find(key) == groupMap.end()) {
                groupOrder.push_back(key);
            }
            groupMap[key].push_back(rec);
        }

        float availW = gridAreaW - (sidePadding * 2.0f);
        if (!isAutoZoom) {
            float minItemSize = 70.0f;
            float maxItemSize = 440.0f;
            float desiredItemSize = minItemSize + zoomScale * (maxItemSize - minItemSize);
            int calcCols = std::clamp((int)std::round((availW + gridGap) / (desiredItemSize + gridGap)), 1, 14);
            columns = calcCols;
        } else {
            // Calculate responsive column count based on available grid width
            if (availW < 360.0f) columns = 2;
            else if (availW < 560.0f) columns = 3;
            else if (availW < 840.0f) columns = 4;
            else if (availW < 1150.0f) columns = 5;
            else if (availW < 1500.0f) columns = 6;
            else columns = 7;

            float itemSize = (availW - (gridGap * (columns - 1))) / (float)columns;
            zoomScale = std::clamp((itemSize - 70.0f) / (440.0f - 70.0f), 0.05f, 1.0f);
            updatePresetFromScale();
        }

        float itemSize = (availW - (gridGap * (columns - 1))) / (float)columns;
        if (itemSize < 50.0f) itemSize = 50.0f;

        // When folder breadcrumb banner is active, leave 56px space so banner NEVER overlaps month headers!
        float curY = topOffset + (hasFolderBanner ? 56.0f : 0.0f);
        int globalFlatIdx = 0;

        for (size_t sIdx = 0; sIdx < groupOrder.size(); ++sIdx) {
            const std::string& key = groupOrder[sIdx];
            const auto& recList = groupMap[key];

            TimelineSection sec;
            sec.title = key;
            sec.startY = curY;
            sec.year = recList[0].year;
            sec.month = recList[0].month;
            sec.day = recList[0].day;
            sec.captureTime = recList[0].captureTime;

            int64_t totalBytes = 0;
            for (const auto& r : recList) totalBytes += r.fileSize;

            char subBuf[128];
            if (totalBytes > 1024 * 1024 * 1024) {
                snprintf(subBuf, sizeof(subBuf), "%d photos - %.2f GB", (int)recList.size(), (float)totalBytes / (1024.0f * 1024.0f * 1024.0f));
            } else {
                snprintf(subBuf, sizeof(subBuf), "%d photos - %.1f MB", (int)recList.size(), (float)totalBytes / (1024.0f * 1024.0f));
            }
            sec.subtitle = subBuf;

            curY += headerHeight;

            int numRows = ((int)recList.size() + columns - 1) / columns;
            for (size_t i = 0; i < recList.size(); ++i) {
                int col = (int)(i % columns);
                int row = (int)(i / columns);

                TimelineItem itm;
                itm.record = recList[i];
                itm.sectionIndex = (int)sIdx;
                itm.itemIndexInSection = (int)i;
                itm.flatIndex = globalFlatIdx++;
                itm.w = itemSize;
                itm.h = itemSize;
                itm.x = sidePadding + col * (itemSize + gridGap);
                itm.y = curY + row * (itemSize + gridGap);
                itm.isSelected = (!selectedPath.empty() && itm.record.path == selectedPath);

                auto pit = prevPos.find(recList[i].path);
                if (pit != prevPos.end()) {
                    itm.animX = pit->second[0];
                    itm.animY = pit->second[1];
                    itm.animW = pit->second[2];
                    itm.animH = pit->second[3];
                    itm.hoverAnim = pit->second[4];
                    itm.selectAnim = pit->second[5];
                } else {
                    itm.animX = itm.x;
                    itm.animY = itm.y;
                    itm.animW = itm.w;
                    itm.animH = itm.h;
                }

                sec.items.push_back(std::move(itm));
            }

            float secGridH = numRows * itemSize + (numRows > 1 ? (numRows - 1) * gridGap : 0.0f);
            sec.height = headerHeight + secGridH + 16.0f;
            curY += secGridH + 20.0f;

            sections.push_back(std::move(sec));
        }

        // Build flatAllItems pointers
        for (auto& sec : sections) {
            for (auto& itm : sec.items) {
                flatAllItems.push_back(&itm);
                if (itm.isSelected) {
                    selectedFlatIndex = itm.flatIndex;
                }
            }
        }

        totalContentHeight = curY + bottomPadding;
    }

    void setZoomScale(float scale, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        zoomScale = std::clamp(scale, 0.05f, 1.0f);
        isAutoZoom = false;
        updatePresetFromScale();
        zoomPillTimer = 2.0f;
        buildTimeline(records, gridAreaW, hasFolderBanner);
    }

    void applyZoomDelta(float delta, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        zoomScale = std::clamp(zoomScale + delta, 0.05f, 1.0f);
        isAutoZoom = false;
        zoomPillTimer = 2.0f;
        updatePresetFromScale();
        buildTimeline(records, gridAreaW, hasFolderBanner);
    }

    void setPreset(GridPreset preset, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        currentPreset = preset;
        isAutoZoom = false;
        switch (preset) {
            case PRESET_XL:      zoomScale = 1.00f; break;
            case PRESET_LARGE:   zoomScale = 0.70f; break;
            case PRESET_MEDIUM:  zoomScale = 0.45f; break;
            case PRESET_SMALL:   zoomScale = 0.20f; break;
            default: break;
        }
        zoomPillTimer = 2.0f;
        buildTimeline(records, gridAreaW, hasFolderBanner);
    }

    void updatePresetFromScale() {
        if (zoomScale >= 0.85f) currentPreset = PRESET_XL;
        else if (zoomScale >= 0.58f) currentPreset = PRESET_LARGE;
        else if (zoomScale >= 0.35f) currentPreset = PRESET_MEDIUM;
        else currentPreset = PRESET_SMALL;
    }

    int getZoomPercentage() const {
        return (int)std::round(zoomScale * 100.0f);
    }

    void zoomGrid(int delta, float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        // Delta > 0 zooms in (increases zoomScale)
        applyZoomDelta(delta * 0.08f, gridAreaW, records, hasFolderBanner);
    }

    void resetGridZoom(float gridAreaW, const std::vector<GalleryRecord>& records, bool hasFolderBanner) {
        isAutoZoom = true;
        zoomPillTimer = 1.5f;
        buildTimeline(records, gridAreaW, hasFolderBanner);
    }

    void updateVisibility(float scrollY, float windowH, float mouseX, float mouseY, float dt = 0.016f) {
        flatVisibleItems.clear();
        float viewTop = scrollY;
        float viewBottom = scrollY + windowH;

        float posLerp = 1.0f - std::exp(-22.0f * dt);

        for (auto& sec : sections) {
            float secTop = sec.startY;
            float secBottom = sec.startY + sec.height;

            if (secBottom < viewTop - 100.0f || secTop > viewBottom + 100.0f) {
                for (auto& itm : sec.items) {
                    // Smoothly animate position even when offscreen
                    itm.animX += (itm.x - itm.animX) * posLerp;
                    itm.animY += (itm.y - itm.animY) * posLerp;
                    itm.animW += (itm.w - itm.animW) * posLerp;
                    itm.animH += (itm.h - itm.animH) * posLerp;

                    itm.isVisible = false;
                    itm.isHovered = false;
                    itm.hoverAnim += (0.0f - itm.hoverAnim) * (1.0f - std::exp(-24.0f * dt));
                    if (itm.hoverAnim < 0.005f) itm.hoverAnim = 0.0f;
                }
                continue;
            }

            for (auto& itm : sec.items) {
                itm.animX += (itm.x - itm.animX) * posLerp;
                itm.animY += (itm.y - itm.animY) * posLerp;
                itm.animW += (itm.w - itm.animW) * posLerp;
                itm.animH += (itm.h - itm.animH) * posLerp;

                if (std::abs(itm.animX - itm.x) < 0.2f) itm.animX = itm.x;
                if (std::abs(itm.animY - itm.y) < 0.2f) itm.animY = itm.y;
                if (std::abs(itm.animW - itm.w) < 0.2f) itm.animW = itm.w;
                if (std::abs(itm.animH - itm.h) < 0.2f) itm.animH = itm.h;

                float curTop = (itm.animH > 0.0f) ? itm.animY : itm.y;
                float curBottom = curTop + ((itm.animH > 0.0f) ? itm.animH : itm.h);
                float curLeft = (itm.animW > 0.0f) ? itm.animX : itm.x;
                float curRight = curLeft + ((itm.animW > 0.0f) ? itm.animW : itm.w);

                if (curBottom >= viewTop - 60.0f && curTop <= viewBottom + 60.0f) {
                    itm.isVisible = true;
                    itm.isHovered = (mouseX >= curLeft && mouseX <= curRight &&
                                     mouseY >= curTop - scrollY && mouseY <= curBottom - scrollY &&
                                     mouseY > 60.0f);
                    itm.isSelected = (!selectedPath.empty() && itm.record.path == selectedPath);
                    flatVisibleItems.push_back(&itm);
                } else {
                    itm.isVisible = false;
                    itm.isHovered = false;
                }

                float targetHover = itm.isHovered ? 1.0f : 0.0f;
                itm.hoverAnim += (targetHover - itm.hoverAnim) * (1.0f - std::exp(-24.0f * dt));
                if (std::abs(itm.hoverAnim - targetHover) < 0.005f) itm.hoverAnim = targetHover;

                float targetSelect = itm.isSelected ? 1.0f : 0.0f;
                itm.selectAnim += (targetSelect - itm.selectAnim) * (1.0f - std::exp(-24.0f * dt));
                if (std::abs(itm.selectAnim - targetSelect) < 0.005f) itm.selectAnim = targetSelect;
            }
        }
    }

    TimelineItem* getItemAt(float mouseX, float mouseY, float scrollY) {
        float worldY = mouseY + scrollY;
        for (auto* itm : flatVisibleItems) {
            float ix = (itm->animW > 0.0f) ? itm->animX : itm->x;
            float iy = (itm->animH > 0.0f) ? itm->animY : itm->y;
            float iw = (itm->animW > 0.0f) ? itm->animW : itm->w;
            float ih = (itm->animH > 0.0f) ? itm->animH : itm->h;
            if (mouseX >= ix && mouseX <= ix + iw &&
                worldY >= iy && worldY <= iy + ih) {
                return itm;
            }
        }
        return nullptr;
    }

    void selectItem(const std::string& path) {
        selectedPath = path;
        selectedFlatIndex = -1;
        for (auto* itm : flatAllItems) {
            if (itm->record.path == path) {
                itm->isSelected = true;
                selectedFlatIndex = itm->flatIndex;
            } else {
                itm->isSelected = false;
            }
        }
    }

    void clearSelection() {
        selectedPath.clear();
        selectedFlatIndex = -1;
        for (auto* itm : flatAllItems) {
            itm->isSelected = false;
        }
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
