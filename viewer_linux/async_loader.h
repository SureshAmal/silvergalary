#pragma once

#include "gl_loader.h"
#include "image_loader.h"
#include "silver_codec.h"
#include "silver_platform.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <ctime>

struct PreloadedImage {
    std::string path;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = nullptr;
    ImageMetadata meta;
    bool ready = false;
    bool inProgress = false;
    bool failed = false;

    ~PreloadedImage() {
        if (data) {
            stbi_image_free(data);
            data = nullptr;
        }
    }
};

class AsyncImagePreloader {
public:
    std::unordered_map<std::string, std::shared_ptr<PreloadedImage>> cache;
    std::deque<std::string> preloadQueue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;

    std::string currentLoadingPath = "";
    std::atomic<bool> isCurrentLoading{false};

    // Cap for decoded preview resolution (0 = decode at native size). Keeps a
    // 100 MP photo from turning into a 400 MB RGBA buffer for a preview pane.
    int maxDecodeEdge = 0;

    void init() {
        // Utilize 4-6 background workers dedicated to pre-loading and large image decoding
        unsigned int numCores = std::thread::hardware_concurrency();
        int workerCount = (int)std::clamp(numCores > 4 ? 6u : 2u, 2u, 6u);

        for (int i = 0; i < workerCount; ++i) {
            workers.emplace_back(&AsyncImagePreloader::workerLoop, this);
        }
    }

    ~AsyncImagePreloader() {
        running = false;
        m_cv.notify_all();
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
        clear();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        preloadQueue.clear();
        cache.clear();
        currentLoadingPath = "";
        isCurrentLoading = false;
    }

    void requestPrimaryImage(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        currentLoadingPath = path;

        auto it = cache.find(path);
        if (it != cache.end() && it->second->ready) {
            isCurrentLoading = false;
            return;
        }

        isCurrentLoading = true;
        if (it == cache.end()) {
            auto item = std::make_shared<PreloadedImage>();
            item->path = path;
            item->ready = false;
            item->inProgress = true;
            cache[path] = item;
        }

        auto qit = std::find(preloadQueue.begin(), preloadQueue.end(), path);
        if (qit != preloadQueue.end()) {
            preloadQueue.erase(qit);
        }
        // Put primary image at the very top of queue
        preloadQueue.push_front(path);
        m_cv.notify_all();
    }

    void updatePreloadTargets(const std::vector<std::string>& fileList, int currentIdx) {
        if (fileList.empty()) return;

        if (currentIdx < 0 || currentIdx >= (int)fileList.size()) return;
        const std::string& current = fileList[(size_t)currentIdx];

        std::lock_guard<std::mutex> lock(m_mutex);
        // Full-resolution RGBA is extremely expensive (a single 5338x3559
        // photo is ~72.5 MiB). Thumbnails already make neighboring navigation
        // instant, so retain only the image that is actually being displayed.
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->first != current) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }

        // Drop queued neighbor work left by an older navigation request. A
        // worker already decoding one may finish, but its pixels are discarded
        // because its cache entry was erased above.
        preloadQueue.erase(std::remove_if(preloadQueue.begin(), preloadQueue.end(),
                                          [&](const std::string& p) { return p != current; }),
                           preloadQueue.end());
    }

    std::shared_ptr<PreloadedImage> getIfReady(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = cache.find(path);
        if (it != cache.end() && it->second->ready) {
            if (path == currentLoadingPath) {
                isCurrentLoading = false;
            }
            return it->second;
        }
        return nullptr;
    }

    bool isLoadingPath(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return isCurrentLoading.load() && currentLoadingPath == path;
    }

    // uploadPixels copies into OpenGL, so keeping the same full-resolution CPU
    // allocation afterward only doubles memory. The shared_ptr returned by
    // getIfReady keeps it alive until the current upload call finishes.
    void discardUploadedPixels(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = cache.find(path);
        if (it != cache.end() && it->second->ready) cache.erase(it);
    }

    void workerLoop() {
        while (running) {
            std::string path = "";
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() {
                    return !running || !preloadQueue.empty();
                });

                if (!running) break;

                if (!preloadQueue.empty()) {
                    path = preloadQueue.front();
                    preloadQueue.pop_front();
                }
            }

            if (path.empty()) continue;

            // Load and decode full resolution image without holding lock
            ImageMetadata meta;
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                meta.filePath = path;
                meta.fileSizeBytes = st.st_size;
                meta.fileSizeFormatted = ImageTexture::formatBytes(st.st_size);

                auto formatTime = [](time_t t) -> std::string {
                    std::tm* tm = std::localtime(&t);
                    if (!tm) return "Unknown";
                    char dateBuf[64];
                    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", tm);
                    return std::string(dateBuf);
                };
                meta.modifiedTime = formatTime(st.st_mtime);
                meta.createdTime = formatTime(st.st_ctime);
                meta.accessedTime = formatTime(st.st_atime);

                char permBuf[16];
                snprintf(permBuf, sizeof(permBuf), "%c%c%c%c%c%c%c%c%c",
                         (st.st_mode & S_IRUSR) ? 'r' : '-',
                         (st.st_mode & S_IWUSR) ? 'w' : '-',
                         (st.st_mode & S_IXUSR) ? 'x' : '-',
                         (st.st_mode & S_IRGRP) ? 'r' : '-',
                         (st.st_mode & S_IWGRP) ? 'w' : '-',
                         (st.st_mode & S_IXGRP) ? 'x' : '-',
                         (st.st_mode & S_IROTH) ? 'r' : '-',
                         (st.st_mode & S_IWOTH) ? 'w' : '-',
                         (st.st_mode & S_IXOTH) ? 'x' : '-');
                meta.attributesStr = permBuf;

                struct passwd* pw = getpwuid(st.st_uid);
                meta.ownerStr = pw ? pw->pw_name : "user";

                meta.computerStr = silverplat::hostName();

                size_t lastSlash = path.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    meta.fileName = path.substr(lastSlash + 1);
                    meta.fileDirectory = path.substr(0, lastSlash);
                } else {
                    meta.fileName = path;
                    meta.fileDirectory = ".";
                }

                size_t lastDot = meta.fileName.find_last_of('.');
                std::string ext = "";
                if (lastDot != std::string::npos) {
                    ext = meta.fileName.substr(lastDot + 1);
                    for (char& c : ext) c = tolower(c);
                }
                if (ext == "jpg" || ext == "jpeg") {
                    meta.fileTypeStr = "JPG File";
                    meta.mimeType = "image/jpeg";
                } else if (ext == "png") {
                    meta.fileTypeStr = "PNG Image";
                    meta.mimeType = "image/png";
                } else if (ext == "webp") {
                    meta.fileTypeStr = "WEBP Image";
                    meta.mimeType = "image/webp";
                } else if (ext == "bmp") {
                    meta.fileTypeStr = "BMP Image";
                    meta.mimeType = "image/bmp";
                } else if (ext == "gif") {
                    meta.fileTypeStr = "GIF Image";
                    meta.mimeType = "image/gif";
                } else {
                    meta.fileTypeStr = "Image File";
                    meta.mimeType = "image/" + ext;
                }

                // Fast Header EXIF (64KB header)
                FILE* fp = fopen(path.c_str(), "rb");
                if (fp) {
                    size_t maxExifBytes = std::min((size_t)st.st_size, (size_t)65536);
                    std::vector<unsigned char> headerBuf(maxExifBytes);
                    size_t bytesRead = fread(headerBuf.data(), 1, maxExifBytes, fp);
                    fclose(fp);
                    if (bytesRead > 0 && meta.exif.parseFrom(headerBuf.data(), bytesRead) == PARSE_EXIF_SUCCESS) {
                        meta.hasExif = true;
                        if (meta.exif.Orientation >= 1 && meta.exif.Orientation <= 8) {
                            meta.exifOrientation = meta.exif.Orientation;
                        }
                        if (meta.exif.DateTimeOriginal.length() > 0) {
                            meta.createdTime = meta.exif.DateTimeOriginal;
                        }
                    }
                }

                int w = 0, h = 0, origComp = 4;
                unsigned char* raw = nullptr;
                if (maxDecodeEdge > 0) {
                    int ow = 0, oh = 0;
                    raw = silvercodec::loadThumbRGBA(path, maxDecodeEdge, &w, &h, &ow, &oh);
                } else {
                    raw = silvercodec::loadRGBA(path, &w, &h, &origComp);
                }
                if (raw && meta.exifOrientation > 1) {
                    raw = silvercodec::applyOrientation(raw, w, h, meta.exifOrientation, &w, &h);
                }
                if (raw) {
                    meta.width = w;
                    meta.height = h;
                    meta.channels = origComp;
                    meta.bitDepth = origComp * 8;
                    meta.megapixels = (float)(w * h) / 1000000.0f;
                    meta.dimensionsStr = std::to_string(w) + " x " + std::to_string(h);
                    int g = ImageTexture::gcd(w, h);
                    if (g > 0) {
                        char arBuf[64];
                        snprintf(arBuf, sizeof(arBuf), "%d:%d (%.2f:1)", w / g, h / g, (float)w / (float)h);
                        meta.aspectRatioStr = arBuf;
                    }

                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = cache.find(path);
                    if (it != cache.end()) {
                        it->second->width = w;
                        it->second->height = h;
                        it->second->channels = 4;
                        it->second->data = raw;
                        it->second->meta = meta;
                        it->second->ready = true;
                        it->second->inProgress = false;
                        it->second->failed = false;
                    } else {
                        silvercodec::freePixels(raw);
                    }
                } else {
                    // A corrupt or unsupported file is a completed failure, not
                    // an operation that remains "loading" forever.
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = cache.find(path);
                    if (it != cache.end()) {
                        it->second->inProgress = false;
                        it->second->failed = true;
                    }
                    if (path == currentLoadingPath) isCurrentLoading = false;
                    std::cerr << "[SilverViewer] Could not decode image: " << path << std::endl;
                }
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = cache.find(path);
                if (it != cache.end()) {
                    it->second->inProgress = false;
                    it->second->failed = true;
                }
                if (path == currentLoadingPath) isCurrentLoading = false;
            }
        }
    }
};
