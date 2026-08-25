#pragma once

#include "gl_loader.h"
#include "theme.h"
#include "silver_constants.h"
#include "silver_config.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

// Declarations only - src/silver_thirdparty.cpp compiles the implementations.
// stb_rect_pack must still precede stb_truetype here, because stb_truetype
// declares its own stbrp_* types unless STB_RECT_PACK_VERSION is already set.
#include <stb_rect_pack.h>
#include <stb_truetype.h>

// FreeType + HarfBuzz text stack. Falls back to the stb_truetype ASCII bake
// when the libraries are not present at build time.
#include "silver_text.h"

struct UIVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float useTex; // 0 = solid color, 1 = font alpha, 2 = rgba texture, 3 = icon atlas alpha
};

class FontRenderer {
public:
    GLuint fontTexture = 0;
    GLuint emojiTexture = 0;      // RGBA page for colour glyphs
    int emojiGeneration = -1;
    int emojiDimUploaded = 0;
    stbtt_bakedchar cdata[96]; // ASCII 32..126
    int texWidth = 512;
    int texHeight = 512;
    float fontSize = 16.0f;

    GLuint vao = 0, vbo = 0;
    GLuint shaderProgram = 0;
    GLint uProjectionLoc = -1;
    GLint uFontTextureLoc = -1;
    GLint uCustomTextureLoc = -1;
    GLint uIconTextureLoc = -1;
    GLint uEmojiTextureLoc = -1;

    std::vector<UIVertex> vertices;

#ifdef HAVE_HARFBUZZ
    silvertext::TextEngine text;
    int atlasGeneration = -1;
    int atlasDimUploaded = 0;
    bool useShaping = false;
#endif

    // Framebuffer pixels per layout point. On a 1.25x scaled display the
    // framebuffer is larger than the window, so glyphs and icons must be
    // rasterized at physical resolution and only *positioned* in points -
    // otherwise the GPU upscales a low-resolution atlas and everything looks
    // soft and pixelated.
    GLint cachedViewport[4] = { 0, 0, 1, 1 };
    bool viewportValid = false;

    // Called on resize; the next frame re-queries.
    void invalidateViewport() { viewportValid = false; }

    float pixelScale = 1.0f;
    float requestedSize = silver::defaults::baseFontPoints;   // in points, before scaling

    void setPixelScale(float scale) {
        if (scale < silver::defaults::minPixelScale) scale = 1.0f;
        if (std::abs(scale - pixelScale) < 0.01f) return;
        pixelScale = scale;
        viewportValid = false;
        applyTextConfig();
    }

    // Convert a layout coordinate to the nearest physical framebuffer pixel.
    // Use this for static image edges and hairlines; animated surfaces keep
    // their fractional coordinates so motion remains continuous.
    float snapToPixel(float value) const {
        return std::round(value * pixelScale) / pixelScale;
    }

    int cornerSegments(float radius) const {
        int wanted = (int)std::ceil(radius * pixelScale * 0.35f);
        return std::clamp(wanted, silver::limits::minCornerSegments,
                          silver::limits::maxCornerSegments);
    }

    // Re-read text settings (size, hinting) and rebuild the atlas if needed.
    void applyTextConfig() {
#ifdef HAVE_HARFBUZZ
        if (!useShaping) return;
        float configured = SilverConfig::get().num("text.pixelSize", 0.0f);
        float pts = (configured > 0.0f) ? configured : requestedSize;
        if (text.reconfigure(pts * pixelScale)) {
            fontSize = pts;
            atlasGeneration = -1;   // force a full re-upload
        }
#endif
    }

    // -------------------------------------------------------------------------
    // Frame batching
    //
    // Call sites push geometry into `vertices` and then call render(), which used
    // to mean one glBufferData + one glDrawArrays each time - several hundred per
    // frame once the photo grid is full. With a frame open, render() instead
    // appends into a single per-frame vertex buffer and records a draw command;
    // adjacent commands that need no conflicting texture are merged, so the whole
    // frame collapses to one upload and roughly one draw per distinct thumbnail.
    // Order is never rearranged, so the painter's-algorithm result is identical.
    // -------------------------------------------------------------------------
    struct DrawCmd {
        GLuint customTex = 0;
        GLuint iconTex = 0;
        size_t first = 0;
        size_t count = 0;
        bool clip = false;                     // scissor this command
        float cx = 0, cy = 0, cw = 0, ch = 0;  // clip rect, UI coordinates
    };

    // Current clip, inherited by commands queued while it is set. Commands with
    // different clip state never merge, so a scrolling list can be masked to its
    // viewport even though every draw is deferred to endFrame().
    bool clipActive = false;
    float clipX = 0, clipY = 0, clipW = 0, clipH = 0;

    void pushClip(float x, float y, float w, float h) {
        clipActive = true;
        clipX = x; clipY = y; clipW = w; clipH = h;
    }

    void popClip() { clipActive = false; }

    ~FontRenderer() {
        if (fontTexture) glDeleteTextures(1, &fontTexture);
        if (emojiTexture) glDeleteTextures(1, &emojiTexture);
        if (persistentMapped && vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        for (GLsync& fence : streamFences)
            if (fence) glDeleteSync(fence);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (overflowVbo) glDeleteBuffers(1, &overflowVbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (shaderProgram) glDeleteProgram(shaderProgram);
    }

    std::vector<UIVertex> frameVertices;
    std::vector<DrawCmd> frameCmds;
    bool frameActive = false;
    int frameW = 0;
    int frameH = 0;

    // Diagnostics for the last completed frame.
    int drawCallsLastFrame = 0;
    int batchesLastFrame = 0;
    size_t verticesLastFrame = 0;
    unsigned char* persistentMapped = nullptr;
    GLuint overflowVbo = 0;
    size_t streamSegmentBytes = 0;
    int streamSegment = -1;
    GLsync streamFences[4] = { nullptr, nullptr, nullptr, nullptr };

    bool init(const char* fontPath = nullptr, float size = silver::defaults::baseFontPoints) {
        fontSize = size;
        requestedSize = size;

#ifdef HAVE_HARFBUZZ
        {
            std::vector<std::string> explicitFonts;
            if (fontPath && fontPath[0] != '\0') explicitFonts.push_back(fontPath);
            // Rasterize in framebuffer pixels; layout still happens in points.
            useShaping = text.init(fontSize * pixelScale, explicitFonts);
            if (useShaping) {
                // FreeType owns the glyph atlas; the stb bake below is skipped.
                glGenTextures(1, &fontTexture);
                syncFontAtlas();
                return initShaderAndBuffers();
            }
            fprintf(stderr, "[SilverText] shaping unavailable, using stb_truetype\n");
        }
#endif

        FILE* f = (fontPath && fontPath[0] != '\0') ? fopen(fontPath, "rb") : nullptr;
        if (!f) {
            const char* fallbacks[] = {
                // Windows System Fonts
                "C:\\Windows\\Fonts\\segoeui.ttf",
                "C:\\Windows\\Fonts\\arial.ttf",
                "C:\\Windows\\Fonts\\calibri.ttf",
                "C:\\Windows\\Fonts\\tahoma.ttf",
                // Fedora / RHEL
                "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
                "/usr/share/fonts/google-carlito-fonts/Carlito-Regular.ttf",
                "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
                "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                // Ubuntu / Debian / Mint / Pop!_OS
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
                "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
                "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                // Arch Linux / Manjaro / Void
                "/usr/share/fonts/TTF/DejaVuSans.ttf",
                "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
                "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                // openSUSE / Alpine / Generic
                "/usr/share/fonts/truetype/LiberationSans-Regular.ttf",
                "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                // Local relative fallbacks
                "src/FiraSans-Regular.ttf",
                "../src/FiraSans-Regular.ttf"
            };
            for (const char* fb : fallbacks) {
                f = fopen(fb, "rb");
                if (f) break;
            }
        }
        if (!f) {
            fprintf(stderr, "Could not open font file!\n");
            return false;
        }

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> fontBuffer(fileSize);
        fread(fontBuffer.data(), 1, fileSize, f);
        fclose(f);

        std::vector<unsigned char> tempBitmap(texWidth * texHeight);
        int result = stbtt_BakeFontBitmap(fontBuffer.data(), 0, fontSize,
                                          tempBitmap.data(), texWidth, texHeight,
                                          32, 96, cdata);
        if (result <= 0) {
            fprintf(stderr, "Font bake failed\n");
            return false;
        }

        std::vector<unsigned char> rgbaBitmap(texWidth * texHeight * 4);
        for (int i = 0; i < texWidth * texHeight; ++i) {
            rgbaBitmap[i * 4 + 0] = 255;
            rgbaBitmap[i * 4 + 1] = 255;
            rgbaBitmap[i * 4 + 2] = 255;
            rgbaBitmap[i * 4 + 3] = tempBitmap[i];
        }

        glGenTextures(1, &fontTexture);
        glBindTexture(GL_TEXTURE_2D, fontTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBitmap.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        return initShaderAndBuffers();
    }

    bool initShaderAndBuffers() {
        // UI Shader supporting solid color, font alpha, custom textures, and vector icon atlas
        const char* vsSource = R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            layout (location = 1) in vec2 aUV;
            layout (location = 2) in vec4 aCol;
            layout (location = 3) in float aUseTex;
            uniform mat4 uProjection;
            out vec2 vUV;
            out vec4 vCol;
            out float vUseTex;
            void main() {
                vUV = aUV;
                vCol = aCol;
                vUseTex = aUseTex;
                gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
            }
        )";

        const char* fsBody = R"(
            in vec2 vUV;
            in vec4 vCol;
            in float vUseTex;
            uniform sampler2D uFontTexture;
            uniform sampler2D uCustomTexture;
            uniform sampler2D uIconTexture;
            uniform sampler2D uEmojiTexture;
            out vec4 FragColor;
            void main() {
                if (vUseTex > 3.5) { // Colour glyph: keep the glyph's own colour
                    vec4 tex = texture(uEmojiTexture, vUV);
                    FragColor = vec4(tex.rgb, tex.a * vCol.a);
                } else if (vUseTex > 2.5) { // Icon Atlas Alpha
                    float alpha = texture(uIconTexture, vUV).a;
                    FragColor = vec4(vCol.rgb, vCol.a * alpha);
                } else if (vUseTex > 1.5) { // Custom Texture (Thumbnail)
                    vec4 tex = texture(uCustomTexture, vUV);
                    FragColor = vec4(tex.rgb, tex.a * vCol.a);
                } else if (vUseTex > 0.5) { // Font coverage
                    float alpha = texture(uFontTexture, vUV).FONT_CHANNEL;
                    FragColor = vec4(vCol.rgb, vCol.a * alpha);
                } else { // Solid Color
                    FragColor = vCol;
                }
            }
        )";

        // The FreeType atlas is single channel (GL_R8); the legacy stb bake is
        // RGBA with coverage in alpha.
        std::string fsFull = std::string("#version 330 core\n");
#ifdef HAVE_HARFBUZZ
        fsFull += useShaping ? "#define FONT_CHANNEL r\n" : "#define FONT_CHANNEL a\n";
#else
        fsFull += "#define FONT_CHANNEL a\n";
#endif
        fsFull += fsBody;
        const char* fsSource = fsFull.c_str();

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsSource, nullptr);
        glCompileShader(vs);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsSource, nullptr);
        glCompileShader(fs);

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vs);
        glAttachShader(shaderProgram, fs);
        glLinkProgram(shaderProgram);

        glDeleteShader(vs);
        glDeleteShader(fs);

        uProjectionLoc = glGetUniformLocation(shaderProgram, "uProjection");
        uFontTextureLoc = glGetUniformLocation(shaderProgram, "uFontTexture");
        uCustomTextureLoc = glGetUniformLocation(shaderProgram, "uCustomTexture");
        uIconTextureLoc = glGetUniformLocation(shaderProgram, "uIconTexture");
        uEmojiTextureLoc = glGetUniformLocation(shaderProgram, "uEmojiTexture");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        bool hasBufferStorage = major > 4 || (major == 4 && minor >= 4) ||
                                glfwExtensionSupported("GL_ARB_buffer_storage");
        if (hasBufferStorage) {
            size_t totalBytes = (size_t)std::max(32,
                SilverConfig::get().integer("rendering.vertexBufferMegabytes", 32))
                * 1024u * 1024u;
            streamSegmentBytes = totalBytes / 4u;
            glBufferStorage(GL_ARRAY_BUFFER, (GLsizeiptr)totalBytes, nullptr,
                            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                            GL_MAP_COHERENT_BIT);
            persistentMapped = (unsigned char*)glMapBufferRange(
                GL_ARRAY_BUFFER, 0, (GLsizeiptr)totalBytes,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT);
            if (!persistentMapped) {
                glDeleteBuffers(1, &vbo);
                glGenBuffers(1, &vbo);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                streamSegmentBytes = 0;
            }
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, x));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, u));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, r));

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, useTex));

        glBindVertexArray(0);
        return true;
    }

#ifdef HAVE_HARFBUZZ
    // Push the FreeType glyph atlas to the GPU whenever new glyphs appeared.
    void syncFontAtlas() {
        if (!useShaping || !text.dirty) return;

        int dim = text.atlasSize();
        glBindTexture(GL_TEXTURE_2D, fontTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        if (dim != atlasDimUploaded || text.generation != atlasGeneration) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, dim, dim, 0,
                         GL_RED, GL_UNSIGNED_BYTE, text.atlasPixels().data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            atlasDimUploaded = dim;
            atlasGeneration = text.generation;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dim, dim,
                            GL_RED, GL_UNSIGNED_BYTE, text.atlasPixels().data());
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        text.dirty = false;
    }

    // The colour page is created lazily, only once an emoji is actually shaped.
    void syncEmojiAtlas() {
        if (!useShaping || !text.hasColorGlyphs() || !text.colorDirty) return;

        if (!emojiTexture) glGenTextures(1, &emojiTexture);

        int dim = text.colorAtlasSize();
        glBindTexture(GL_TEXTURE_2D, emojiTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        if (dim != emojiDimUploaded || text.colorGeneration != emojiGeneration) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dim, dim, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, text.colorAtlasPixels().data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            emojiDimUploaded = dim;
            emojiGeneration = text.colorGeneration;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dim, dim,
                            GL_RGBA, GL_UNSIGNED_BYTE, text.colorAtlasPixels().data());
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        text.colorDirty = false;
    }

    float lineHeight() const {
        return useShaping ? (text.lineHeightPx() / pixelScale) : fontSize * 1.35f;
    }
    // Height of the inked line box (no line gap) - the right value for
    // vertically centering a label inside a button or chip.
    float textHeight() const {
        return useShaping ? ((text.ascentPx() + text.descentPx()) / pixelScale) : fontSize;
    }
#else
    void syncFontAtlas() {}
    void syncEmojiAtlas() {}
    float lineHeight() const { return fontSize * 1.35f; }
    float textHeight() const { return fontSize; }
#endif

    // Center a label in a box using real font metrics instead of a hand-tuned
    // vertical offset. Never starts left of the box, so a label that is wider
    // than its box overflows to the right only.
    void addTextCenteredIn(float x, float y, float w, float h, const std::string& str, Color4 col) {
        float tw = measureText(str);
        float tx = x + (w - tw) * 0.5f;
        if (tx < x) tx = x;
        float ty = y + (h - textHeight()) * 0.5f;
        addText(std::round(tx), std::round(ty), str, col);
    }

    // Vertical centering only, caller owns x.
    void addTextVCentered(float x, float y, float h, const std::string& str, Color4 col) {
        addText(x, std::round(y + (h - textHeight()) * 0.5f), str, col);
    }

    // Soft shadow cast from a vertical edge.
    //
    // A single constant-alpha rectangle is not a shadow - it reads as a painted
    // grey stripe, which is obvious on a light theme. Real falloff needs a
    // gradient, so this lays down thin slices with quadratic decay.
    void addVerticalEdgeShadow(float edgeX, float y, float h, float width,
                               float alpha, bool towardLeft, int steps = 8) {
        if (width <= 0.0f || h <= 0.0f || alpha <= 0.0f) return;
        if (steps < 1) steps = 1;

        float sliceW = width / (float)steps;
        for (int i = 0; i < steps; ++i) {
            // t: 0 at the edge, 1 at the far end of the falloff.
            float t = ((float)i + 0.5f) / (float)steps;
            float a = alpha * (1.0f - t) * (1.0f - t);
            float x = towardLeft ? (edgeX - (float)(i + 1) * sliceW)
                                 : (edgeX + (float)i * sliceW);
            addRect(x, y, sliceW + 0.5f, h, Color4(0.0f, 0.0f, 0.0f, a));
        }
    }

    // Same idea for a horizontal edge (a bar casting a shadow onto content).
    void addHorizontalEdgeShadow(float edgeY, float x, float w, float height,
                                 float alpha, bool towardUp, int steps = 6) {
        if (height <= 0.0f || w <= 0.0f || alpha <= 0.0f) return;
        if (steps < 1) steps = 1;

        float sliceH = height / (float)steps;
        for (int i = 0; i < steps; ++i) {
            float t = ((float)i + 0.5f) / (float)steps;
            float a = alpha * (1.0f - t) * (1.0f - t);
            float y = towardUp ? (edgeY - (float)(i + 1) * sliceH)
                               : (edgeY + (float)i * sliceH);
            addRect(x, y, w, sliceH + 0.5f, Color4(0.0f, 0.0f, 0.0f, a));
        }
    }

    void beginBatch() {
        vertices.clear();
    }

    void addRect(float x, float y, float w, float h, Color4 col) {
        float x0 = x, y0 = y;
        float x1 = x + w, y1 = y + h;
        UIVertex v[6] = {
            { x0, y0, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x1, y0, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x1, y1, 0, 0, col.r, col.g, col.b, col.a, 0.0f },

            { x0, y0, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x1, y1, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x0, y1, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
        };
        vertices.insert(vertices.end(), v, v + 6);
    }

    void addRoundedRect(float x, float y, float w, float h, float radius, Color4 col) {
        radius = std::min({ radius, w * 0.5f, h * 0.5f });
        if (radius <= 1.0f) {
            addRect(x, y, w, h, col);
            return;
        }

        // Center
        addRect(x + radius, y, w - 2 * radius, h, col);
        // Left & Right
        addRect(x, y + radius, radius, h - 2 * radius, col);
        addRect(x + w - radius, y + radius, radius, h - 2 * radius, col);

        auto addCorner = [&](float cx, float cy, float startAngle) {
            const int segments = cornerSegments(radius);
            float step = (M_PI * 0.5f) / segments;
            for (int i = 0; i < segments; ++i) {
                float a1 = startAngle + i * step;
                float a2 = startAngle + (i + 1) * step;
                float x1 = cx + cosf(a1) * radius;
                float y1 = cy + sinf(a1) * radius;
                float x2 = cx + cosf(a2) * radius;
                float y2 = cy + sinf(a2) * radius;

                UIVertex tri[3] = {
                    { cx, cy, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
                    { x1, y1, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
                    { x2, y2, 0, 0, col.r, col.g, col.b, col.a, 0.0f }
                };
                vertices.insert(vertices.end(), tri, tri + 3);
            }
        };

        addCorner(x + radius, y + radius, M_PI);
        addCorner(x + w - radius, y + radius, 1.5f * M_PI);
        addCorner(x + w - radius, y + h - radius, 0.0f);
        addCorner(x + radius, y + h - radius, 0.5f * M_PI);
    }

    void addRoundedBorder(float x, float y, float w, float h, float radius, float thickness, Color4 col) {
        radius = std::min({ radius, w * 0.5f, h * 0.5f });
        if (radius <= 1.0f) {
            addBorder(x, y, w, h, thickness, col);
            return;
        }

        // 4 straight edges
        addRect(x + radius, y, w - 2 * radius, thickness, col);
        addRect(x + radius, y + h - thickness, w - 2 * radius, thickness, col);
        addRect(x, y + radius, thickness, h - 2 * radius, col);
        addRect(x + w - thickness, y + radius, thickness, h - 2 * radius, col);

        // 4 smooth corner arcs
        auto addCornerBorder = [&](float cx, float cy, float startAngle) {
            const int segments = cornerSegments(radius);
            float step = (M_PI * 0.5f) / segments;
            for (int i = 0; i < segments; ++i) {
                float a1 = startAngle + i * step;
                float a2 = startAngle + (i + 1) * step;

                float rOut = radius;
                float rIn = std::max(0.0f, radius - thickness);

                float x1_out = cx + cosf(a1) * rOut;
                float y1_out = cy + sinf(a1) * rOut;
                float x2_out = cx + cosf(a2) * rOut;
                float y2_out = cy + sinf(a2) * rOut;

                float x1_in = cx + cosf(a1) * rIn;
                float y1_in = cy + sinf(a1) * rIn;
                float x2_in = cx + cosf(a2) * rIn;
                float y2_in = cy + sinf(a2) * rIn;

                UIVertex quad[6] = {
                    { x1_out, y1_out, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
                    { x2_out, y2_out, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
                    { x2_in,  y2_in,  0, 0, col.r, col.g, col.b, col.a, 0.0f },

                    { x1_out, y1_out, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
                    { x2_in,  y2_in,  0, 0, col.r, col.g, col.b, col.a, 0.0f },
                    { x1_in,  y1_in,  0, 0, col.r, col.g, col.b, col.a, 0.0f },
                };
                vertices.insert(vertices.end(), quad, quad + 6);
            }
        };

        addCornerBorder(x + radius, y + radius, M_PI);
        addCornerBorder(x + w - radius, y + radius, 1.5f * M_PI);
        addCornerBorder(x + w - radius, y + h - radius, 0.0f);
        addCornerBorder(x + radius, y + h - radius, 0.5f * M_PI);
    }

    void addBorder(float x, float y, float w, float h, float thickness, Color4 col) {
        addRect(x, y, w, thickness, col);
        addRect(x, y + h - thickness, w, thickness, col);
        addRect(x, y + thickness, thickness, h - 2 * thickness, col);
        addRect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, col);
    }

    void addLine(float x1, float y1, float x2, float y2, float thickness, Color4 col) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = sqrtf(dx * dx + dy * dy);
        if (len <= 0.0001f) return;

        float nx = -dy / len * (thickness * 0.5f);
        float ny = dx / len * (thickness * 0.5f);

        UIVertex v[6] = {
            { x1 + nx, y1 + ny, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x2 + nx, y2 + ny, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x2 - nx, y2 - ny, 0, 0, col.r, col.g, col.b, col.a, 0.0f },

            { x1 + nx, y1 + ny, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x2 - nx, y2 - ny, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
            { x1 - nx, y1 - ny, 0, 0, col.r, col.g, col.b, col.a, 0.0f },
        };
        vertices.insert(vertices.end(), v, v + 6);
    }

    static std::string sanitizeUTF8(const std::string& input) {
        std::string output;
        output.reserve(input.size());
        size_t i = 0;
        while (i < input.size()) {
            unsigned char c = (unsigned char)input[i];
            if (c < 128) {
                output.push_back((char)c);
                i++;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < input.size()) {
                unsigned char c2 = (unsigned char)input[i + 1];
                uint32_t cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
                if (cp == 0xD7) output.push_back('x'); // ×
                else if (cp == 0xB7) output.push_back('-'); // ·
                else output.push_back(' ');
                i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < input.size()) {
                unsigned char c2 = (unsigned char)input[i + 1];
                unsigned char c3 = (unsigned char)input[i + 2];
                uint32_t cp = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                if (cp == 0x2022) output.push_back('-'); // •
                else if (cp == 0x2026) output.append("..."); // …
                else if (cp == 0x2013 || cp == 0x2014) output.push_back('-'); // – —
                else if (cp == 0x2190) output.append("<-"); // ←
                else if (cp == 0x2192) output.append("->"); // →
                else if (cp == 0x2715 || cp == 0x2716) output.push_back('x'); // ✕ ✖
                else if (cp == 0x2713 || cp == 0x2714) output.push_back('v'); // ✓ ✔
                else if (cp == 0x27E8) output.push_back('<'); // ⟨
                else if (cp == 0x27E9) output.push_back('>'); // ⟩
                else output.push_back(' ');
                i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < input.size()) {
                output.push_back(' ');
                i += 4;
            } else {
                i++;
            }
        }
        return output;
    }

    void addText(float x, float y, const std::string& str, Color4 col) {
#ifdef HAVE_HARFBUZZ
        if (useShaping) {
            // `y` stays the top of the line, matching the old stb behaviour, so
            // every existing layout constant still lines up.
            const silvertext::ShapedRun& run = text.shape(str);

            // Glyph metrics are in framebuffer pixels. Position each quad on a
            // whole *physical* pixel, then convert back to points - snapping in
            // point space would still land between texels on a scaled display.
            const float inv = 1.0f / pixelScale;
            float penPx = x * pixelScale;
            float baselinePx = y * pixelScale + text.ascentPx();

            for (const silvertext::PositionedGlyph& pg : run.glyphs) {
                const silvertext::Glyph* g = text.glyphAt(pg.glyphIndex);
                if (!g || g->width <= 0 || g->height <= 0) continue;

                float px0 = std::round(penPx + pg.x + (float)g->bearingX);
                float py0 = std::round(baselinePx - pg.y - (float)g->bearingY);

                float x0 = px0 * inv;
                float y0 = py0 * inv;
                float x1 = (px0 + (float)g->width) * inv;
                float y1 = (py0 + (float)g->height) * inv;

                // Colour glyphs sample the RGBA page and ignore the text colour.
                float mode = g->color ? 4.0f : 1.0f;

                UIVertex v[6] = {
                    { x0, y0, g->u0, g->v0, col.r, col.g, col.b, col.a, mode },
                    { x1, y0, g->u1, g->v0, col.r, col.g, col.b, col.a, mode },
                    { x1, y1, g->u1, g->v1, col.r, col.g, col.b, col.a, mode },

                    { x0, y0, g->u0, g->v0, col.r, col.g, col.b, col.a, mode },
                    { x1, y1, g->u1, g->v1, col.r, col.g, col.b, col.a, mode },
                    { x0, y1, g->u0, g->v1, col.r, col.g, col.b, col.a, mode },
                };
                vertices.insert(vertices.end(), v, v + 6);
            }
            return;
        }
#endif
        std::string cleanText = sanitizeUTF8(str);
        float curX = x;
        float curY = y + fontSize * 0.85f;

        for (char c : cleanText) {
            if (c < 32 || c > 126) c = ' ';
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, texWidth, texHeight, c - 32, &curX, &curY, &q, 1);

            UIVertex v[6] = {
                { q.x0, q.y0, q.s0, q.t0, col.r, col.g, col.b, col.a, 1.0f },
                { q.x1, q.y0, q.s1, q.t0, col.r, col.g, col.b, col.a, 1.0f },
                { q.x1, q.y1, q.s1, q.t1, col.r, col.g, col.b, col.a, 1.0f },

                { q.x0, q.y0, q.s0, q.t0, col.r, col.g, col.b, col.a, 1.0f },
                { q.x1, q.y1, q.s1, q.t1, col.r, col.g, col.b, col.a, 1.0f },
                { q.x0, q.y1, q.s0, q.t1, col.r, col.g, col.b, col.a, 1.0f },
            };
            vertices.insert(vertices.end(), v, v + 6);
        }
    }

    // Truncate to fit `maxW`, appending an ellipsis.
    //
    // Lives here so the gallery and the viewer share one implementation - they
    // previously had separate copies, one of which cut in the middle of a UTF-8
    // sequence and produced a broken glyph.
    // Longest prefix of `str` (cut on a character boundary) such that
    // prefix + suffix fits in maxW. Returns a byte count.
    size_t longestPrefixThatFits(const std::string& str, float maxW, const std::string& suffix) {
        // Candidate cut points are character starts, never byte offsets: cutting
        // mid-sequence yields a broken glyph, and snapping a byte midpoint back
        // to a boundary can fail to make progress and loop forever.
        std::vector<size_t> cuts;
        cuts.reserve(str.size() + 1);
        cuts.push_back(0);
        for (size_t i = 1; i < str.size(); ++i) {
            if (((unsigned char)str[i] & 0xC0) != 0x80) cuts.push_back(i);
        }
        cuts.push_back(str.size());

        size_t lo = 0, hi = cuts.size() - 1;
        while (lo < hi) {
            size_t mid = lo + (hi - lo + 1) / 2;   // strictly greater than lo, so this ends
            if (measureText(str.substr(0, cuts[mid]) + suffix) <= maxW) lo = mid;
            else hi = mid - 1;
        }
        return cuts[lo];
    }

    std::string fitWithEllipsis(const std::string& str, float maxW) {
        if (str.empty() || measureText(str) <= maxW) return str;

        const std::string ell = "...";
        size_t keep = longestPrefixThatFits(str, maxW, ell);
        if (keep == 0) return ell;
        return str.substr(0, keep) + ell;
    }

    // Length in bytes of the UTF-8 sequence starting at `i`.
    static size_t utf8SequenceLength(const std::string& s, size_t i) {
        if (i >= s.size()) return 0;
        unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if ((c & 0xF8) == 0xF0) len = 4;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xE0) == 0xC0) len = 2;
        if (i + len > s.size()) len = 1;
        return len;
    }

    int addWrappedText(float x, float y, const std::string& str, float maxW, float lineH, Color4 col) {
        if (str.empty()) return 0;
        std::string line = "";
        float curY = y;
        int lineCount = 0;

#ifdef HAVE_HARFBUZZ
        const std::string& source = useShaping ? str : (sanitizeCache = sanitizeUTF8(str));
#else
        const std::string& source = (sanitizeCache = sanitizeUTF8(str));
#endif

        // Advance one whole character at a time, never one byte, so multi-byte
        // sequences are never split mid-glyph.
        size_t i = 0;
        while (i < source.size()) {
            size_t len = utf8SequenceLength(source, i);
            std::string ch = source.substr(i, len);
            i += len;

            if (ch == "\n") {
                addText(x, curY, line, col);
                line.clear();
                curY += lineH;
                lineCount++;
                continue;
            }

            if (measureText(line + ch) > maxW && !line.empty()) {
                addText(x, curY, line, col);
                line = ch;
                curY += lineH;
                lineCount++;
            } else {
                line += ch;
            }
        }
        if (!line.empty()) {
            addText(x, curY, line, col);
            lineCount++;
        }
        return lineCount;
    }

    std::string sanitizeCache;   // backing store for the fallback wrap path

    float measureText(const std::string& str) {
#ifdef HAVE_HARFBUZZ
        if (useShaping) return text.measure(str) / pixelScale;
#endif
        std::string cleanText = sanitizeUTF8(str);
        float x = 0.0f;
        for (char c : cleanText) {
            if (c < 32 || c > 126) {
                x += cdata[' ' - 32].xadvance;
                continue;
            }
            x += cdata[c - 32].xadvance;
        }
        return x;
    }

    // Open a frame. Everything rendered until endFrame() is coalesced.
    void beginFrame(int windowWidth, int windowHeight) {
        frameActive = true;
        frameW = windowWidth;
        frameH = windowHeight;
        frameVertices.clear();
        frameCmds.clear();
        batchesLastFrame = 0;
        clipActive = false;
    }

    // Can this command be folded into the previous one? Only textures the shader
    // will actually sample matter: a batch that binds nothing is compatible with
    // anything, and a batch that already owns a texture accepts more geometry
    // needing that same texture.
    static bool texCompatible(GLuint existing, GLuint wanted) {
        return existing == 0 || wanted == 0 || existing == wanted;
    }

    void appendToFrame(GLuint customTex, GLuint iconTex) {
        if (vertices.empty()) return;

        size_t first = frameVertices.size();
        frameVertices.insert(frameVertices.end(), vertices.begin(), vertices.end());
        batchesLastFrame++;

        if (!frameCmds.empty()) {
            DrawCmd& last = frameCmds.back();
            bool sameClip = (last.clip == clipActive) &&
                            (!clipActive || (last.cx == clipX && last.cy == clipY &&
                                             last.cw == clipW && last.ch == clipH));
            if (sameClip && texCompatible(last.customTex, customTex) &&
                texCompatible(last.iconTex, iconTex)) {
                if (customTex) last.customTex = customTex;
                if (iconTex) last.iconTex = iconTex;
                last.count += vertices.size();
                return;
            }
        }

        DrawCmd cmd;
        cmd.customTex = customTex;
        cmd.iconTex = iconTex;
        cmd.first = first;
        cmd.count = vertices.size();
        cmd.clip = clipActive;
        cmd.cx = clipX; cmd.cy = clipY; cmd.cw = clipW; cmd.ch = clipH;
        frameCmds.push_back(cmd);
    }

    // Upload the whole frame once and issue the merged draw commands.
    void endFrame() {
        if (!frameActive) return;
        frameActive = false;

        drawCallsLastFrame = 0;
        verticesLastFrame = frameVertices.size();
        if (frameVertices.empty() || frameCmds.empty()) return;

        // Any glyph shaped during this frame must reach the GPU before we draw.
        syncFontAtlas();
        syncEmojiAtlas();

        glUseProgram(shaderProgram);
        setProjection(frameW, frameH);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTexture);
        glUniform1i(uFontTextureLoc, 0);
        glUniform1i(uCustomTextureLoc, 1);
        glUniform1i(uIconTextureLoc, 2);
        glUniform1i(uEmojiTextureLoc, 3);
        if (emojiTexture) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, emojiTexture);
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        const size_t uploadBytes = frameVertices.size() * sizeof(UIVertex);
        size_t streamOffset = 0;
        bool usedPersistentUpload = false;
        if (persistentMapped && uploadBytes <= streamSegmentBytes) {
            usedPersistentUpload = true;
            streamSegment = (streamSegment + 1) & 3;
            GLsync& fence = streamFences[streamSegment];
            if (fence) {
                // Usually already signalled after three intervening frames.
                // A short bounded wait is still cheaper than reallocating the
                // buffer or overwriting vertices the GPU is consuming.
                GLenum state = glClientWaitSync(fence, 0, 0);
                if (state == GL_TIMEOUT_EXPIRED) {
                    state = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000);
                    if (state == GL_TIMEOUT_EXPIRED)
                        glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                                         GL_TIMEOUT_IGNORED);
                }
                glDeleteSync(fence);
                fence = nullptr;
            }
            streamOffset = (size_t)streamSegment * streamSegmentBytes;
            memcpy(persistentMapped + streamOffset, frameVertices.data(), uploadBytes);
        } else {
            // OpenGL < 4.4 fallback: orphaning remains non-blocking on all
            // supported drivers.
            if (persistentMapped) {
                if (!overflowVbo) glGenBuffers(1, &overflowVbo);
                glBindBuffer(GL_ARRAY_BUFFER, overflowVbo);
            }
            glBufferData(GL_ARRAY_BUFFER, uploadBytes, nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, uploadBytes, frameVertices.data());
        }

        const uintptr_t base = (uintptr_t)streamOffset;
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
                              (void*)(base + offsetof(UIVertex, x)));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
                              (void*)(base + offsetof(UIVertex, u)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
                              (void*)(base + offsetof(UIVertex, r)));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
                              (void*)(base + offsetof(UIVertex, useTex)));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        // Scissor works in framebuffer pixels from the bottom-left, while the UI
        // is laid out in window points from the top-left, so the real viewport
        // is needed to convert. Cached: glGetIntegerv is a driver round-trip and
        // the viewport only changes on resize.
        GLint* vp = cachedViewport;
        if (!viewportValid) {
            glGetIntegerv(GL_VIEWPORT, cachedViewport);
            viewportValid = true;
        }
        float sx = (frameW > 0) ? (float)vp[2] / (float)frameW : 1.0f;
        float sy = (frameH > 0) ? (float)vp[3] / (float)frameH : 1.0f;

        GLuint boundCustom = 0;
        GLuint boundIcon = 0;
        bool scissorOn = false;
        for (const DrawCmd& cmd : frameCmds) {
            if (cmd.clip) {
                if (!scissorOn) { glEnable(GL_SCISSOR_TEST); scissorOn = true; }
                GLint rx = vp[0] + (GLint)std::floor(cmd.cx * sx);
                GLint rw = (GLint)std::ceil(cmd.cw * sx);
                GLint rh = (GLint)std::ceil(cmd.ch * sy);
                GLint ry = vp[1] + vp[3] - (GLint)std::ceil((cmd.cy + cmd.ch) * sy);
                glScissor(rx, ry, std::max(0, rw), std::max(0, rh));
            } else if (scissorOn) {
                glDisable(GL_SCISSOR_TEST);
                scissorOn = false;
            }
            if (cmd.customTex && cmd.customTex != boundCustom) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, cmd.customTex);
                boundCustom = cmd.customTex;
            }
            if (cmd.iconTex && cmd.iconTex != boundIcon) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, cmd.iconTex);
                boundIcon = cmd.iconTex;
            }
            glDrawArrays(GL_TRIANGLES, (GLint)cmd.first, (GLsizei)cmd.count);
            drawCallsLastFrame++;
        }
        if (usedPersistentUpload)
            streamFences[streamSegment] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (scissorOn) glDisable(GL_SCISSOR_TEST);

        glBindVertexArray(0);
        glUseProgram(0);
        frameVertices.clear();
        frameCmds.clear();
    }

    void setProjection(int windowWidth, int windowHeight) {
        float L = 0.0f, R = (float)windowWidth;
        float T = 0.0f, B = (float)windowHeight;
        float N = -1.0f, F = 1.0f;

        float proj[16] = {
            2.0f / (R - L), 0.0f,           0.0f,          0.0f,
            0.0f,           2.0f / (T - B), 0.0f,          0.0f,
            0.0f,           0.0f,          -2.0f / (F - N), 0.0f,
            -(R + L)/(R-L), -(T + B)/(T-B), -(F + N)/(F-N), 1.0f
        };
        glUniformMatrix4fv(uProjectionLoc, 1, GL_FALSE, proj);
    }

    void render(int windowWidth, int windowHeight, GLuint customTex = 0, GLuint iconTex = 0) {
        if (vertices.empty()) return;

        // Inside a frame this only queues work; the GPU is touched once, in
        // endFrame().
        if (frameActive) {
            // Deliberately does not clear `vertices` - see below.
            // Deliberately does not clear `vertices` - the immediate path never
            // did either, so any call site that renders twice without an
            // intervening beginBatch() keeps producing identical output.
            appendToFrame(customTex, iconTex);
            return;
        }

        syncFontAtlas();
        syncEmojiAtlas();
        glUseProgram(shaderProgram);

        float L = 0.0f, R = (float)windowWidth;
        float T = 0.0f, B = (float)windowHeight;
        float N = -1.0f, F = 1.0f;

        float proj[16] = {
            2.0f / (R - L), 0.0f,           0.0f,          0.0f,
            0.0f,           2.0f / (T - B), 0.0f,          0.0f,
            0.0f,           0.0f,          -2.0f / (F - N), 0.0f,
            -(R + L)/(R-L), -(T + B)/(T-B), -(F + N)/(F-N), 1.0f
        };

        glUniformMatrix4fv(uProjectionLoc, 1, GL_FALSE, proj);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTexture);
        glUniform1i(uFontTextureLoc, 0);

        if (customTex) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, customTex);
            glUniform1i(uCustomTextureLoc, 1);
        }

        if (iconTex) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, iconTex);
            glUniform1i(uIconTextureLoc, 2);
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(UIVertex), vertices.data(), GL_DYNAMIC_DRAW);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

        glBindVertexArray(0);
        glUseProgram(0);
    }
};
