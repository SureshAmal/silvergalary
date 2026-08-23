#pragma once

#include "db.h"
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <functional>
#include <fstream>
#include <sys/stat.h>
#include "easyexif/exif.h"
#include "stb_image.h"

namespace fs = std::filesystem;

class GalleryScanner {
public:
    std::atomic<bool> isScanning{false};
    std::atomic<int> scannedFiles{0};
    std::atomic<int> totalIndexed{0};
    std::string currentStatus;
    std::mutex statusMutex;

    std::vector<std::string> searchRoots;
    std::thread workerThread;

    std::function<void()> onScanComplete;

    GalleryScanner() {
        discoverDefaultRoots();
    }

    ~GalleryScanner() {
        stop();
    }

    void discoverDefaultRoots() {
        searchRoots.clear();
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/home";
        }
        std::string homeStr(home);

        std::vector<std::string> candidates = {
            homeStr + "/Pictures",
            homeStr + "/Downloads",
            homeStr + "/Documents",
            homeStr + "/Desktop",
            homeStr + "/Images",
            homeStr + "/Photos",
            "/usr/share/backgrounds"
        };

        for (const auto& dir : candidates) {
            std::error_code ec;
            if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
                searchRoots.push_back(dir);
            }
        }

        if (searchRoots.empty()) {
            searchRoots.push_back(homeStr);
        }
    }

    void addCustomRoot(const std::string& dir) {
        std::error_code ec;
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            if (std::find(searchRoots.begin(), searchRoots.end(), dir) == searchRoots.end()) {
                searchRoots.insert(searchRoots.begin(), dir);
            }
        }
    }

    static bool isSupportedExtension(const std::string& ext) {
        static const std::vector<std::string> kExts = {
            ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif",
            ".tga", ".ppm", ".pgm", ".svg", ".ico", ".jfif", ".avif"
        };
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return std::find(kExts.begin(), kExts.end(), lower) != kExts.end();
    }

    void startScan(GalleryDatabase& db, bool async = true) {
        if (isScanning.load()) return;
        if (workerThread.joinable()) workerThread.join();

        if (async) {
            workerThread = std::thread([this, &db]() {
                runScanInternal(db);
                if (onScanComplete) onScanComplete();
            });
        } else {
            runScanInternal(db);
            if (onScanComplete) onScanComplete();
        }
    }

    void stop() {
        isScanning.store(false);
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

private:
    void setStatus(const std::string& status) {
        std::lock_guard<std::mutex> lock(statusMutex);
        currentStatus = status;
    }

    void runScanInternal(GalleryDatabase& db) {
        isScanning.store(true);
        scannedFiles.store(0);
        totalIndexed.store(0);

        // Fetch existing path/mtime/size in one fast query to avoid millions of SQLite locks
        auto existingMap = db.fetchPathMtimeMap();

        std::vector<GalleryRecord> batch;
        const size_t kBatchSize = 100;

        for (const auto& root : searchRoots) {
            if (!isScanning.load()) break;

            setStatus("Scanning: " + root);

            std::error_code ec;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            fs::recursive_directory_iterator endIt;

            while (it != endIt && isScanning.load()) {
                if (ec) {
                    it.increment(ec);
                    continue;
                }

                const auto& entry = *it;

                // Skip hidden folders (.git, .cache, .local, etc.) and heavy build directories
                if (entry.is_directory(ec)) {
                    std::string dirName = entry.path().filename().string();
                    if ((dirName.length() > 1 && dirName[0] == '.') ||
                        dirName == "node_modules" || dirName == "target" ||
                        dirName == "__pycache__" || dirName == "build" ||
                        dirName == "bin" || dirName == "obj" || dirName == "dist") {
                        it.disable_recursion_pending();
                        it.increment(ec);
                        continue;
                    }
                }

                if (entry.is_regular_file(ec)) {
                    std::string ext = entry.path().extension().string();
                    if (isSupportedExtension(ext)) {
                        scannedFiles++;
                        std::string filePath = entry.path().string();

                        struct stat st;
                        if (stat(filePath.c_str(), &st) == 0) {
                            bool needFullInspect = true;

                            auto eit = existingMap.find(filePath);
                            if (eit != existingMap.end()) {
                                if (eit->second.first == st.st_mtime && eit->second.second == st.st_size) {
                                    needFullInspect = false;
                                }
                            }

                            if (needFullInspect) {
                                GalleryRecord rec = inspectFile(filePath, st);
                                batch.push_back(std::move(rec));
                                totalIndexed++;

                                if (batch.size() >= kBatchSize) {
                                    db.insertOrUpdateBatch(batch);
                                    batch.clear();
                                }
                            }
                        }
                    }
                }
                it.increment(ec);
            }
        }

        if (!batch.empty()) {
            db.insertOrUpdateBatch(batch);
            batch.clear();
        }

        setStatus("Scan complete. " + std::to_string(scannedFiles.load()) + " photos indexed.");
        isScanning.store(false);
    }

    GalleryRecord inspectFile(const std::string& path, const struct stat& st) {
        GalleryRecord rec;
        rec.path = path;
        rec.fileSize = st.st_size;
        rec.modifiedTime = st.st_mtime;
        rec.createdTime = st.st_ctime;
        rec.captureTime = st.st_mtime; // default fallback

        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            rec.filename = path.substr(lastSlash + 1);
            rec.folder = path.substr(0, lastSlash);
        } else {
            rec.filename = path;
            rec.folder = ".";
        }

        size_t lastDot = rec.filename.find_last_of('.');
        if (lastDot != std::string::npos) {
            rec.fileType = rec.filename.substr(lastDot + 1);
            std::transform(rec.fileType.begin(), rec.fileType.end(), rec.fileType.begin(), ::toupper);
        }

        // 1. Fast STB Header Info
        int w = 0, h = 0, comp = 0;
        if (stbi_info(path.c_str(), &w, &h, &comp)) {
            rec.width = w;
            rec.height = h;
        }

        // 2. EXIF Capture Date Extraction
        if (rec.fileType == "JPG" || rec.fileType == "JPEG" || rec.fileType == "TIFF") {
            FILE* fp = fopen(path.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long len = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                // Read first 64KB for EXIF headers
                long readLen = std::min(len, 65536L);
                if (readLen > 0) {
                    std::vector<unsigned char> buf(readLen);
                    if (fread(buf.data(), 1, readLen, fp) == (size_t)readLen) {
                        easyexif::EXIFInfo info;
                        if (info.parseFrom(buf.data(), readLen) == PARSE_EXIF_SUCCESS) {
                            if (!info.DateTimeOriginal.empty()) {
                                struct tm tmExif{};
                                if (strptime(info.DateTimeOriginal.c_str(), "%Y:%m:%d %H:%M:%S", &tmExif)) {
                                    time_t exifEpoch = timegm(&tmExif);
                                    if (exifEpoch > 0) {
                                        rec.captureTime = exifEpoch;
                                    }
                                }
                            }
                        }
                    }
                }
                fclose(fp);
            }
        }

        // Format Year, Month, Day, Date Label
        time_t t = (rec.captureTime > 0) ? (time_t)rec.captureTime : (time_t)st.st_mtime;
        struct tm* ltm = localtime(&t);
        if (ltm) {
            rec.year = 1900 + ltm->tm_year;
            rec.month = 1 + ltm->tm_mon;
            rec.day = ltm->tm_mday;

            char dateBuf[64];
            strftime(dateBuf, sizeof(dateBuf), "%B %Y", ltm); // e.g. "August 2026"
            rec.dateLabel = dateBuf;
        }

        return rec;
    }
};
