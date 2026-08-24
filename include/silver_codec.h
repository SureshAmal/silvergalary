#pragma once

// -----------------------------------------------------------------------------
// SilverCodec - unified image decoding front-end.
//
// Wraps stb_image with optional accelerated / extra-format back-ends:
//   * JPEG  -> libjpeg-turbo (DCT-scaled decoding for thumbnails)   [HAVE_TURBOJPEG]
//   * WebP  -> libwebp (native scaled decoding)                     [HAVE_WEBP]
//   * AVIF  -> libavif                                              [HAVE_AVIF]
//   * SVG   -> nanosvg (vendored, always available)
//   * rest  -> stb_image
//
// Every buffer handed back is plain malloc() memory in RGBA8 order and must be
// released with silvercodec::freePixels(). stb_image.h must already be included
// (with its implementation compiled) before this header.
// -----------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#include "stb_image.h"
#include "stb_image_resize.h"

#ifdef HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif

#ifdef HAVE_WEBP
#include <webp/decode.h>
#endif

#ifdef HAVE_AVIF
#include <avif/avif.h>
#endif

// Declarations only; see src/silver_thirdparty.cpp for the implementations.
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace silvercodec {

enum Format {
    FMT_UNKNOWN = 0,
    FMT_JPEG,
    FMT_PNG,
    FMT_WEBP,
    FMT_AVIF,
    FMT_SVG,
    FMT_GIF,
    FMT_BMP,
    FMT_OTHER
};

// Canvas size used for vector images that declare no intrinsic size.
static const int kSvgFallbackSize = 512;

inline void freePixels(void* p) {
    if (p) free(p);
}

inline std::string lowerExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext;
}

// The one list of extensions this application handles.
//
// This lives here, next to the decoders, because it must match what the codec
// layer can actually open. Keeping separate copies in the indexer and the
// viewer's folder scanner let them drift: the indexer accepted SVG and AVIF
// while the viewer's navigation silently ignored them.
inline bool isSupportedExtension(const std::string& extWithDot) {
    static const char* kExts[] = {
        ".png", ".jpg", ".jpeg", ".jfif", ".webp", ".bmp", ".gif",
        ".tga", ".ppm", ".pgm", ".pnm", ".psd", ".hdr", ".pic",
        ".svg", ".svgz", ".ico", ".avif"
    };

    std::string lower = extWithDot;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    for (const char* e : kExts) {
        if (lower == e) return true;
    }
    return false;
}

// Sniff the first bytes so a mislabeled extension still decodes correctly.
inline Format detectFormat(const std::string& path) {
    unsigned char sig[16] = {0};
    size_t got = 0;
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp) {
        got = fread(sig, 1, sizeof(sig), fp);
        fclose(fp);
    }

    if (got >= 3 && sig[0] == 0xFF && sig[1] == 0xD8 && sig[2] == 0xFF) return FMT_JPEG;
    if (got >= 8 && memcmp(sig, "\x89PNG\r\n\x1a\n", 8) == 0) return FMT_PNG;
    if (got >= 12 && memcmp(sig, "RIFF", 4) == 0 && memcmp(sig + 8, "WEBP", 4) == 0) return FMT_WEBP;
    if (got >= 12 && memcmp(sig + 4, "ftyp", 4) == 0) {
        if (memcmp(sig + 8, "avif", 4) == 0 || memcmp(sig + 8, "avis", 4) == 0) return FMT_AVIF;
    }
    if (got >= 6 && memcmp(sig, "GIF8", 4) == 0) return FMT_GIF;
    if (got >= 2 && sig[0] == 'B' && sig[1] == 'M') return FMT_BMP;

    std::string ext = lowerExtension(path);
    if (ext == "svg" || ext == "svgz") return FMT_SVG;
    if (got >= 5) {
        // XML prologue or a bare <svg root element
        const char* head = (const char*)sig;
        if (strncmp(head, "<?xml", 5) == 0 || strncmp(head, "<svg", 4) == 0) return FMT_SVG;
    }

    if (ext == "jpg" || ext == "jpeg" || ext == "jfif") return FMT_JPEG;
    if (ext == "png") return FMT_PNG;
    if (ext == "webp") return FMT_WEBP;
    if (ext == "avif") return FMT_AVIF;
    return FMT_OTHER;
}

// Box-average by an integer factor. Cheap, cache friendly, and it does most of
// the work before the (much more expensive) general resampler runs.
inline unsigned char* boxReduce(unsigned char* src, int w, int h, int factor, int* outW, int* outH) {
    if (factor < 2) { *outW = w; *outH = h; return src; }
    int dw = std::max(1, w / factor);
    int dh = std::max(1, h / factor);
    unsigned char* dst = (unsigned char*)malloc((size_t)dw * dh * 4);
    if (!dst) { *outW = w; *outH = h; return src; }

    const int inv = factor * factor;
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            unsigned int acc[4] = { 0, 0, 0, 0 };
            for (int by = 0; by < factor; ++by) {
                const unsigned char* row = src + ((size_t)(y * factor + by) * w + (size_t)x * factor) * 4;
                for (int bx = 0; bx < factor; ++bx) {
                    acc[0] += row[bx * 4 + 0];
                    acc[1] += row[bx * 4 + 1];
                    acc[2] += row[bx * 4 + 2];
                    acc[3] += row[bx * 4 + 3];
                }
            }
            unsigned char* dp = dst + ((size_t)y * dw + x) * 4;
            dp[0] = (unsigned char)(acc[0] / inv);
            dp[1] = (unsigned char)(acc[1] / inv);
            dp[2] = (unsigned char)(acc[2] / inv);
            dp[3] = (unsigned char)(acc[3] / inv);
        }
    }
    free(src);
    *outW = dw;
    *outH = dh;
    return dst;
}

// Downscale RGBA to exactly tw x th. Consumes src, returns a fresh buffer.
inline unsigned char* resizeRGBA(unsigned char* src, int w, int h, int tw, int th) {
    if (!src || tw <= 0 || th <= 0) return src;
    if (tw == w && th == h) return src;

    if (tw < w && th < h) {
        int factor = std::min(w / tw, h / th);
        if (factor >= 2) {
            // Leave at least a 1x margin so the final resample still antialiases.
            factor = std::min(factor, 16);
            src = boxReduce(src, w, h, factor, &w, &h);
            if (w == tw && h == th) return src;
        }
    }

    unsigned char* dst = (unsigned char*)malloc((size_t)tw * th * 4);
    if (!dst) return src;
    stbir_resize_uint8(src, w, h, 0, dst, tw, th, 0, 4);
    free(src);
    return dst;
}

inline bool readWholeFile(const std::string& path, std::vector<unsigned char>& out) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0) { fclose(fp); return false; }
    out.resize((size_t)len);
    size_t got = fread(out.data(), 1, (size_t)len, fp);
    fclose(fp);
    if (got != (size_t)len) { out.clear(); return false; }
    return true;
}

// -----------------------------------------------------------------------------
// GIF
//
// stbi_load() hands back a single raw GIF frame with no canvas compositing, so
// anything using disposal methods, a sub-rectangle frame or a local colour
// table decodes wrong (partial images, stale pixels, wrong transparency).
// stbi_load_gif_from_memory() runs the real frame pipeline, so every GIF goes
// through it - even when only the first frame is wanted.
// -----------------------------------------------------------------------------
struct GifAnimation {
    int width = 0;
    int height = 0;
    int frameCount = 0;
    std::vector<unsigned char> pixels;   // frameCount * width * height * 4, RGBA
    std::vector<int> delaysMs;           // per frame, already sanitised

    size_t frameBytes() const { return (size_t)width * height * 4; }

    const unsigned char* frame(int index) const {
        if (index < 0 || index >= frameCount) return nullptr;
        return pixels.data() + (size_t)index * frameBytes();
    }

    bool valid() const { return frameCount > 0 && width > 0 && height > 0; }
};

// Guard rails so a pathological GIF cannot exhaust memory.
inline int& gifMaxFrames() { static int v = 300; return v; }
inline size_t& gifMaxBytes() { static size_t v = 256ull * 1024 * 1024; return v; }

inline bool loadGifAnimation(const std::string& path, GifAnimation& out) {
    std::vector<unsigned char> buf;
    if (!readWholeFile(path, buf)) return false;

    int* delays = nullptr;
    int w = 0, h = 0, frames = 0, comp = 0;
    unsigned char* data = stbi_load_gif_from_memory(buf.data(), (int)buf.size(),
                                                    &delays, &w, &h, &frames, &comp, 4);
    if (!data) {
        if (delays) free(delays);
        return false;
    }
    if (w <= 0 || h <= 0 || frames <= 0) {
        stbi_image_free(data);
        if (delays) free(delays);
        return false;
    }

    // Keep at most as many frames as the caps allow; frame 0 is always kept.
    size_t frameBytes = (size_t)w * h * 4;
    int keep = frames;
    if (keep > gifMaxFrames()) keep = gifMaxFrames();
    if (frameBytes > 0 && (size_t)keep * frameBytes > gifMaxBytes()) {
        keep = (int)std::max<size_t>(1, gifMaxBytes() / frameBytes);
    }

    out.width = w;
    out.height = h;
    out.frameCount = keep;
    out.pixels.assign(data, data + (size_t)keep * frameBytes);
    out.delaysMs.resize((size_t)keep);
    for (int i = 0; i < keep; ++i) {
        // GIF delays are in 1/100 s; 0 and 1 mean "as fast as possible", which
        // every browser clamps to 100 ms.
        int ms = delays ? delays[i] : 100;
        out.delaysMs[(size_t)i] = (ms <= 10) ? 100 : ms;
    }

    stbi_image_free(data);
    if (delays) free(delays);
    return out.valid();
}

// Single properly composited frame (frame 0 unless another index is asked for).
inline unsigned char* loadGifFrame(const std::string& path, int index,
                                   int* outW, int* outH) {
    GifAnimation anim;
    if (!loadGifAnimation(path, anim)) return nullptr;
    const unsigned char* src = anim.frame(std::clamp(index, 0, anim.frameCount - 1));
    if (!src) return nullptr;

    size_t bytes = anim.frameBytes();
    unsigned char* copy = (unsigned char*)malloc(bytes);
    if (!copy) return nullptr;
    memcpy(copy, src, bytes);
    if (outW) *outW = anim.width;
    if (outH) *outH = anim.height;
    return copy;
}

// -----------------------------------------------------------------------------
// Header probe: dimensions without decoding pixels.
// -----------------------------------------------------------------------------
inline bool info(const std::string& path, int* w, int* h, int* comp) {
    Format fmt = detectFormat(path);

    if (fmt == FMT_SVG) {
        NSVGimage* img = nsvgParseFromFile(path.c_str(), "px", 96.0f);
        if (!img) return false;
        int iw = (int)(img->width + 0.5f);
        int ih = (int)(img->height + 0.5f);
        nsvgDelete(img);
        // Plenty of SVGs carry only a viewBox and no width/height attributes.
        // They are still perfectly renderable - report a nominal square canvas
        // rather than treating them as broken.
        if (iw <= 0 || ih <= 0) {
            iw = kSvgFallbackSize;
            ih = kSvgFallbackSize;
        }
        if (w) *w = iw;
        if (h) *h = ih;
        if (comp) *comp = 4;
        return true;
    }

#ifdef HAVE_WEBP
    if (fmt == FMT_WEBP) {
        std::vector<unsigned char> buf;
        if (readWholeFile(path, buf)) {
            int iw = 0, ih = 0;
            if (WebPGetInfo(buf.data(), buf.size(), &iw, &ih)) {
                if (w) *w = iw;
                if (h) *h = ih;
                if (comp) *comp = 4;
                return true;
            }
        }
        return false;
    }
#endif

#ifdef HAVE_AVIF
    if (fmt == FMT_AVIF) {
        avifDecoder* dec = avifDecoderCreate();
        if (!dec) return false;
        bool ok = false;
        if (avifDecoderSetIOFile(dec, path.c_str()) == AVIF_RESULT_OK &&
            avifDecoderParse(dec) == AVIF_RESULT_OK) {
            if (w) *w = (int)dec->image->width;
            if (h) *h = (int)dec->image->height;
            if (comp) *comp = 4;
            ok = dec->image->width > 0 && dec->image->height > 0;
        }
        avifDecoderDestroy(dec);
        return ok;
    }
#endif

    // stb parses just the header off disk - cheaper than buffering the whole
    // file, so it stays the first choice for everything it understands.
    if (stbi_info(path.c_str(), w, h, comp) != 0) return true;

#ifdef HAVE_TURBOJPEG
    if (fmt == FMT_JPEG) {
        std::vector<unsigned char> buf;
        if (readWholeFile(path, buf)) {
            tjhandle tj = tjInitDecompress();
            if (tj) {
                int iw = 0, ih = 0, subsamp = 0, cs = 0;
                bool ok = tjDecompressHeader3(tj, buf.data(), (unsigned long)buf.size(),
                                              &iw, &ih, &subsamp, &cs) == 0;
                tjDestroy(tj);
                if (ok) {
                    if (w) *w = iw;
                    if (h) *h = ih;
                    if (comp) *comp = 3;
                    return true;
                }
            }
        }
    }
#endif

    return false;
}

// -----------------------------------------------------------------------------
// Full-resolution RGBA decode.
// -----------------------------------------------------------------------------
inline unsigned char* loadRGBAKnown(const std::string& path, Format fmt,
                                    int* outW, int* outH, int* outComp);

inline unsigned char* loadRGBA(const std::string& path, int* outW, int* outH, int* outComp) {
    return loadRGBAKnown(path, detectFormat(path), outW, outH, outComp);
}

// Same, but for callers that already sniffed the format - detectFormat opens
// the file, and doing it twice per decode is a wasted syscall pair on every
// photo in the library.
inline unsigned char* loadRGBAKnown(const std::string& path, Format fmt,
                                    int* outW, int* outH, int* outComp) {

    if (fmt == FMT_SVG) {
        NSVGimage* img = nsvgParseFromFile(path.c_str(), "px", 96.0f);
        if (!img) return nullptr;
        // Vectors have no native pixel size; render at a generous 2048px max edge.
        float maxEdge = 2048.0f;
        float maxDim = std::max(img->width, img->height);
        if (maxDim <= 0.0f) {
            // No width/height and no usable viewBox extent - fall back to a
            // square canvas so the image still renders.
            img->width = (float)kSvgFallbackSize;
            img->height = (float)kSvgFallbackSize;
            maxDim = (float)kSvgFallbackSize;
        }
        float scale = std::min(4.0f, maxEdge / maxDim);
        int w = std::max(1, (int)(img->width * scale + 0.5f));
        int h = std::max(1, (int)(img->height * scale + 0.5f));

        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (!rast) { nsvgDelete(img); return nullptr; }
        unsigned char* pixels = (unsigned char*)malloc((size_t)w * h * 4);
        if (!pixels) { nsvgDeleteRasterizer(rast); nsvgDelete(img); return nullptr; }
        memset(pixels, 0, (size_t)w * h * 4);
        nsvgRasterize(rast, img, 0.0f, 0.0f, scale, pixels, w, h, w * 4);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(img);

        if (outW) *outW = w;
        if (outH) *outH = h;
        if (outComp) *outComp = 4;
        return pixels;
    }

#ifdef HAVE_WEBP
    if (fmt == FMT_WEBP) {
        std::vector<unsigned char> buf;
        if (!readWholeFile(path, buf)) return nullptr;
        int w = 0, h = 0;
        if (!WebPGetInfo(buf.data(), buf.size(), &w, &h) || w <= 0 || h <= 0) return nullptr;
        unsigned char* pixels = (unsigned char*)malloc((size_t)w * h * 4);
        if (!pixels) return nullptr;
        if (!WebPDecodeRGBAInto(buf.data(), buf.size(), pixels, (size_t)w * h * 4, w * 4)) {
            free(pixels);
            return nullptr;
        }
        if (outW) *outW = w;
        if (outH) *outH = h;
        if (outComp) *outComp = 4;
        return pixels;
    }
#endif

#ifdef HAVE_AVIF
    if (fmt == FMT_AVIF) {
        avifDecoder* dec = avifDecoderCreate();
        if (!dec) return nullptr;
        unsigned char* pixels = nullptr;
        if (avifDecoderSetIOFile(dec, path.c_str()) == AVIF_RESULT_OK &&
            avifDecoderParse(dec) == AVIF_RESULT_OK &&
            avifDecoderNextImage(dec) == AVIF_RESULT_OK) {
            int w = (int)dec->image->width;
            int h = (int)dec->image->height;
            pixels = (unsigned char*)malloc((size_t)w * h * 4);
            if (pixels) {
                avifRGBImage rgb;
                avifRGBImageSetDefaults(&rgb, dec->image);
                rgb.format = AVIF_RGB_FORMAT_RGBA;
                rgb.depth = 8;
                rgb.pixels = pixels;
                rgb.rowBytes = (uint32_t)(w * 4);
                if (avifImageYUVToRGB(dec->image, &rgb) != AVIF_RESULT_OK) {
                    free(pixels);
                    pixels = nullptr;
                } else {
                    if (outW) *outW = w;
                    if (outH) *outH = h;
                    if (outComp) *outComp = 4;
                }
            }
        }
        avifDecoderDestroy(dec);
        if (pixels) return pixels;
        return nullptr;
    }
#endif

    if (fmt == FMT_GIF) {
        int w = 0, h = 0;
        unsigned char* pixels = loadGifFrame(path, 0, &w, &h);
        if (pixels) {
            if (outW) *outW = w;
            if (outH) *outH = h;
            if (outComp) *outComp = 4;
            return pixels;
        }
        // fall through to stb if the frame pipeline refused the file
    }

#ifdef HAVE_TURBOJPEG
    if (fmt == FMT_JPEG) {
        std::vector<unsigned char> buf;
        if (readWholeFile(path, buf)) {
            tjhandle tj = tjInitDecompress();
            if (tj) {
                int w = 0, h = 0, subsamp = 0, cs = 0;
                if (tjDecompressHeader3(tj, buf.data(), (unsigned long)buf.size(),
                                        &w, &h, &subsamp, &cs) == 0 && w > 0 && h > 0) {
                    unsigned char* pixels = (unsigned char*)malloc((size_t)w * h * 4);
                    if (pixels && tjDecompress2(tj, buf.data(), (unsigned long)buf.size(), pixels,
                                                w, w * 4, h, TJPF_RGBA, TJFLAG_FASTDCT) == 0) {
                        tjDestroy(tj);
                        if (outW) *outW = w;
                        if (outH) *outH = h;
                        if (outComp) *outComp = 3;
                        return pixels;
                    }
                    if (pixels) free(pixels);
                }
                tjDestroy(tj);
            }
        }
        // fall through to stb
    }
#endif

    int comp = 0;
    unsigned char* pixels = stbi_load(path.c_str(), outW, outH, &comp, 4);
    if (outComp) *outComp = comp ? comp : 4;
    return pixels;
}

// -----------------------------------------------------------------------------
// Thumbnail decode. Decodes directly at (or near) the requested size whenever
// the back-end supports it, so a 60 MP JPEG never materializes at full size.
// Returns RGBA of *outW x *outH; *origW/*origH report the true image size.
// -----------------------------------------------------------------------------
inline unsigned char* loadThumbRGBA(const std::string& path, int maxEdge,
                                    int* outW, int* outH,
                                    int* origW, int* origH) {
    if (maxEdge < 16) maxEdge = 16;
    Format fmt = detectFormat(path);

    auto finish = [&](unsigned char* src, int w, int h) -> unsigned char* {
        if (!src) return nullptr;
        if (origW) *origW = w;
        if (origH) *origH = h;

        int tw = w, th = h;
        if (w > maxEdge || h > maxEdge) {
            if (w >= h) {
                tw = maxEdge;
                th = std::max(1, (int)((int64_t)h * maxEdge / w));
            } else {
                th = maxEdge;
                tw = std::max(1, (int)((int64_t)w * maxEdge / h));
            }
        }
        if (tw == w && th == h) {
            if (outW) *outW = w;
            if (outH) *outH = h;
            return src;
        }

        unsigned char* dst = resizeRGBA(src, w, h, tw, th);
        if (outW) *outW = tw;
        if (outH) *outH = th;
        return dst;
    };

    if (fmt == FMT_GIF) {
        // Only frame 0 is wanted here. Going through the full animation loader
        // would composite every frame first - for a 300-frame GIF that is 300x
        // the work and memory for a single thumbnail.
        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (pixels) {
            if (origW) *origW = w;
            if (origH) *origH = h;

            int tw = w, th = h;
            if (w > maxEdge || h > maxEdge) {
                if (w >= h) { tw = maxEdge; th = std::max(1, (int)((int64_t)h * maxEdge / w)); }
                else        { th = maxEdge; tw = std::max(1, (int)((int64_t)w * maxEdge / h)); }
            }
            unsigned char* out = resizeRGBA(pixels, w, h, tw, th);
            if (outW) *outW = tw;
            if (outH) *outH = th;
            return out;
        }
        // fall through to the compositing path if stb refused the file
    }

    if (fmt == FMT_SVG) {
        NSVGimage* img = nsvgParseFromFile(path.c_str(), "px", 96.0f);
        if (!img) return nullptr;
        float maxDim = std::max(img->width, img->height);
        if (maxDim <= 0.0f) {
            img->width = (float)kSvgFallbackSize;
            img->height = (float)kSvgFallbackSize;
            maxDim = (float)kSvgFallbackSize;
        }
        // Rasterize straight at thumbnail resolution - vectors are resolution free.
        float scale = (float)maxEdge / maxDim;
        int w = std::max(1, (int)(img->width * scale + 0.5f));
        int h = std::max(1, (int)(img->height * scale + 0.5f));

        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (!rast) { nsvgDelete(img); return nullptr; }
        unsigned char* pixels = (unsigned char*)malloc((size_t)w * h * 4);
        if (!pixels) { nsvgDeleteRasterizer(rast); nsvgDelete(img); return nullptr; }
        memset(pixels, 0, (size_t)w * h * 4);
        nsvgRasterize(rast, img, 0.0f, 0.0f, scale, pixels, w, h, w * 4);
        if (origW) *origW = (int)(img->width + 0.5f);
        if (origH) *origH = (int)(img->height + 0.5f);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(img);
        if (outW) *outW = w;
        if (outH) *outH = h;
        return pixels;
    }

#ifdef HAVE_WEBP
    if (fmt == FMT_WEBP) {
        std::vector<unsigned char> buf;
        if (!readWholeFile(path, buf)) return nullptr;
        int w = 0, h = 0;
        if (!WebPGetInfo(buf.data(), buf.size(), &w, &h) || w <= 0 || h <= 0) return nullptr;
        if (origW) *origW = w;
        if (origH) *origH = h;

        int tw = w, th = h;
        if (w > maxEdge || h > maxEdge) {
            if (w >= h) { tw = maxEdge; th = std::max(1, (int)((int64_t)h * maxEdge / w)); }
            else        { th = maxEdge; tw = std::max(1, (int)((int64_t)w * maxEdge / h)); }
        }

        WebPDecoderConfig cfg;
        if (!WebPInitDecoderConfig(&cfg)) return nullptr;
        unsigned char* pixels = (unsigned char*)malloc((size_t)tw * th * 4);
        if (!pixels) return nullptr;
        cfg.options.use_scaling = (tw != w || th != h) ? 1 : 0;
        cfg.options.scaled_width = tw;
        cfg.options.scaled_height = th;
        cfg.output.colorspace = MODE_RGBA;
        cfg.output.is_external_memory = 1;
        cfg.output.u.RGBA.rgba = pixels;
        cfg.output.u.RGBA.stride = tw * 4;
        cfg.output.u.RGBA.size = (size_t)tw * th * 4;
        if (WebPDecode(buf.data(), buf.size(), &cfg) != VP8_STATUS_OK) {
            free(pixels);
            return nullptr;
        }
        if (outW) *outW = tw;
        if (outH) *outH = th;
        return pixels;
    }
#endif

#ifdef HAVE_TURBOJPEG
    if (fmt == FMT_JPEG) {
        std::vector<unsigned char> buf;
        if (readWholeFile(path, buf)) {
            tjhandle tj = tjInitDecompress();
            if (tj) {
                int w = 0, h = 0, subsamp = 0, cs = 0;
                if (tjDecompressHeader3(tj, buf.data(), (unsigned long)buf.size(),
                                        &w, &h, &subsamp, &cs) == 0 && w > 0 && h > 0) {
                    // Pick the cheapest DCT scaling factor that still covers maxEdge.
                    int numSF = 0;
                    tjscalingfactor* sfList = tjGetScalingFactors(&numSF);
                    int bestW = w, bestH = h;
                    if (sfList) {
                        for (int i = 0; i < numSF; ++i) {
                            int sw = TJSCALED(w, sfList[i]);
                            int sh = TJSCALED(h, sfList[i]);
                            if (std::max(sw, sh) >= maxEdge && (sw * (int64_t)sh) < (bestW * (int64_t)bestH)) {
                                bestW = sw;
                                bestH = sh;
                            }
                        }
                    }
                    unsigned char* pixels = (unsigned char*)malloc((size_t)bestW * bestH * 4);
                    if (pixels && tjDecompress2(tj, buf.data(), (unsigned long)buf.size(), pixels,
                                                bestW, bestW * 4, bestH, TJPF_RGBA, TJFLAG_FASTDCT) == 0) {
                        tjDestroy(tj);
                        if (origW) *origW = w;
                        if (origH) *origH = h;
                        // bestW/bestH may still exceed maxEdge (factors are coarse) - trim.
                        int fw = bestW, fh = bestH;
                        unsigned char* res = pixels;
                        if (bestW > maxEdge || bestH > maxEdge) {
                            if (bestW >= bestH) { fw = maxEdge; fh = std::max(1, (int)((int64_t)bestH * maxEdge / bestW)); }
                            else                { fh = maxEdge; fw = std::max(1, (int)((int64_t)bestW * maxEdge / bestH)); }
                            res = resizeRGBA(pixels, bestW, bestH, fw, fh);
                        }
                        if (outW) *outW = fw;
                        if (outH) *outH = fh;
                        return res;
                    }
                    if (pixels) free(pixels);
                }
                tjDestroy(tj);
            }
        }
        // fall through to stb
    }
#endif

    int w = 0, h = 0, comp = 0;
    unsigned char* raw = loadRGBAKnown(path, fmt, &w, &h, &comp);
    if (!raw) return nullptr;
    return finish(raw, w, h);
}

// -----------------------------------------------------------------------------
// EXIF orientation (1..8) applied in-place to an RGBA buffer.
// Returns a new buffer when the transform swaps axes; frees the input either way.
// -----------------------------------------------------------------------------
inline unsigned char* applyOrientation(unsigned char* src, int w, int h, int orientation,
                                       int* outW, int* outH) {
    if (outW) *outW = w;
    if (outH) *outH = h;
    if (!src || orientation <= 1 || orientation > 8) return src;

    bool swapAxes = (orientation >= 5);
    int dw = swapAxes ? h : w;
    int dh = swapAxes ? w : h;
    unsigned char* dst = (unsigned char*)malloc((size_t)dw * dh * 4);
    if (!dst) return src;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int nx = x, ny = y;
            switch (orientation) {
                case 2: nx = w - 1 - x; ny = y;             break; // mirror horizontal
                case 3: nx = w - 1 - x; ny = h - 1 - y;     break; // rotate 180
                case 4: nx = x;         ny = h - 1 - y;     break; // mirror vertical
                case 5: nx = y;         ny = x;             break; // transpose
                case 6: nx = h - 1 - y; ny = x;             break; // rotate 90 CW
                case 7: nx = h - 1 - y; ny = w - 1 - x;     break; // transverse
                case 8: nx = y;         ny = w - 1 - x;     break; // rotate 270 CW
                default: break;
            }
            const unsigned char* sp = src + ((size_t)y * w + x) * 4;
            unsigned char* dp = dst + ((size_t)ny * dw + nx) * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }

    free(src);
    if (outW) *outW = dw;
    if (outH) *outH = dh;
    return dst;
}

// Reads just the EXIF orientation tag from a JPEG header (1 when absent).
inline int readExifOrientation(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 1;
    unsigned char head[2];
    if (fread(head, 1, 2, fp) != 2 || head[0] != 0xFF || head[1] != 0xD8) {
        fclose(fp);
        return 1;
    }
    std::vector<unsigned char> buf(65536);
    size_t got = fread(buf.data(), 1, buf.size(), fp);
    fclose(fp);

    // Locate the APP1/Exif segment inside the header window.
    for (size_t i = 0; i + 10 < got; ++i) {
        if (buf[i] != 0xFF || buf[i + 1] != 0xE1) continue;
        if (memcmp(&buf[i + 4], "Exif\0\0", 6) != 0) continue;

        size_t tiff = i + 10;
        if (tiff + 8 > got) return 1;
        bool little = (buf[tiff] == 'I' && buf[tiff + 1] == 'I');
        auto rd16 = [&](size_t off) -> uint16_t {
            if (off + 1 >= got) return 0;
            return little ? (uint16_t)(buf[off] | (buf[off + 1] << 8))
                          : (uint16_t)((buf[off] << 8) | buf[off + 1]);
        };
        auto rd32 = [&](size_t off) -> uint32_t {
            if (off + 3 >= got) return 0;
            return little ? (uint32_t)(buf[off] | (buf[off + 1] << 8) | (buf[off + 2] << 16) | (buf[off + 3] << 24))
                          : (uint32_t)((buf[off] << 24) | (buf[off + 1] << 16) | (buf[off + 2] << 8) | buf[off + 3]);
        };

        uint32_t ifdOff = rd32(tiff + 4);
        size_t ifd = tiff + ifdOff;
        if (ifd + 2 > got) return 1;
        uint16_t entries = rd16(ifd);
        for (uint16_t e = 0; e < entries; ++e) {
            size_t entry = ifd + 2 + (size_t)e * 12;
            if (entry + 12 > got) break;
            if (rd16(entry) == 0x0112) {
                int val = (int)rd16(entry + 8);
                return (val >= 1 && val <= 8) ? val : 1;
            }
        }
        return 1;
    }
    return 1;
}

} // namespace silvercodec
