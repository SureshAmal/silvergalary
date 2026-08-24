#pragma once

// -----------------------------------------------------------------------------
// SilverXdgThumb - read the desktop's shared thumbnail cache.
//
// Nautilus, Dolphin, Thunar, Nemo and friends all write to the freedesktop
// Thumbnail Managing Standard cache:
//
//   $XDG_CACHE_HOME/thumbnails/{normal,large,x-large,xx-large}/<md5>.png
//     normal 128px | large 256px | x-large 512px | xx-large 1024px
//
// <md5> is the MD5 of the file's absolute URI ("file:///home/u/a.jpg", with
// percent-encoding). Validity is checked against two PNG tEXt chunks the
// producer must write: Thumb::URI and Thumb::MTime.
//
// Why bother: decoding one of these PNGs costs ~2 ms, where re-decoding an
// original WebP costs ~16 ms. For any file the user has already browsed in
// their file manager, the work is simply already done.
//
// Reference: https://specifications.freedesktop.org/thumbnail/latest/
// -----------------------------------------------------------------------------

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <filesystem>
#include <system_error>
#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#endif

namespace silverxdg {

// -----------------------------------------------------------------------------
// MD5 (RFC 1321). Needed only to reproduce the spec's file naming.
// -----------------------------------------------------------------------------
class MD5 {
public:
    MD5() { reset(); }

    void reset() {
        state[0] = 0x67452301; state[1] = 0xefcdab89;
        state[2] = 0x98badcfe; state[3] = 0x10325476;
        count = 0;
        bufferLen = 0;
    }

    void update(const unsigned char* data, size_t len) {
        count += (uint64_t)len * 8;
        absorb(data, len);
    }

    std::string hexDigest() {
        // Length in bits must be captured before the padding is absorbed.
        const uint64_t bits = count;

        unsigned char pad[72];
        memset(pad, 0, sizeof(pad));
        pad[0] = 0x80;
        size_t padLen = (bufferLen < 56) ? (56 - bufferLen) : (120 - bufferLen);
        absorb(pad, padLen);

        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = (unsigned char)((bits >> (8 * i)) & 0xFF);
        absorb(lenBytes, 8);

        char out[33];
        for (int i = 0; i < 4; ++i) {
            for (int b = 0; b < 4; ++b) {
                snprintf(out + (i * 4 + b) * 2, 3, "%02x",
                         (unsigned)((state[i] >> (8 * b)) & 0xFF));
            }
        }
        return std::string(out, 32);
    }

    static std::string of(const std::string& text) {
        MD5 md5;
        md5.update((const unsigned char*)text.data(), text.size());
        return md5.hexDigest();
    }

private:
    uint32_t state[4];
    uint64_t count = 0;         // message length in bits
    unsigned char buffer[64];
    size_t bufferLen = 0;

    // Feed bytes through the 64-byte block buffer without counting them.
    void absorb(const unsigned char* data, size_t len) {
        while (len > 0) {
            size_t take = 64 - bufferLen;
            if (take > len) take = len;
            memcpy(buffer + bufferLen, data, take);
            bufferLen += take;
            data += take;
            len -= take;
            if (bufferLen == 64) {
                transform(buffer);
                bufferLen = 0;
            }
        }
    }

    static uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

    void transform(const unsigned char block[64]) {
        static const uint32_t K[64] = {
            0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
            0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
            0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
            0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
            0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
            0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
            0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
            0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
        };
        static const int S[64] = {
            7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
            5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
        };

        uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
                   ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
        }

        uint32_t A = state[0], B = state[1], C = state[2], D = state[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t F;
            int g;
            if (i < 16)      { F = (B & C) | (~B & D);        g = i; }
            else if (i < 32) { F = (D & B) | (~D & C);        g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;                 g = (3 * i + 5) % 16; }
            else             { F = C ^ (B | ~D);              g = (7 * i) % 16; }

            F = F + A + K[i] + M[g];
            A = D;
            D = C;
            C = B;
            B = B + rotl(F, S[i]);
        }
        state[0] += A; state[1] += B; state[2] += C; state[3] += D;
    }
};

// -----------------------------------------------------------------------------
// URI construction
// -----------------------------------------------------------------------------

// Percent-encode a path into a file:// URI exactly the way GLib's
// g_filename_to_uri does, since that is what produced the cache entries.
inline std::string pathToFileURI(const std::string& absolutePath) {
    static const char* kUnreserved =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~!$&'()*+,;=:@/";

    std::string uri = "file://";
    for (unsigned char c : absolutePath) {
        if (strchr(kUnreserved, (int)c) && c != '\0') {
            uri += (char)c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned)c);
            uri += buf;
        }
    }
    return uri;
}

inline std::string cacheRoot() {
#ifdef _WIN32
    return "";   // no shared thumbnail cache on Windows
#else
    const char* xdg = getenv("XDG_CACHE_HOME");
    std::string base;
    if (xdg && xdg[0]) {
        base = xdg;
    } else {
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : nullptr;
        }
        if (!home) return "";
        base = std::string(home) + "/.cache";
    }
    return base + "/thumbnails";
#endif
}

// The four standard sizes, smallest first.
struct XdgTier {
    const char* dir;
    int edge;
};

inline const XdgTier* tiers(int& count) {
    static const XdgTier kTiers[4] = {
        { "normal",   128 },
        { "large",    256 },
        { "x-large",  512 },
        { "xx-large", 1024 }
    };
    count = 4;
    return kTiers;
}

// -----------------------------------------------------------------------------
// PNG tEXt inspection
// -----------------------------------------------------------------------------

// Read a tEXt value out of a PNG header. Returns false when absent.
// Only the first 64 KB is scanned - producers write these before the image data.
inline bool readPngTextChunk(const std::string& pngPath, const char* key, std::string& out) {
    FILE* fp = fopen(pngPath.c_str(), "rb");
    if (!fp) return false;

    unsigned char header[8];
    if (fread(header, 1, 8, fp) != 8 || memcmp(header, "\x89PNG\r\n\x1a\n", 8) != 0) {
        fclose(fp);
        return false;
    }

    const size_t kMaxScan = 64 * 1024;
    size_t scanned = 0;
    bool found = false;

    while (scanned < kMaxScan) {
        unsigned char lenBytes[4];
        char type[5] = {0};
        if (fread(lenBytes, 1, 4, fp) != 4) break;
        if (fread(type, 1, 4, fp) != 4) break;

        uint32_t len = ((uint32_t)lenBytes[0] << 24) | ((uint32_t)lenBytes[1] << 16) |
                       ((uint32_t)lenBytes[2] << 8) | (uint32_t)lenBytes[3];
        scanned += 12 + len;

        if (memcmp(type, "IDAT", 4) == 0 || memcmp(type, "IEND", 4) == 0) break;

        if (memcmp(type, "tEXt", 4) == 0 && len > 0 && len < 8192) {
            std::vector<char> data(len);
            if (fread(data.data(), 1, len, fp) != len) break;

            // "keyword\0value"
            size_t sep = 0;
            while (sep < data.size() && data[sep] != '\0') sep++;
            if (sep < data.size()) {
                std::string chunkKey(data.data(), sep);
                if (chunkKey == key) {
                    out.assign(data.data() + sep + 1, data.size() - sep - 1);
                    found = true;
                    break;
                }
            }
            fseek(fp, 4, SEEK_CUR);   // CRC
            continue;
        }

        fseek(fp, (long)len + 4, SEEK_CUR);   // payload + CRC
    }

    fclose(fp);
    return found;
}

// -----------------------------------------------------------------------------
// Lookup
// -----------------------------------------------------------------------------

struct Hit {
    std::string file;   // PNG in the shared cache
    int edge = 0;       // its tier size
};

// The desktop records files it could not thumbnail under fail/<producer>/.
// Anything listed there is not worth attempting again.
inline bool isKnownFailure(const std::string& path) {
    std::string root = cacheRoot();
    if (root.empty()) return false;

    struct stat srcSt;
    if (stat(path.c_str(), &srcSt) != 0) return false;

    std::string failRoot = root + "/fail";
    std::string md5 = MD5::of(pathToFileURI(path));

    std::error_code ec;
    std::filesystem::directory_iterator it(failRoot, ec);
    if (ec) return false;

    for (const auto& producer : it) {
        if (!producer.is_directory(ec)) continue;
        std::string candidate = producer.path().string() + "/" + md5 + ".png";
        struct stat st;
        if (stat(candidate.c_str(), &st) != 0) continue;

        // Only trust the marker while the original is unchanged.
        std::string mtimeText;
        if (!readPngTextChunk(candidate, "Thumb::MTime", mtimeText)) return true;
        if (strtoll(mtimeText.c_str(), nullptr, 10) == (long long)srcSt.st_mtime) return true;
    }
    return false;
}

// Find a shared-cache thumbnail for `path` at `minEdge` or larger, verified
// against the original's mtime. Returns false when nothing usable exists.
inline bool find(const std::string& path, int minEdge, Hit& hit) {
    std::string root = cacheRoot();
    if (root.empty()) return false;

    struct stat srcSt;
    if (stat(path.c_str(), &srcSt) != 0) return false;

    std::string md5 = MD5::of(pathToFileURI(path));

    int tierCount = 0;
    const XdgTier* list = tiers(tierCount);

    for (int i = 0; i < tierCount; ++i) {
        if (list[i].edge < minEdge) continue;

        std::string candidate = root + "/" + list[i].dir + "/" + md5 + ".png";
        struct stat thumbSt;
        if (stat(candidate.c_str(), &thumbSt) != 0) continue;

        // The spec requires Thumb::MTime to match the original exactly; a
        // mismatch means the source changed after the thumbnail was made.
        std::string mtimeText;
        if (!readPngTextChunk(candidate, "Thumb::MTime", mtimeText)) continue;
        if (strtoll(mtimeText.c_str(), nullptr, 10) != (long long)srcSt.st_mtime) continue;

        hit.file = candidate;
        hit.edge = list[i].edge;
        return true;
    }
    return false;
}

} // namespace silverxdg
