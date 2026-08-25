#pragma once

#include "gl_loader.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include "silver_codec.h"
#include "silver_config.h"
#include "silver_xdgthumb.h"
#include "silver_anim.h"
#include "silver_constants.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <filesystem>
#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#endif

struct ThumbnailItem {
    GLuint texId = 0;
    int width = 0;
    int height = 0;
    int loadedEdge = 0;      // resolution tier this texture was decoded at
    int origWidth = 0;       // true source dimensions
    int origHeight = 0;
    bool ready = false;
    bool inProgress = false;
    bool failed = false;      // decode attempted and failed - stop retrying
    uint64_t lastUsedFrame = 0;
};

struct DecodedThumb {
    std::string path;
    int w = 0;
    int h = 0;
    int edge = 0;
    int origW = 0;
    int origH = 0;
    std::vector<unsigned char> data;
};

// A thumbnail request: path plus the resolution tier the grid currently needs.
struct ThumbRequest {
    std::string path;
    int edge = 256;
    bool diskOnly = false;   // pre-generate the cache file, don't upload a texture
};

class ThumbnailManager {
public:
    std::unordered_map<std::string, ThumbnailItem> cache;
    std::deque<ThumbRequest> loadQueue;
    std::unordered_set<std::string> queuedSet;
    std::unordered_set<std::string> inFlightSet;   // handed to a worker, not yet decoded
    std::unordered_set<std::string> pendingUpload; // decoded, waiting for the GL upload
    std::vector<ThumbRequest> pinned;              // selection / fullscreen priority loads
    std::deque<DecodedThumb> readyQueue;

    // mutable: taking a lock to inspect state is not a logical mutation, and
    // callers that only ask "is anything pending?" should be able to be const.
    mutable std::mutex queueLock;
    mutable std::mutex readyLock;
    std::condition_variable cv;
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;

    // Budget for resident GL textures.
    //
    // This used to be a flat *count*, which silently scaled with the zoom tier:
    // 2000 textures is 205 MB at tier 160 but 2.1 GB at tier 512, because a
    // 512x512 RGBA thumbnail is 1 MB. Budgeting in bytes is tier-independent and
    // is what texture streaming systems do.
    size_t maxResidentBytes = 256ull * 1024 * 1024;
    size_t residentBytes = 0;
    size_t residentTextures = 0;   // kept for diagnostics only
    int uploadsPerFrame = 32;
    int diskCacheQuality = 85;
    std::vector<int> tiers = { 96, 160, 256, 384, 512 };
    float tierHeadroom = 1.35f;
    float pixelScale = 1.0f;

    // One cached image per photo, at the largest tier. Every smaller size is
    // derived from it in memory (~2 ms) rather than decoded from the original
    // (~16 ms) and stored separately. Caching per tier meant a photo could be
    // decoded four times and stored four times.
    int masterTier = 512;
    uint64_t frameCounter = 0;
    float lastFrameDt = 1.0f / 60.0f;

    // Persistent on-disk thumbnail cache (survives restarts, kills re-decoding).
    bool diskCacheEnabled = true;
    bool useSharedCache = true;   // read ~/.cache/thumbnails written by the desktop
    std::string diskCacheDir;

    // Low priority queue used to pre-generate the whole library in the
    // background once the visible tiles are done.
    std::deque<ThumbRequest> prewarmQueue;
    std::atomic<int> prewarmRemaining{0};
    std::atomic<int> prewarmActive{0};   // workers currently on background work
    int maxPrewarmWorkers = 2;           // the rest stay free for on-screen tiles
    int prewarmEdge = 0;                 // tier the queue was built for

    void applyConfig() {
        const SilverConfig& c = SilverConfig::get();
        maxResidentBytes = (size_t)std::max(32, c.integer("thumbnails.maxResidentMegabytes", 256))
                           * 1024ull * 1024ull;
        uploadsPerFrame     = std::max(1, c.integer("thumbnails.uploadsPerFrame", 32));
        diskCacheQuality    = std::clamp(c.integer("thumbnails.diskCacheQuality", 85), 30, 100);
        tierHeadroom        = std::max(1.0f, c.num("thumbnails.tierHeadroom", 1.35f));
        tiers               = c.intArray("thumbnails.tiers", { 96, 160, 256, 384, 512 });
        std::sort(tiers.begin(), tiers.end());

        int configuredMaster = c.integer("thumbnails.masterTier", 0);
        masterTier = (configuredMaster > 0) ? configuredMaster
                                            : (tiers.empty() ? 512 : tiers.back());
        if (!c.flag("thumbnails.diskCache", true)) diskCacheEnabled = false;
        useSharedCache = c.flag("thumbnails.useSharedCache", true);
    }

    void setPixelScale(float scale) {
        pixelScale = (scale >= silver::defaults::minPixelScale) ? scale : 1.0f;
    }

    float scrollOffset = 0.0f;
    float targetScrollOffset = 0.0f;
    float scrollVelocity = 0.0f;

    void init() {
        applyConfig();
        setupDiskCache();

        // Utilize available CPU threads (up to 8 worker threads for parallel decoding)
        unsigned int numCores = std::thread::hardware_concurrency();
        int workerCount = (int)std::clamp(numCores > 2 ? numCores - 2 : 2u, 2u, 8u);

        // Background pre-generation must leave capacity for on-screen tiles, but
        // it does not need to be throttled hard: one thumbnail is ~16 ms, so a
        // foreground request waits at most one item behind. Reserving two
        // workers keeps the grid responsive while still warming the library in
        // roughly a quarter of the time a 25% cap took.
        int reserved = std::max(1, SilverConfig::get().integer("thumbnails.reservedWorkers", 2));
        int configured = SilverConfig::get().integer("thumbnails.prewarmWorkers", 0);
        maxPrewarmWorkers = (configured > 0) ? configured
                                            : std::max(1, workerCount - reserved);

        for (int i = 0; i < workerCount; ++i) {
            workers.emplace_back(&ThumbnailManager::workerLoop, this);
        }
    }

    ~ThumbnailManager() {
        running = false;
        cv.notify_all();
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
        for (auto& [path, item] : cache) {
            if (item.texId) {
                glDeleteTextures(1, &item.texId);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Disk cache plumbing
    // -------------------------------------------------------------------------
    void setupDiskCache() {
#ifdef _WIN32
        const char* appData = getenv("APPDATA");
        std::string baseDir = (appData && appData[0]) ? appData : "C:\\ProgramData";
        diskCacheDir = baseDir + "\\SilverGallery\\thumbs";
#else
        const char* xdgCache = getenv("XDG_CACHE_HOME");
        std::string baseDir;
        if (xdgCache && xdgCache[0]) {
            baseDir = xdgCache;
        } else {
            const char* home = getenv("HOME");
            if (!home) {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : "/tmp";
            }
            baseDir = std::string(home) + "/.cache";
        }
        diskCacheDir = baseDir + "/silver_gallery/thumbs";
#endif
        std::error_code ec;
        std::filesystem::create_directories(diskCacheDir, ec);
        if (ec) {
            diskCacheEnabled = false;
            return;
        }

        // 256 shards keep any single directory to a few dozen entries. A flat
        // directory works on ext4 but degrades badly elsewhere, and every other
        // photo manager shards for the same reason.
        for (int i = 0; i < 256; ++i) {
            char sub[8];
            snprintf(sub, sizeof(sub), "/%02x", i);
            std::filesystem::create_directories(diskCacheDir + sub, ec);
        }
    }

    static uint64_t hashPath(const std::string& s) {
        uint64_t hash = 1469598103934665603ULL; // FNV-1a
        for (unsigned char c : s) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    std::string diskCachePath(const std::string& path, int edge) const {
        uint64_t h = hashPath(path);
        char buf[80];
        snprintf(buf, sizeof(buf), "/%02x/%016llx_%d.sthumb",
                 (unsigned)(h >> 56), (unsigned long long)h, edge);
        return diskCacheDir + buf;
    }

    // Pre-sharding layout, kept so an existing cache is not thrown away.
    std::string legacyCachePath(const std::string& path, int edge) const {
        char buf[64];
        snprintf(buf, sizeof(buf), "/%016llx_%d.sthumb",
                 (unsigned long long)hashPath(path), edge);
        return diskCacheDir + buf;
    }

    // Marker recording that this file cannot be decoded, so it is not retried on
    // every launch. Mirrors the freedesktop cache's fail/ directory.
    std::string failMarkerPath(const std::string& path) const {
        uint64_t h = hashPath(path);
        char buf[80];
        snprintf(buf, sizeof(buf), "/%02x/%016llx.fail",
                 (unsigned)(h >> 56), (unsigned long long)h);
        return diskCacheDir + buf;
    }

    // Bumped whenever decoding changes in a way that invalidates cached pixels
    // (v2: GIF frames are now properly composited).
    static constexpr int32_t kDiskCacheVersion = 2;

    // Container: "STHB" | ver | origW | origH | mtime | fileSize | jpegLen | jpeg bytes
    struct DiskHeader {
        char magic[4];
        int32_t version;
        int32_t origW;
        int32_t origH;
        int64_t mtime;
        int64_t fileSize;
        int32_t jpegLen;
        int32_t hasAlpha;   // payload is PNG when set, JPEG otherwise
    };

    // A cached thumbnail at a LARGER tier can serve a smaller one: decoding a
    // 384px cache file and shrinking it costs ~2-3 ms, where re-decoding the
    // original costs ~16 ms. This is what makes changing zoom (and therefore
    // tier) cheap instead of re-processing the whole library.
    bool loadFromDisk(const std::string& path, int edge, DecodedThumb& out) const {
        if (loadFromDiskExact(path, edge, out)) return true;

        for (int t : tiers) {
            if (t <= edge) continue;
            if (!loadFromDiskExact(path, t, out)) continue;

            // Shrink the larger cached image down to the requested tier.
            int tw = out.w, th = out.h;
            if (out.w > edge || out.h > edge) {
                if (out.w >= out.h) {
                    tw = edge;
                    th = std::max(1, (int)((int64_t)out.h * edge / out.w));
                } else {
                    th = edge;
                    tw = std::max(1, (int)((int64_t)out.w * edge / out.h));
                }
            }
            if (tw != out.w || th != out.h) {
                unsigned char* src = (unsigned char*)malloc(out.data.size());
                if (src) {
                    memcpy(src, out.data.data(), out.data.size());
                    unsigned char* dst = silvercodec::resizeRGBA(src, out.w, out.h, tw, th);
                    if (dst) {
                        out.data.assign(dst, dst + (size_t)tw * th * 4);
                        out.w = tw;
                        out.h = th;
                        silvercodec::freePixels(dst);
                    }
                }
            }
            out.edge = edge;
            return true;
        }
        return false;
    }

    bool loadFromDiskExact(const std::string& path, int edge, DecodedThumb& out) const {
        if (!diskCacheEnabled) return false;

        struct stat srcSt;
        if (stat(path.c_str(), &srcSt) != 0) return false;

        std::string cachePath = diskCachePath(path, edge);
        FILE* fp = fopen(cachePath.c_str(), "rb");
        if (!fp) {
            // Adopt an entry written by the old flat layout instead of redoing
            // work the user already paid for.
            std::string legacy = legacyCachePath(path, edge);
            std::error_code ec;
            if (std::filesystem::exists(legacy, ec)) {
                std::filesystem::rename(legacy, cachePath, ec);
                if (!ec) fp = fopen(cachePath.c_str(), "rb");
            }
        }
        if (!fp) return false;

        DiskHeader hdr{};
        if (fread(&hdr, sizeof(hdr), 1, fp) != 1 ||
            memcmp(hdr.magic, "STHB", 4) != 0 || hdr.version != kDiskCacheVersion ||
            hdr.mtime != (int64_t)srcSt.st_mtime || hdr.fileSize != (int64_t)srcSt.st_size ||
            hdr.jpegLen <= 0 || hdr.jpegLen > 32 * 1024 * 1024) {
            fclose(fp);
            return false;
        }

        std::vector<unsigned char> jpeg((size_t)hdr.jpegLen);
        size_t got = fread(jpeg.data(), 1, jpeg.size(), fp);
        fclose(fp);
        if (got != jpeg.size()) return false;

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load_from_memory(jpeg.data(), (int)jpeg.size(), &w, &h, &comp, 4);
        if (!pixels) return false;

        out.path = path;
        out.w = w;
        out.h = h;
        out.edge = edge;
        out.origW = hdr.origW;
        out.origH = hdr.origH;
        out.data.assign(pixels, pixels + (size_t)w * h * 4);
        stbi_image_free(pixels);
        return true;
    }

    void storeToDisk(const std::string& path, int edge, const DecodedThumb& thumb) const {
        if (!diskCacheEnabled || thumb.data.empty()) return;

        struct stat srcSt;
        if (stat(path.c_str(), &srcSt) != 0) return;

        // JPEG throws away alpha, so transparent thumbnails (PNG, SVG, ICO) are
        // stored as PNG instead.
        bool hasAlpha = false;
        for (size_t i = 3; i < thumb.data.size(); i += 4) {
            if (thumb.data[i] != 255) { hasAlpha = true; break; }
        }

        std::vector<unsigned char> jpeg;
        auto sink = [](void* ctx, void* data, int size) {
            auto* vec = (std::vector<unsigned char>*)ctx;
            const unsigned char* p = (const unsigned char*)data;
            vec->insert(vec->end(), p, p + size);
        };
        int wrote = hasAlpha
            ? stbi_write_png_to_func(sink, &jpeg, thumb.w, thumb.h, 4, thumb.data.data(), thumb.w * 4)
            : stbi_write_jpg_to_func(sink, &jpeg, thumb.w, thumb.h, 4, thumb.data.data(), diskCacheQuality);
        if (!wrote || jpeg.empty()) return;

        std::string finalPath = diskCachePath(path, edge);
        std::string tmpPath = finalPath + ".tmp";

        FILE* fp = fopen(tmpPath.c_str(), "wb");
        if (!fp) return;

        DiskHeader hdr{};
        memcpy(hdr.magic, "STHB", 4);
        hdr.version = kDiskCacheVersion;
        hdr.origW = thumb.origW;
        hdr.origH = thumb.origH;
        hdr.mtime = (int64_t)srcSt.st_mtime;
        hdr.fileSize = (int64_t)srcSt.st_size;
        hdr.jpegLen = (int32_t)jpeg.size();
        hdr.hasAlpha = hasAlpha ? 1 : 0;

        bool ok = fwrite(&hdr, sizeof(hdr), 1, fp) == 1 &&
                  fwrite(jpeg.data(), 1, jpeg.size(), fp) == jpeg.size();
        fclose(fp);

        std::error_code ec;
        if (ok) std::filesystem::rename(tmpPath, finalPath, ec);
        else    std::filesystem::remove(tmpPath, ec);
    }

    // -------------------------------------------------------------------------
    // Request API
    // -------------------------------------------------------------------------
    // Snap to one of the configured tiers so zooming reuses decoded textures
    // instead of re-decoding the library on every pinch step.
    int quantizeEdge(float cellPixels) const {
        if (tiers.empty()) return silver::defaults::thumbnailEdge;
        float need = cellPixels * pixelScale * tierHeadroom;
        for (int t : tiers) {
            if (need <= (float)t) return t;
        }
        return tiers.back();
    }

    void requestThumbnail(const std::string& path, bool highPriority = false, int edge = 512) {
        std::lock_guard<std::mutex> lock(queueLock);
        if (highPriority) {
            // Pinned requests (selection, fullscreen) survive viewport churn.
            auto pit = std::find_if(pinned.begin(), pinned.end(),
                                    [&](const ThumbRequest& r) { return r.path == path; });
            if (pit == pinned.end()) pinned.push_back(ThumbRequest{ path, edge });
            else pit->edge = std::max(pit->edge, edge);
            if (pinned.size() > 8) pinned.erase(pinned.begin());
        }
        enqueueLocked(path, edge, highPriority);
    }

    // Replaces the pending queue with exactly what is on screen right now.
    // Anything scrolled out of view stops competing for decode threads.
    void requestVisibleSet(const std::vector<std::string>& paths, int edge) {
        std::lock_guard<std::mutex> lock(queueLock);

        // Anything still queued is about to be dropped, so clear its pending
        // flag - otherwise those entries read as permanently "in progress" and
        // the diagnostics lie about what the pipeline is doing.
        for (const auto& queued : queuedSet) {
            auto it = cache.find(queued);
            if (it != cache.end() && !it->second.ready) it->second.inProgress = false;
        }

        loadQueue.clear();
        queuedSet.clear();
        recountOutstanding();

        pinned.erase(std::remove_if(pinned.begin(), pinned.end(), [&](const ThumbRequest& r) {
            auto it = cache.find(r.path);
            return it != cache.end() && it->second.ready && it->second.loadedEdge >= r.edge;
        }), pinned.end());

        for (const auto& r : pinned) {
            enqueueLocked(r.path, r.edge, false);
        }
        for (const auto& p : paths) {
            enqueueLocked(p, edge, false);
        }
        recountOutstanding();
        cv.notify_all();
    }

    void preloadFolder(const std::vector<std::string>& files, int currentIdx, int edge = 512) {
        std::lock_guard<std::mutex> lock(queueLock);
        if (files.empty()) return;

        // Prioritize items near currentIdx outward
        int n = (int)files.size();
        std::vector<int> indices;
        indices.reserve(n);
        if (currentIdx >= 0 && currentIdx < n) indices.push_back(currentIdx);
        for (int d = 1; d < n; ++d) {
            if (currentIdx + d < n) indices.push_back(currentIdx + d);
            if (currentIdx - d >= 0) indices.push_back(currentIdx - d);
        }

        for (int idx : indices) {
            enqueueLocked(files[idx], edge, false);
        }
        cv.notify_all();
    }

    void workerLoop() {
        while (running) {
            ThumbRequest req;
            {
                std::unique_lock<std::mutex> lock(queueLock);
                cv.wait(lock, [this]() {
                    return !running || !loadQueue.empty() ||
                           (!prewarmQueue.empty() && prewarmActive.load() < maxPrewarmWorkers);
                });

                if (!running) break;

                if (!loadQueue.empty()) {
                    // On-screen work always wins over background pre-generation.
                    req = loadQueue.front();
                    loadQueue.pop_front();
                    queuedSet.erase(req.path);
                    inFlightSet.insert(req.path);
                    recountOutstanding();
                } else if (!prewarmQueue.empty() && prewarmActive.load() < maxPrewarmWorkers) {
                    req = prewarmQueue.front();
                    prewarmQueue.pop_front();
                    prewarmRemaining.store((int)prewarmQueue.size());
                    prewarmActive++;
                } else {
                    continue;
                }
            }

            if (req.path.empty()) continue;

            // Always report back, even on failure, so the main thread can clear
            // the in-progress flag without touching the cache from here.
            struct FlightGuard {
                ThumbnailManager* mgr;
                std::string path;
                bool active = true;
                ~FlightGuard() {
                    if (!active) return;
                    std::lock_guard<std::mutex> lock(mgr->queueLock);
                    mgr->inFlightSet.erase(path);
                    mgr->recountOutstanding();
                }
            } guard{ this, req.path, !req.diskOnly };

            if (req.diskOnly) {
                // Background pre-generation: build the cache file and stop there,
                // nothing gets uploaded or held in RAM.
                struct PrewarmSlot {
                    ThumbnailManager* mgr;
                    ~PrewarmSlot() {
                        // Take the lock before notifying: the wait predicate reads
                        // prewarmActive under it, so an unlocked notify could slip
                        // between another worker's predicate check and its wait.
                        {
                            std::lock_guard<std::mutex> lock(mgr->queueLock);
                            mgr->prewarmActive--;
                        }
                        mgr->cv.notify_one();
                    }
                } slot{ this };

                if (hasFreshDiskEntry(req.path, masterTier)) continue;
                if (isKnownBad(req.path)) continue;

                // Prefer converting the desktop's thumbnail over re-decoding.
                DecodedThumb shared;
                if (loadFromSharedCache(req.path, masterTier, shared)) {
                    storeToDisk(req.path, masterTier, shared);
                    emitPreview(shared);
                    statFromShared++;
                    statPrewarmDone++;
                    continue;
                }

                int w = 0, h = 0, origW = 0, origH = 0;
                unsigned char* pixels = silvercodec::loadThumbRGBA(req.path, masterTier, &w, &h, &origW, &origH);
                if (!pixels) {
                    markBad(req.path);
                    continue;
                }
                int orientation = (silvercodec::detectFormat(req.path) == silvercodec::FMT_JPEG)
                                      ? silvercodec::readExifOrientation(req.path) : 1;
                if (orientation > 1) {
                    pixels = silvercodec::applyOrientation(pixels, w, h, orientation, &w, &h);
                    if (orientation >= 5) std::swap(origW, origH);
                }
                DecodedThumb cold;
                cold.path = req.path;
                cold.w = w; cold.h = h; cold.edge = masterTier;
                cold.origW = origW > 0 ? origW : w;
                cold.origH = origH > 0 ? origH : h;
                cold.data.assign(pixels, pixels + (size_t)w * h * 4);
                silvercodec::freePixels(pixels);
                storeToDisk(req.path, masterTier, cold);
                emitPreview(cold);
                statPrewarmDone++;
                continue;
            }

            // Everything is produced from one master image, then shrunk. That
            // way a photo is decoded from its original exactly once, ever,
            // whatever zoom levels the user visits.
            DecodedThumb dt;
            if (loadFromDisk(req.path, masterTier, dt)) {
                statFromCache++;
                deriveTo(dt, req.edge);
            } else if (loadFromSharedCache(req.path, masterTier, dt)) {
                // Someone's file manager already did this work.
                statFromShared++;
                storeToDisk(req.path, masterTier, dt);
                deriveTo(dt, req.edge);
            } else if (isKnownBad(req.path)) {
                publishFailure(req.path, req.edge);
                continue;
            } else {
                auto decodeStart = std::chrono::steady_clock::now();
                int w = 0, h = 0, origW = 0, origH = 0;
                unsigned char* pixels = silvercodec::loadThumbRGBA(req.path, masterTier, &w, &h, &origW, &origH);
                if (!pixels) {
                    markBad(req.path);   // never attempt this file again
                    publishFailure(req.path, req.edge);
                    continue;
                }

                // Honour EXIF orientation so phone shots are never sideways.
                // Only JPEG carries the APP1 segment, so do not open every other
                // file just to read two bytes and give up.
                int orientation = (silvercodec::detectFormat(req.path) == silvercodec::FMT_JPEG)
                                      ? silvercodec::readExifOrientation(req.path) : 1;
                if (orientation > 1) {
                    pixels = silvercodec::applyOrientation(pixels, w, h, orientation, &w, &h);
                    if (orientation >= 5) std::swap(origW, origH);
                }

                dt.path = req.path;
                dt.w = w;
                dt.h = h;
                dt.edge = masterTier;
                dt.origW = origW > 0 ? origW : w;
                dt.origH = origH > 0 ? origH : h;
                dt.data.assign(pixels, pixels + (size_t)w * h * 4);
                silvercodec::freePixels(pixels);

                // Persist the master, then shrink the copy we are about to show.
                storeToDisk(req.path, masterTier, dt);
                emitPreview(dt);
                deriveTo(dt, req.edge);

                statFromOriginal++;
                statDecodeMicros += std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - decodeStart).count();
            }

            {
                // Claim the upload slot before publishing, so the next frame's
                // visible-set pass does not queue this path all over again.
                std::lock_guard<std::mutex> lock(queueLock);
                pendingUpload.insert(dt.path);
                recountOutstanding();
            }
            {
                std::lock_guard<std::mutex> rlock(readyLock);
                readyQueue.push_back(std::move(dt));
            }
            if (onWorkReady) onWorkReady();
        }
    }

    void publishFailure(const std::string& path, int edge) {
        {
            std::lock_guard<std::mutex> lock(queueLock);
            pendingUpload.insert(path);
            recountOutstanding();
        }
        DecodedThumb failed;
        failed.path = path;
        failed.edge = edge;
        std::lock_guard<std::mutex> rlock(readyLock);
        readyQueue.push_back(std::move(failed));
    }

    void updateGL(float dt = 0.0f) {
        if (dt > 0.0f) lastFrameDt = dt;
        frameCounter++;

        // Upload a bounded number of ready thumbnails per frame so a big batch
        // never stalls the render thread.
        int uploaded = 0;
        while (uploaded < uploadsPerFrame) {
            DecodedThumb dt;
            {
                std::lock_guard<std::mutex> rlock(readyLock);
                if (readyQueue.empty()) break;
                dt = std::move(readyQueue.front());
                readyQueue.pop_front();
            }
            {
                std::lock_guard<std::mutex> lock(queueLock);
                pendingUpload.erase(dt.path);
                recountOutstanding();
            }

            if (dt.data.empty() || dt.w <= 0 || dt.h <= 0) {
                // Decode failed - remember that, so it is never queued again and
                // never holds up the startup prime.
                ThumbnailItem& bad = cache[dt.path];
                if (!bad.ready) {
                    bad.inProgress = false;
                    bad.failed = true;
                }
                continue;
            }

            ThumbnailItem& item = cache[dt.path];
            if (item.texId) {
                glDeleteTextures(1, &item.texId);
                item.texId = 0;
                if (residentTextures) residentTextures--;
                size_t old = textureBytes(item.width, item.height);
                residentBytes = (residentBytes > old) ? residentBytes - old : 0;
            }

            GLuint tid = 0;
            glGenTextures(1, &tid);
            glBindTexture(GL_TEXTURE_2D, tid);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dt.w, dt.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, dt.data.data());
            // No mipmaps. The tier is chosen to match the on-screen cell size,
            // so the texture is already near 1:1 - a mip chain would cost 33%
            // more memory and a glGenerateMipmap per upload to be sampled from
            // level 0 anyway.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            item.texId = tid;
            item.width = dt.w;
            item.height = dt.h;
            item.loadedEdge = dt.edge;
            item.origWidth = dt.origW;
            item.origHeight = dt.origH;
            item.ready = true;
            item.inProgress = false;
            item.failed = false;
            item.lastUsedFrame = frameCounter;
            residentTextures++;
            residentBytes += textureBytes(dt.w, dt.h);

            uploaded++;
        }

        evictIfNeeded();

        // Smooth filmstrip scroll interpolation
        silveranim::drivePos(scrollOffset, scrollVelocity, targetScrollOffset,
                             silveranim::rates().chFilmstrip, lastFrameDt);
    }

    // Mark a thumbnail as drawn this frame (keeps it out of the eviction pool).
    void touch(const std::string& path) {
        auto it = cache.find(path);
        if (it != cache.end()) it->second.lastUsedFrame = frameCounter;
    }

    // Box-average the decoded thumbnail down to 8x8 RGB. Costs almost nothing
    // next to the decode that just happened, and only runs on a fresh decode -
    // never when serving from cache.
    void emitPreview(const DecodedThumb& thumb) {
        if (thumb.data.empty() || thumb.w <= 0 || thumb.h <= 0) return;

        std::vector<unsigned char> out((size_t)kPreviewDim * kPreviewDim * 3, 0);
        for (int cy = 0; cy < kPreviewDim; ++cy) {
            int y0 = (int)((int64_t)cy * thumb.h / kPreviewDim);
            int y1 = std::max(y0 + 1, (int)((int64_t)(cy + 1) * thumb.h / kPreviewDim));
            for (int cx = 0; cx < kPreviewDim; ++cx) {
                int x0 = (int)((int64_t)cx * thumb.w / kPreviewDim);
                int x1 = std::max(x0 + 1, (int)((int64_t)(cx + 1) * thumb.w / kPreviewDim));

                uint64_t acc[3] = { 0, 0, 0 };
                uint64_t n = 0;
                for (int y = y0; y < y1 && y < thumb.h; ++y) {
                    const unsigned char* row = thumb.data.data() + ((size_t)y * thumb.w) * 4;
                    for (int x = x0; x < x1 && x < thumb.w; ++x) {
                        acc[0] += row[(size_t)x * 4 + 0];
                        acc[1] += row[(size_t)x * 4 + 1];
                        acc[2] += row[(size_t)x * 4 + 2];
                        n++;
                    }
                }
                if (!n) continue;
                size_t o = ((size_t)cy * kPreviewDim + cx) * 3;
                out[o + 0] = (unsigned char)(acc[0] / n);
                out[o + 1] = (unsigned char)(acc[1] / n);
                out[o + 2] = (unsigned char)(acc[2] / n);
            }
        }

        std::lock_guard<std::mutex> lock(previewLock);
        pendingPreviews.emplace_back(thumb.path, std::move(out));
    }

    static size_t textureBytes(int w, int h) {
        return (size_t)std::max(0, w) * (size_t)std::max(0, h) * 4u;
    }

    void evictIfNeeded() {
        // Fast path: nothing to do, and no cache walk. This runs every frame and
        // the cache can hold tens of thousands of entries.
        if (residentBytes <= maxResidentBytes) return;

        // Least-recently-drawn first, which is the standard policy for a
        // streaming texture pool.
        std::vector<std::pair<uint64_t, std::string>> victims;
        victims.reserve(residentTextures);
        for (const auto& kv : cache) {
            if (kv.second.texId) victims.emplace_back(kv.second.lastUsedFrame, kv.first);
        }
        std::sort(victims.begin(), victims.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Free down to three quarters of the budget so this does not run every
        // frame once the pool is full.
        size_t target = maxResidentBytes * 3 / 4;
        for (size_t i = 0; i < victims.size() && residentBytes > target; ++i) {
            // Never drop something drawn in the last few frames.
            if (frameCounter - victims[i].first < 4) break;
            auto it = cache.find(victims[i].second);
            if (it == cache.end() || !it->second.texId) continue;

            size_t bytes = textureBytes(it->second.width, it->second.height);
            glDeleteTextures(1, &it->second.texId);
            cache.erase(it);
            if (residentTextures) residentTextures--;
            residentBytes = (residentBytes > bytes) ? residentBytes - bytes : 0;
        }
    }

    struct FailMarker {
        char magic[4];
        int32_t version;
        int64_t mtime;
        int64_t fileSize;
    };

    // Has this file already been proven undecodable (by us, or by the desktop)?
    bool isKnownBad(const std::string& path) const {
        struct stat srcSt;
        if (stat(path.c_str(), &srcSt) != 0) return false;

        if (diskCacheEnabled) {
            FILE* fp = fopen(failMarkerPath(path).c_str(), "rb");
            if (fp) {
                FailMarker m{};
                bool match = fread(&m, sizeof(m), 1, fp) == 1 &&
                             memcmp(m.magic, "SFAI", 4) == 0 &&
                             m.mtime == (int64_t)srcSt.st_mtime &&
                             m.fileSize == (int64_t)srcSt.st_size;
                fclose(fp);
                if (match) return true;
            }
        }

        // The desktop keeps its own list of files it could not thumbnail; there
        // is no point rediscovering them one by one.
        if (useSharedCache && silverxdg::isKnownFailure(path)) return true;
        return false;
    }

    void markBad(const std::string& path) const {
        if (!diskCacheEnabled) return;
        struct stat srcSt;
        if (stat(path.c_str(), &srcSt) != 0) return;

        FILE* fp = fopen(failMarkerPath(path).c_str(), "wb");
        if (!fp) return;
        FailMarker m{};
        memcpy(m.magic, "SFAI", 4);
        m.version = 1;
        m.mtime = (int64_t)srcSt.st_mtime;
        m.fileSize = (int64_t)srcSt.st_size;
        fwrite(&m, sizeof(m), 1, fp);
        fclose(fp);
    }

    // Shrink an in-memory thumbnail to `edge`. Cheap compared with decoding.
    static void deriveTo(DecodedThumb& thumb, int edge) {
        if (thumb.w <= edge && thumb.h <= edge) {
            thumb.edge = edge;
            return;
        }

        int tw, th;
        if (thumb.w >= thumb.h) {
            tw = edge;
            th = std::max(1, (int)((int64_t)thumb.h * edge / thumb.w));
        } else {
            th = edge;
            tw = std::max(1, (int)((int64_t)thumb.w * edge / thumb.h));
        }

        unsigned char* src = (unsigned char*)malloc(thumb.data.size());
        if (!src) { thumb.edge = edge; return; }
        memcpy(src, thumb.data.data(), thumb.data.size());

        unsigned char* dst = silvercodec::resizeRGBA(src, thumb.w, thumb.h, tw, th);
        if (dst) {
            thumb.data.assign(dst, dst + (size_t)tw * th * 4);
            thumb.w = tw;
            thumb.h = th;
            silvercodec::freePixels(dst);
        }
        thumb.edge = edge;
    }

    // Convert a freedesktop shared-cache PNG into a thumbnail at `edge`.
    bool loadFromSharedCache(const std::string& path, int edge, DecodedThumb& out) const {
        if (!useSharedCache) return false;

        silverxdg::Hit hit;
        if (!silverxdg::find(path, edge, hit)) return false;

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load(hit.file.c_str(), &w, &h, &comp, 4);
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels) stbi_image_free(pixels);
            return false;
        }

        int tw = w, th = h;
        if (w > edge || h > edge) {
            if (w >= h) { tw = edge; th = std::max(1, (int)((int64_t)h * edge / w)); }
            else        { th = edge; tw = std::max(1, (int)((int64_t)w * edge / h)); }
        }

        if (tw != w || th != h) {
            unsigned char* copy = (unsigned char*)malloc((size_t)w * h * 4);
            if (!copy) { stbi_image_free(pixels); return false; }
            memcpy(copy, pixels, (size_t)w * h * 4);
            stbi_image_free(pixels);
            unsigned char* scaled = silvercodec::resizeRGBA(copy, w, h, tw, th);
            if (!scaled) return false;
            out.data.assign(scaled, scaled + (size_t)tw * th * 4);
            silvercodec::freePixels(scaled);
        } else {
            out.data.assign(pixels, pixels + (size_t)w * h * 4);
            stbi_image_free(pixels);
        }

        out.path = path;
        out.w = tw;
        out.h = th;
        out.edge = edge;
        // The shared thumbnail is already scaled, so the true source size is not
        // recoverable from it; the indexer supplies real dimensions separately.
        out.origW = tw;
        out.origH = th;
        return true;
    }

    // True when a valid cache file exists for this path at the requested tier or
    // any larger one - a larger entry can always be shrunk, so pre-generating
    // the smaller tier again would be wasted work.
    bool hasFreshDiskEntry(const std::string& path, int edge) const {
        // Only the master is ever written now, so one stat answers this. The
        // old version probed up to five tier files per photo, which for this
        // library was ~55k pointless stat calls per pre-generation pass.
        return hasFreshDiskEntryExact(path, edge);
    }

    bool hasFreshDiskEntryExact(const std::string& path, int edge) const {
        if (!diskCacheEnabled) return false;
        struct stat srcSt;
        if (stat(path.c_str(), &srcSt) != 0) return false;

        FILE* fp = fopen(diskCachePath(path, edge).c_str(), "rb");
        if (!fp) return false;
        DiskHeader hdr{};
        bool ok = fread(&hdr, sizeof(hdr), 1, fp) == 1 &&
                  memcmp(hdr.magic, "STHB", 4) == 0 && hdr.version == kDiskCacheVersion &&
                  hdr.mtime == (int64_t)srcSt.st_mtime &&
                  hdr.fileSize == (int64_t)srcSt.st_size;
        fclose(fp);
        return ok;
    }

    // Queue the whole library for background cache generation at the lowest
    // priority, so later scrolling never has to decode anything.
    void prewarmLibrary(const std::vector<std::string>& paths, int /*edge*/) {
        std::lock_guard<std::mutex> lock(queueLock);
        int edge = masterTier;   // the queue only ever produces masters
        prewarmEdge = edge;
        prewarmQueue.clear();
        for (const auto& p : paths) {
            prewarmQueue.push_back(ThumbRequest{ p, edge, true });
        }
        prewarmRemaining.store((int)prewarmQueue.size());
        cv.notify_all();
    }

    // Remove cache files for tiers that are no longer written.
    //
    // Switching to a single master leaves the previous per-tier files behind -
    // for this library that was 49k files and roughly 500 MB of dead weight.
    // Runs once, in the background, and is skipped if a marker says it is done.
    void pruneLegacyTiers() {
        if (!diskCacheEnabled || diskCacheDir.empty()) return;

        std::string marker = diskCacheDir + "/.master-only";
        std::error_code ec;
        if (std::filesystem::exists(marker, ec)) return;

        std::string suffix = "_" + std::to_string(masterTier) + ".sthumb";
        size_t removed = 0;
        uintmax_t freed = 0;

        std::filesystem::recursive_directory_iterator it(
            diskCacheDir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return;

        for (const auto& entry : it) {
            if (!running) return;
            std::error_code ec2;
            if (!entry.is_regular_file(ec2)) continue;

            const std::string name = entry.path().filename().string();
            if (name.size() < 8 || name.rfind(".sthumb") == std::string::npos) continue;
            if (name.size() >= suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                continue;   // this is a master
            }

            uintmax_t sz = entry.file_size(ec2);
            if (std::filesystem::remove(entry.path(), ec2)) {
                removed++;
                if (!ec2) freed += sz;
            }
        }

        FILE* fp = fopen(marker.c_str(), "wb");
        if (fp) { fputs("1\n", fp); fclose(fp); }

        if (removed) {
            fprintf(stderr, "[SilverGallery] pruned %zu stale thumbnail files (%.0f MB)\n",
                    removed, (double)freed / (1024.0 * 1024.0));
        }
    }

    void cancelPrewarm() {
        std::lock_guard<std::mutex> lock(queueLock);
        prewarmQueue.clear();
        prewarmRemaining.store(0);
    }

    // Work still in flight that will change the screen when it lands.
    // Background pre-generation is excluded: it never touches a texture, so it
    // must not keep the app awake.
    // Lock-free: this is polled every frame by the idle check, and taking two
    // mutexes to answer a yes/no question is wasteful.
    std::atomic<int> outstandingWork{0};

    bool hasPendingWork() const {
        return outstandingWork.load(std::memory_order_relaxed) > 0;
    }

    void recountOutstanding() {
        outstandingWork.store((int)(loadQueue.size() + inFlightSet.size() + pendingUpload.size()),
                              std::memory_order_relaxed);
    }

    // Called from a worker when a decode lands, so a sleeping main loop wakes
    // up instead of waiting out its timeout.
    std::function<void()> onWorkReady;

    // Blurred placeholders produced as a by-product of decoding, drained by the
    // main thread and written to the index in batches.
    static const int kPreviewDim = 8;
    std::mutex previewLock;
    std::vector<std::pair<std::string, std::vector<unsigned char>>> pendingPreviews;

    void drainPreviews(std::vector<std::pair<std::string, std::vector<unsigned char>>>& out) {
        std::lock_guard<std::mutex> lock(previewLock);
        if (pendingPreviews.empty()) return;
        out.insert(out.end(), std::make_move_iterator(pendingPreviews.begin()),
                   std::make_move_iterator(pendingPreviews.end()));
        pendingPreviews.clear();
    }

    int prewarmTier() const { return prewarmEdge; }
    int masterTierSize() const { return masterTier; }
    int prewarmPending() const { return prewarmRemaining.load(); }

    // Counters for SILVER_THUMB_STATS=1
    std::atomic<int> statFromOriginal{0};   // full decode of the source image
    std::atomic<int> statFromCache{0};      // served by the on-disk cache
    std::atomic<int> statFromShared{0};     // served by the desktop's shared cache
    std::atomic<int> statPrewarmDone{0};
    std::atomic<long long> statDecodeMicros{0};

    // Set by the grid each frame: how many tiles wanted a thumbnail, and how
    // many of those actually resolved to a live texture.
    int lastVisibleTiles = 0;
    int lastResolvedTiles = 0;

    std::string statsLine() {
        int orig = statFromOriginal.load();
        int cached = statFromCache.load();
        long long micros = statDecodeMicros.load();
        // Cache state, counted on the main thread where it is owned.
        int ready = 0, inProgress = 0, failedCount = 0;
        for (const auto& kv : cache) {
            if (kv.second.ready) ready++;
            if (kv.second.inProgress) inProgress++;
            if (kv.second.failed) failedCount++;
        }

        size_t queued = 0, inflight = 0, readyPending = 0;
        {
            std::lock_guard<std::mutex> lock(queueLock);
            queued = loadQueue.size();
            inflight = inFlightSet.size();
        }
        {
            std::lock_guard<std::mutex> rlock(readyLock);
            readyPending = readyQueue.size();
        }

        char buf[512];
        snprintf(buf, sizeof(buf),
                 "thumbs: tiles %d/%d drawn | vram %zu/%zu MB in %zu tex | ready=%d busy=%d bad=%d | "
                 "queue=%zu inflight=%zu upload=%zu | %d decoded (%.1f ms avg), %d disk, %d desktop | "
                 "prewarm %d done / %d left",
                 lastResolvedTiles, lastVisibleTiles,
                 residentBytes / (1024 * 1024), maxResidentBytes / (1024 * 1024), residentTextures,
                 ready, inProgress, failedCount,
                 queued, inflight, readyPending,
                 orig, orig ? (float)micros / 1000.0f / (float)orig : 0.0f,
                 cached, statFromShared.load(),
                 statPrewarmDone.load(), prewarmRemaining.load());
        return buf;
    }

    // "Settled" means every path either has a texture at the requested tier or
    // has been proven undecodable - a broken file must not stall startup.
    bool allReady(const std::vector<std::string>& paths, int edge) {
        for (const auto& p : paths) {
            auto it = cache.find(p);
            if (it == cache.end()) return false;
            if (it->second.failed) continue;
            if (!it->second.ready || it->second.loadedEdge < edge) return false;
        }
        return true;
    }

    // Blocking startup pass: decode/upload the first screenful before the window
    // is ever presented, so the gallery opens already showing photos.
    // Returns how many textures were ready when it finished.
    int primeStartup(const std::vector<std::string>& paths, int edge, int budgetMillis) {
        if (paths.empty()) return 0;
        requestVisibleSet(paths, edge);

        auto start = std::chrono::steady_clock::now();
        while (true) {
            updateGL();
            if (allReady(paths, edge)) break;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start).count();
            if (elapsed >= budgetMillis) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        int ready = 0;
        for (const auto& p : paths) {
            auto it = cache.find(p);
            if (it != cache.end() && it->second.ready) ready++;
        }
        return ready;
    }

    void centerOnIndex(int idx, int total, float windowW, float thumbW, float thumbGap) {
        float itemStep = thumbW + thumbGap;
        float centerPos = idx * itemStep + thumbW * 0.5f;
        targetScrollOffset = centerPos - windowW * 0.5f;

        float maxScroll = std::max(0.0f, total * itemStep - windowW + thumbGap);
        if (targetScrollOffset < 0.0f) targetScrollOffset = 0.0f;
        if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
    }

private:
    // Caller must hold queueLock.
    void enqueueLocked(const std::string& path, int edge, bool highPriority) {
        auto it = cache.find(path);
        if (it != cache.end()) {
            if (it->second.ready && it->second.loadedEdge >= edge) return; // already resident
            if (it->second.failed) return;                                 // known bad file
        }

        if (queuedSet.count(path)) {
            if (highPriority) {
                auto qit = std::find_if(loadQueue.begin(), loadQueue.end(),
                                        [&](const ThumbRequest& r) { return r.path == path; });
                if (qit != loadQueue.end()) {
                    ThumbRequest req = *qit;
                    req.edge = std::max(req.edge, edge);
                    loadQueue.erase(qit);
                    loadQueue.push_front(req);
                }
            }
            return;
        }
        if (inFlightSet.count(path)) return;   // a worker is decoding it right now
        if (pendingUpload.count(path)) return; // decoded already, just waiting for upload

        if (it == cache.end()) {
            ThumbnailItem item;
            item.ready = false;
            item.inProgress = true;
            cache[path] = item;
        } else {
            it->second.inProgress = true;
        }

        queuedSet.insert(path);
        ThumbRequest req{ path, edge };
        if (highPriority) loadQueue.push_front(req);
        else              loadQueue.push_back(req);
        recountOutstanding();
        cv.notify_one();
    }
};
