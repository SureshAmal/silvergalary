#pragma once

#include "gl_loader.h"
#include <stb_image.h>
#ifndef STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#endif
#include <stb_image_resize.h>
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
#include <algorithm>
#include <cstring>

struct ThumbnailItem {
    GLuint texId = 0;
    int width = 0;
    int height = 0;
    bool ready = false;
    bool inProgress = false;
};

struct DecodedThumb {
    std::string path;
    int w = 0;
    int h = 0;
    std::vector<unsigned char> data;
};

class ThumbnailManager {
public:
    std::unordered_map<std::string, ThumbnailItem> cache;
    std::deque<std::string> loadQueue;
    std::unordered_set<std::string> queuedSet;
    std::deque<DecodedThumb> readyQueue;

    std::mutex queueLock;
    std::mutex readyLock;
    std::condition_variable cv;
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;

    float scrollOffset = 0.0f;
    float targetScrollOffset = 0.0f;

    void init() {
        // Utilize available CPU threads (up to 8 worker threads for parallel decoding)
        unsigned int numCores = std::thread::hardware_concurrency();
        int workerCount = (int)std::clamp(numCores > 2 ? numCores - 2 : 2, 2u, 8u);

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

    void requestThumbnail(const std::string& path, bool highPriority = false) {
        std::lock_guard<std::mutex> lock(queueLock);
        auto it = cache.find(path);
        if (it != cache.end()) {
            if (highPriority && !it->second.ready && queuedSet.find(path) != queuedSet.end()) {
                // Move to front of queue
                auto qit = std::find(loadQueue.begin(), loadQueue.end(), path);
                if (qit != loadQueue.end()) {
                    loadQueue.erase(qit);
                    loadQueue.push_front(path);
                }
            }
            return;
        }

        ThumbnailItem item;
        item.ready = false;
        item.inProgress = true;
        cache[path] = item;

        queuedSet.insert(path);
        if (highPriority) {
            loadQueue.push_front(path);
        } else {
            loadQueue.push_back(path);
        }
        cv.notify_one();
    }

    void preloadFolder(const std::vector<std::string>& files, int currentIdx) {
        std::lock_guard<std::mutex> lock(queueLock);
        if (files.empty()) return;

        // Prioritize items near currentIdx outward
        int n = (int)files.size();
        std::vector<int> indices;
        indices.reserve(n);
        indices.push_back(currentIdx);
        for (int d = 1; d < n; ++d) {
            if (currentIdx + d < n) indices.push_back(currentIdx + d);
            if (currentIdx - d >= 0) indices.push_back(currentIdx - d);
        }

        for (int idx : indices) {
            const std::string& path = files[idx];
            if (cache.find(path) == cache.end()) {
                ThumbnailItem item;
                item.ready = false;
                item.inProgress = true;
                cache[path] = item;
                queuedSet.insert(path);
                loadQueue.push_back(path);
            }
        }
        cv.notify_all();
    }

    void workerLoop() {
        while (running) {
            std::string path = "";
            {
                std::unique_lock<std::mutex> lock(queueLock);
                cv.wait(lock, [this]() {
                    return !running || !loadQueue.empty();
                });

                if (!running) break;

                if (!loadQueue.empty()) {
                    path = loadQueue.front();
                    loadQueue.pop_front();
                    queuedSet.erase(path);
                }
            }

            if (path.empty()) continue;

            int w = 0, h = 0, comp = 0;
            // Decode image from disk
            unsigned char* raw = stbi_load(path.c_str(), &w, &h, &comp, 4);
            if (raw) {
                // High-DPI crisp thumbnail target size (512px max dimension)
                int maxEdge = 512;
                int targetW = w;
                int targetH = h;
                if (w > maxEdge || h > maxEdge) {
                    if (w >= h) {
                        targetW = maxEdge;
                        targetH = std::max(1, (h * maxEdge) / w);
                    } else {
                        targetH = maxEdge;
                        targetW = std::max(1, (w * maxEdge) / h);
                    }
                }

                std::vector<unsigned char> resized(targetW * targetH * 4);
                if (targetW == w && targetH == h) {
                    std::memcpy(resized.data(), raw, (size_t)w * h * 4);
                } else {
                    // Studio-quality Mitchell downsampling filter (zero pixelation, anti-aliased)
                    stbir_resize_uint8(raw, w, h, 0, resized.data(), targetW, targetH, 0, 4);
                }
                stbi_image_free(raw);

                DecodedThumb dt;
                dt.path = path;
                dt.w = targetW;
                dt.h = targetH;
                dt.data = std::move(resized);

                std::lock_guard<std::mutex> rlock(readyLock);
                readyQueue.push_back(std::move(dt));
            }
        }
    }

    void updateGL() {
        // Upload up to 12 ready thumbnails to OpenGL textures per frame
        int uploaded = 0;
        while (uploaded < 12) {
            DecodedThumb dt;
            {
                std::lock_guard<std::mutex> rlock(readyLock);
                if (readyQueue.empty()) break;
                dt = std::move(readyQueue.front());
                readyQueue.pop_front();
            }

            GLuint tid = 0;
            glGenTextures(1, &tid);
            glBindTexture(GL_TEXTURE_2D, tid);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dt.w, dt.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, dt.data.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);

            ThumbnailItem& item = cache[dt.path];
            item.texId = tid;
            item.width = dt.w;
            item.height = dt.h;
            item.ready = true;
            item.inProgress = false;

            uploaded++;
        }

        // Smooth filmstrip scroll interpolation
        scrollOffset += (targetScrollOffset - scrollOffset) * 0.25f;
    }

    void centerOnIndex(int idx, int total, float windowW, float thumbW, float thumbGap) {
        float itemStep = thumbW + thumbGap;
        float centerPos = idx * itemStep + thumbW * 0.5f;
        targetScrollOffset = centerPos - windowW * 0.5f;

        float maxScroll = std::max(0.0f, total * itemStep - windowW + thumbGap);
        if (targetScrollOffset < 0.0f) targetScrollOffset = 0.0f;
        if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
    }
};
