#pragma once

#include "db.h"
#include <cctype>
#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#endif
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <chrono>
#include <functional>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include "silver_platform.h"
#include "easyexif/exif.h"
#include "silver_codec.h"
#include "silver_config.h"
#ifndef _WIN32
#include <malloc.h>
#endif

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// GalleryScanner
//
// Two-stage pipeline so huge multi-folder libraries index quickly:
//   stage 1 - N walker threads traverse directories (readdir + stat only)
//   stage 2 - N inspector threads probe dimensions / EXIF for new or changed
//             files and hand finished records to a single DB writer
// Files already in the index with an unchanged mtime+size never get opened.
// -----------------------------------------------------------------------------
class GalleryScanner {
public:
    std::atomic<bool> isScanning{false};
    std::atomic<int> scannedFiles{0};
    std::atomic<int> totalIndexed{0};
    std::atomic<bool> hasFreshData{false};   // set when a batch lands in SQLite
    std::string currentStatus;
    std::mutex statusMutex;

    std::vector<std::string> searchRoots;
    std::vector<std::string> excludedRoots;
    std::thread workerThread;

    std::function<void()> onScanComplete;

    // Roots are resolved lazily, not in the constructor: the gallery's app object
    // is a namespace-scope static, so it is constructed before main() calls
    // SilverConfig::init(). Reading config here would always miss library.roots.
    bool rootsResolved = false;

    GalleryScanner() = default;

    void ensureRoots() {
        if (rootsResolved) return;
        discoverDefaultRoots();
        rootsResolved = true;
    }

    ~GalleryScanner() {
        stop();
    }

    // Expand a leading "~" and any $VAR / ${VAR} so config paths stay portable
    // across machines instead of hardcoding one user's home directory.
    static std::string expandPath(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (i == 0 && in[i] == '~' && (in.size() == 1 || in[1] == '/' || in[1] == '\\')) {
                out += homeDirectory();
                continue;
            }
            if (in[i] == '$' && i + 1 < in.size()) {
                size_t start = i + 1;
                bool braced = in[start] == '{';
                if (braced) ++start;
                size_t end = start;
                while (end < in.size() &&
                       (isalnum((unsigned char)in[end]) || in[end] == '_')) ++end;
                std::string name = in.substr(start, end - start);
                if (braced) {
                    if (end < in.size() && in[end] == '}') ++end;
                    else { out += in[i]; continue; }   // unterminated: leave as written
                }
                if (!name.empty()) {
                    const char* val = getenv(name.c_str());
                    if (val) out += val;
                    i = end - 1;
                    continue;
                }
            }
            out += in[i];
        }
        return out;
    }

    static std::string homeDirectory() {
#ifdef _WIN32
        const char* userProfile = getenv("USERPROFILE");
        return userProfile ? userProfile : "C:\\";
#else
        const char* home = getenv("HOME");
        if (home && *home) return home;
        struct passwd* pw = getpwuid(getuid());
        return pw && pw->pw_dir ? pw->pw_dir : "/home";
#endif
    }

    void discoverDefaultRoots() {
        rootsResolved = true;
        searchRoots.clear();
        excludedRoots.clear();

        // Config wins when it lists roots; the platform defaults below are only
        // used when the key is absent, so a user who sets "library.roots": []
        // deliberately gets an empty library rather than a surprise rescan of $HOME.
        const SilverConfig& cfg = SilverConfig::get();
        const std::vector<std::string> sentinel = { "<unset>" };
        std::vector<std::string> configured = cfg.stringArray("library.roots", sentinel);
        for (const std::string& raw : cfg.stringArray("library.exclude", {})) {
            std::string dir = expandPath(raw);
            if (!dir.empty()) excludedRoots.push_back(normalizeDir(dir));
        }

        if (configured != sentinel) {
            for (const std::string& raw : configured) {
                std::string dir = expandPath(raw);
                std::error_code ec;
                if (!dir.empty() && fs::exists(dir, ec) && fs::is_directory(dir, ec))
                    searchRoots.push_back(normalizeDir(dir));
            }
            dedupeRoots();
            return;
        }

#ifdef _WIN32
        std::string homeStr = homeDirectory();
        std::vector<std::string> candidates = {
            homeStr + "\\Pictures",
            homeStr + "\\Downloads",
            homeStr + "\\Documents",
            homeStr + "\\Desktop",
            homeStr + "\\Photos",
            homeStr + "\\OneDrive\\Pictures"
        };
#else
        std::string homeStr = homeDirectory();

        std::vector<std::string> candidates = {
            homeStr + "/Pictures",
            homeStr + "/Downloads",
            homeStr + "/Documents",
            homeStr + "/Desktop",
            homeStr + "/Images",
            homeStr + "/Photos",
            "/usr/share/backgrounds"
        };
#endif

        for (const auto& dir : candidates) {
            std::error_code ec;
            if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
                searchRoots.push_back(normalizeDir(dir));
            }
        }

        if (searchRoots.empty()) {
            searchRoots.push_back(normalizeDir(homeStr));
        }
        dedupeRoots();
    }

    static std::string normalizeDir(const std::string& in) {
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(in), ec);
        std::string out = ec ? in : p.string();
        while (out.size() > 1 && (out.back() == '/' || out.back() == '\\')) out.pop_back();
        return out;
    }

    void dedupeRoots() {
        // Order carries priority -- the first root is scanned first, and
        // addCustomRoot puts the user's pick at the front -- so this must not
        // sort. It keeps first occurrences and collapses ancestor/descendant
        // pairs, since scanning both ~ and ~/Pictures would index twice.
        std::vector<std::string> kept;
        for (const std::string& dir : searchRoots) {
            if (dir.empty() || isExcluded(dir)) continue;
            bool covered = false;
            for (const std::string& seen : kept) {
                if (dir == seen || isUnder(dir, seen)) { covered = true; break; }
            }
            if (covered) continue;
            // This root contains ones already kept; they become redundant.
            kept.erase(std::remove_if(kept.begin(), kept.end(),
                                      [&](const std::string& seen) {
                                          return isUnder(seen, dir);
                                      }),
                       kept.end());
            kept.push_back(dir);
        }
        searchRoots.swap(kept);
    }

    static bool isUnder(const std::string& path, const std::string& parent) {
        if (path.size() <= parent.size()) return false;
        if (path.compare(0, parent.size(), parent) != 0) return false;
        return path[parent.size()] == '/' || path[parent.size()] == '\\';
    }

    bool isExcluded(const std::string& path) const {
        for (const std::string& ex : excludedRoots) {
            if (path == ex || isUnder(path, ex)) return true;
        }
        return false;
    }

    void addCustomRoot(const std::string& raw) {
        ensureRoots();
        std::string dir = normalizeDir(expandPath(raw));
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        if (isExcluded(dir)) return;
        if (std::find(searchRoots.begin(), searchRoots.end(), dir) != searchRoots.end()) return;
        searchRoots.insert(searchRoots.begin(), dir);
        dedupeRoots();
    }

    static bool isSupportedExtension(const std::string& ext) {
        return silvercodec::isSupportedExtension(ext);
    }

    static bool isSkippedDirectory(const std::string& name) {
        if (name == "." || name == "..") return true;
        if (name.length() > 1 && name[0] == '.') return true; // .git, .cache, .local
        return name == "node_modules" || name == "target" || name == "__pycache__" ||
               name == "build" || name == "bin" || name == "obj" || name == "dist" ||
               name == "venv" || name == "vendor";
    }

    void startScan(GalleryDatabase& db, bool async = true) {
        if (isScanning.load()) return;
        ensureRoots();
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

    std::string getStatus() {
        std::lock_guard<std::mutex> lock(statusMutex);
        return currentStatus;
    }

private:
    struct Candidate {
        std::string path;
        int64_t mtime = 0;
        int64_t ctime = 0;
        int64_t size = 0;
    };

    void setStatus(const std::string& status) {
        std::lock_guard<std::mutex> lock(statusMutex);
        currentStatus = status;
    }

    void runScanInternal(GalleryDatabase& db) {
        isScanning.store(true);
        scannedFiles.store(0);
        totalIndexed.store(0);

        auto startClock = std::chrono::steady_clock::now();

        // One fast query instead of a lock per file.
        auto existingMap = db.fetchPathMtimeMap();

        const SilverConfig& cfg = SilverConfig::get();
        unsigned int cores = std::thread::hardware_concurrency();
        int walkerCount = cfg.integer("scanner.walkerThreads", 0);
        int inspectCount = cfg.integer("scanner.inspectorThreads", 0);
        if (walkerCount <= 0)  walkerCount  = (int)std::clamp(cores ? cores / 2 : 2u, 2u, 6u);
        if (inspectCount <= 0) inspectCount = (int)std::clamp(cores ? cores - 1 : 3u, 2u, 8u);

        // --- shared state between the two stages ---------------------------
        std::deque<std::string> dirQueue;
        std::mutex dirMutex;
        std::condition_variable dirCv;
        std::atomic<int> activeWalkers{0};
        std::atomic<bool> walkDone{false};

        std::deque<Candidate> workQueue;
        std::mutex workMutex;
        std::condition_variable workCv;

        std::mutex seenMutex;
        std::vector<std::string> seenPaths;   // everything currently on disk

        std::mutex batchMutex;
        std::vector<GalleryRecord> pendingBatch;
        const size_t kBatchSize = (size_t)std::max(16, cfg.integer("scanner.batchSize", 400));

        for (const auto& root : searchRoots) {
            dirQueue.push_back(root);
        }
        setStatus("Indexing " + std::to_string(searchRoots.size()) + " folders...");

        auto flushBatch = [&](bool force) {
            std::vector<GalleryRecord> toWrite;
            {
                std::lock_guard<std::mutex> lock(batchMutex);
                if (pendingBatch.empty()) return;
                if (!force && pendingBatch.size() < kBatchSize) return;
                toWrite.swap(pendingBatch);
            }
            db.insertOrUpdateBatch(toWrite);
            hasFreshData.store(true);
        };

        // --- stage 1: directory walkers ------------------------------------
        auto walker = [&]() {
            while (isScanning.load()) {
                std::string dir;
                {
                    std::unique_lock<std::mutex> lock(dirMutex);
                    dirCv.wait(lock, [&]() {
                        return !dirQueue.empty() || walkDone.load() || !isScanning.load();
                    });
                    if (!isScanning.load()) return;
                    if (dirQueue.empty()) {
                        if (walkDone.load()) return;
                        continue;
                    }
                    dir = dirQueue.front();
                    dirQueue.pop_front();
                    activeWalkers++;
                }

                std::vector<silverplat::DirEntry> entries;
                if (silverplat::listDirectory(dir, entries)) {
                    std::vector<std::string> subDirs;
                    for (const auto& de : entries) {
                        const std::string& name = de.name;
                        std::string full = dir + "/" + name;

                        bool isDir = de.isDir;
                        bool isFile = de.isFile;

                        struct stat st;
                        bool haveStat = false;
                        if (!de.typeKnown) {
                            if (stat(full.c_str(), &st) != 0) continue;
                            haveStat = true;
                            isDir = S_ISDIR(st.st_mode);
                            isFile = S_ISREG(st.st_mode);
                        }

                        if (isDir) {
                            if (isSkippedDirectory(name)) continue;
                            // Roots are normalized, so full is already canonical
                            // enough for a prefix test; avoid a syscall per directory.
                            if (!excludedRoots.empty() && isExcluded(full)) continue;
                            subDirs.push_back(full);
                            continue;
                        }
                        if (!isFile) continue;

                        size_t dot = name.find_last_of('.');
                        if (dot == std::string::npos) continue;
                        if (!isSupportedExtension(name.substr(dot))) continue;

                        if (!haveStat && stat(full.c_str(), &st) != 0) continue;

                        scannedFiles++;
                        {
                            std::lock_guard<std::mutex> lock(seenMutex);
                            seenPaths.push_back(full);
                        }

                        auto eit = existingMap.find(full);
                        if (eit != existingMap.end() &&
                            eit->second.first == (int64_t)st.st_mtime &&
                            eit->second.second == (int64_t)st.st_size) {
                            continue; // unchanged - never opened
                        }

                        Candidate c;
                        c.path = full;
                        c.mtime = (int64_t)st.st_mtime;
                        c.ctime = (int64_t)st.st_ctime;
                        c.size = (int64_t)st.st_size;
                        {
                            std::lock_guard<std::mutex> lock(workMutex);
                            workQueue.push_back(std::move(c));
                        }
                        workCv.notify_one();
                    }
                    if (!subDirs.empty()) {
                        std::lock_guard<std::mutex> lock(dirMutex);
                        for (auto& s : subDirs) dirQueue.push_back(std::move(s));
                        dirCv.notify_all();
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(dirMutex);
                    activeWalkers--;
                    if (dirQueue.empty() && activeWalkers.load() == 0) {
                        walkDone.store(true);
                        dirCv.notify_all();
                    }
                }
            }
        };

        // --- stage 2: metadata inspectors ----------------------------------
        auto inspector = [&]() {
            while (true) {
                Candidate c;
                {
                    std::unique_lock<std::mutex> lock(workMutex);
                    workCv.wait(lock, [&]() {
                        return !workQueue.empty() || walkDone.load() || !isScanning.load();
                    });
                    if (workQueue.empty()) {
                        if (walkDone.load() || !isScanning.load()) return;
                        continue;
                    }
                    c = std::move(workQueue.front());
                    workQueue.pop_front();
                }

                GalleryRecord rec = inspectFile(c);
                totalIndexed++;
                {
                    std::lock_guard<std::mutex> lock(batchMutex);
                    pendingBatch.push_back(std::move(rec));
                }
                flushBatch(false);
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < walkerCount; ++i) threads.emplace_back(walker);
        for (int i = 0; i < inspectCount; ++i) threads.emplace_back(inspector);

        // Keep the workers awake while stages drain.
        while (!walkDone.load() && isScanning.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            dirCv.notify_all();
            workCv.notify_all();
        }
        dirCv.notify_all();
        workCv.notify_all();

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

        flushBatch(true);

        // Drop records whose files disappeared, so stale folders vanish from the UI.
        if (isScanning.load() && cfg.flag("scanner.pruneMissing", true)) {
            std::unordered_set<std::string> alive;
            {
                std::lock_guard<std::mutex> lock(seenMutex);
                alive.reserve(seenPaths.size() * 2);
                for (auto& p : seenPaths) alive.insert(std::move(p));
                seenPaths.clear();
            }

            std::vector<std::string> gone;
            for (const auto& kv : existingMap) {
                if (!alive.count(kv.first)) gone.push_back(kv.first);
            }
            if (!gone.empty()) {
                db.deletePaths(gone);
                hasFreshData.store(true);
            }
        }

        float secs = std::chrono::duration<float>(std::chrono::steady_clock::now() - startClock).count();
        char statusBuf[160];
        snprintf(statusBuf, sizeof(statusBuf), "Indexed %d photos (%d new) in %.1fs",
                 scannedFiles.load(), totalIndexed.load(), secs);
        setStatus(statusBuf);
        std::cout << "[SilverGallery] " << statusBuf << std::endl;

        hasFreshData.store(true);
        isScanning.store(false);
#ifndef _WIN32
        malloc_trim(0);
#endif
    }

    GalleryRecord inspectFile(const Candidate& c) {
        const std::string& path = c.path;

        GalleryRecord rec;
        rec.path = path;
        rec.fileSize = c.size;
        rec.modifiedTime = c.mtime;
        rec.createdTime = c.ctime;
        rec.captureTime = c.mtime; // default fallback

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

        // 1. Header-only dimension probe (handles WebP / SVG / AVIF too)
        int w = 0, h = 0, comp = 0;
        if (silvercodec::info(path, &w, &h, &comp)) {
            rec.width = w;
            rec.height = h;
        }

        // 2. EXIF capture date (JPEG/TIFF header window only)
        if (rec.fileType == "JPG" || rec.fileType == "JPEG" || rec.fileType == "JFIF" || rec.fileType == "TIFF") {
            FILE* fp = fopen(path.c_str(), "rb");
            if (fp) {
                long readLen = (long)std::min<int64_t>(c.size, 65536);
                if (readLen > 0) {
                    std::vector<unsigned char> buf(readLen);
                    if (fread(buf.data(), 1, readLen, fp) == (size_t)readLen) {
                        easyexif::EXIFInfo info;
                        if (info.parseFrom(buf.data(), readLen) == PARSE_EXIF_SUCCESS) {
                            if (!info.DateTimeOriginal.empty()) {
                                struct tm tmExif{};
                                if (silverplat::parseExifDateTime(info.DateTimeOriginal.c_str(), tmExif)) {
                                    time_t exifEpoch = mktime(&tmExif);
                                    if (exifEpoch > 0) {
                                        rec.captureTime = exifEpoch;
                                    }
                                }
                            }
                            // EXIF rotation means the stored w/h are swapped
                            if (info.Orientation >= 5 && info.Orientation <= 8) {
                                std::swap(rec.width, rec.height);
                            }
                        }
                    }
                }
                fclose(fp);
            }
        }

        // Format Year, Month, Day, Date Label
        time_t t = (rec.captureTime > 0) ? (time_t)rec.captureTime : (time_t)c.mtime;
        struct tm ltmBuf{};
        if (silverplat::localTime(t, ltmBuf)) {
            struct tm* ltm = &ltmBuf;
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
