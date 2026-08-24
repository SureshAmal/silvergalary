#pragma once

#include "gl_loader.h"
#include <easyexif/exif.h>
#include <stb_image.h>
#include "silver_codec.h"
#include "silver_platform.h"
#include <string>
#include <vector>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

struct ImageMetadata {
    std::string filePath;
    std::string fileName;
    std::string fileDirectory;
    size_t fileSizeBytes = 0;
    std::string fileSizeFormatted;
    std::string fileTypeStr = "Image";
    std::string mimeType = "image/jpeg";
    std::string attributesStr = "rw-r--r--";
    std::string modifiedTime;
    std::string createdTime;
    std::string accessedTime;

    int width = 0;
    int height = 0;
    int channels = 0;
    int bitDepth = 24;
    int dpiX = 96;
    int dpiY = 96;
    float megapixels = 0.0f;
    std::string aspectRatioStr;
    std::string dimensionsStr;

    std::string ownerStr = "user";
    std::string computerStr = "linux";
    std::string perceivedType = "Image";

    bool hasExif = false;
    easyexif::EXIFInfo exif;
    int exifOrientation = 1;
};

class ImageTexture {
public:
    GLuint id = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* rawData = nullptr;
    ImageMetadata meta;
    bool isLoaded = false;

    // Animated GIF playback state (empty for every other format).
    silvercodec::GifAnimation gif;
    int gifFrame = 0;
    float gifClock = 0.0f;
    bool gifPlaying = true;

    bool isAnimated() const { return gif.frameCount > 1; }
    int frameCount() const { return gif.frameCount; }

    // Advance GIF playback and push the current frame to the GPU.
    // Returns true when a new frame was uploaded.
    bool advanceAnimation(float dt) {
        if (!isAnimated() || !gifPlaying || !id) return false;

        int delay = gif.delaysMs[(size_t)gifFrame];
        gifClock += dt * 1000.0f;
        if (gifClock < (float)delay) return false;

        int guard = 0;
        while (gifClock >= (float)delay && guard++ < gif.frameCount) {
            gifClock -= (float)delay;
            gifFrame = (gifFrame + 1) % gif.frameCount;
            delay = gif.delaysMs[(size_t)gifFrame];
        }

        const unsigned char* frame = gif.frame(gifFrame);
        if (!frame) return false;

        glBindTexture(GL_TEXTURE_2D, id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gif.width, gif.height,
                        GL_RGBA, GL_UNSIGNED_BYTE, frame);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    void resetAnimation() {
        gif = silvercodec::GifAnimation();
        gifFrame = 0;
        gifClock = 0.0f;
        gifPlaying = true;
    }

    ~ImageTexture() {
        unload();
    }

    void unload() {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
        }
        if (rawData) {
            silvercodec::freePixels(rawData);
            rawData = nullptr;
        }
        isLoaded = false;
        width = 0;
        height = 0;
        channels = 0;
        resetAnimation();
    }

    static std::string formatBytes(size_t bytes) {
        char buf[64];
        if (bytes < 1024) {
            snprintf(buf, sizeof(buf), "%zu B", bytes);
        } else if (bytes < 1024 * 1024) {
            snprintf(buf, sizeof(buf), "%.1f kB", bytes / 1024.0f);
        } else if (bytes < 1024ULL * 1024 * 1024) {
            snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0f * 1024.0f));
        } else {
            snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0f * 1024.0f * 1024.0f));
        }
        return std::string(buf);
    }

    static int gcd(int a, int b) {
        while (b != 0) {
            int t = b;
            b = a % b;
            a = t;
        }
        return a;
    }

    bool probeHeader(const std::string& path) {
        meta.filePath = path;
        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            meta.fileName = path.substr(lastSlash + 1);
            meta.fileDirectory = path.substr(0, lastSlash);
        } else {
            meta.fileName = path;
            meta.fileDirectory = ".";
        }

        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            meta.fileSizeBytes = st.st_size;
            meta.fileSizeFormatted = formatBytes(st.st_size);
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
        }

        int w = 0, h = 0, comp = 0;
        if (silvercodec::info(path, &w, &h, &comp)) {
            // Rotated shots report their pre-rotation size in the header.
            meta.exifOrientation = silvercodec::readExifOrientation(path);
            if (meta.exifOrientation >= 5) std::swap(w, h);
            width = w;
            height = h;
            channels = comp;
            meta.width = w;
            meta.height = h;
            meta.dimensionsStr = std::to_string(w) + " x " + std::to_string(h);
            return true;
        }
        return false;
    }

    bool uploadPixels(const unsigned char* pixels, int w, int h, const ImageMetadata& metadata) {
        unload();   // also clears any previous animation
        if (!pixels || w <= 0 || h <= 0) return false;

        meta = metadata;
        width = w;
        height = h;
        channels = 4;

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, 0);
        isLoaded = true;
        return true;
    }

    bool load(const std::string& path) {
        unload();

        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            return false;
        }
        meta.filePath = path;
        meta.fileSizeBytes = st.st_size;
        meta.fileSizeFormatted = formatBytes(st.st_size);

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
        } else if (ext == "svg" || ext == "svgz") {
            meta.fileTypeStr = "SVG Vector";
            meta.mimeType = "image/svg+xml";
        } else if (ext == "avif") {
            meta.fileTypeStr = "AVIF Image";
            meta.mimeType = "image/avif";
        } else {
            meta.fileTypeStr = "Image File";
            meta.mimeType = "image/" + ext;
        }

        // Fast Header EXIF (64KB header only)
        meta.hasExif = false;
        meta.exif.clear();
        meta.exifOrientation = 1;
        meta.dpiX = 96;
        meta.dpiY = 96;

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

        int origComp = 4;
        if (silvercodec::detectFormat(path) == silvercodec::FMT_GIF &&
            silvercodec::loadGifAnimation(path, gif) && gif.valid()) {
            // Keep every composited frame so the viewer can play it back.
            width = gif.width;
            height = gif.height;
            origComp = 4;
            size_t bytes = gif.frameBytes();
            rawData = (unsigned char*)malloc(bytes);
            if (rawData) memcpy(rawData, gif.frame(0), bytes);
            gifFrame = 0;
            gifClock = 0.0f;
        } else {
            rawData = silvercodec::loadRGBA(path, &width, &height, &origComp);
        }
        if (!rawData) {
            return false;
        }
        if (meta.exifOrientation > 1 && !isAnimated()) {
            rawData = silvercodec::applyOrientation(rawData, width, height, meta.exifOrientation, &width, &height);
        }
        channels = 4;
        meta.width = width;
        meta.height = height;
        meta.channels = origComp;
        meta.bitDepth = origComp * 8;
        meta.megapixels = (float)(width * height) / 1000000.0f;
        meta.dimensionsStr = std::to_string(width) + " x " + std::to_string(height);
        if (isAnimated()) {
            meta.fileTypeStr = "GIF Animation";
            meta.dimensionsStr += "  (" + std::to_string(gif.frameCount) + " frames)";
        }

        int g = gcd(width, height);
        if (g > 0) {
            char arBuf[64];
            snprintf(arBuf, sizeof(arBuf), "%d:%d (%.2f:1)", width / g, height / g, (float)width / (float)height);
            meta.aspectRatioStr = arBuf;
        }

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rawData);
        glGenerateMipmap(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, 0);

        isLoaded = true;
        return true;
    }

    void setFiltering(bool nearest) {
        if (!id) return;
        glBindTexture(GL_TEXTURE_2D, id);
        if (nearest) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }
};
