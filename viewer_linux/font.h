#pragma once

#include "gl_loader.h"
#include "theme.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

struct UIVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float useTex; // 0 = solid color, 1 = font alpha, 2 = rgba texture, 3 = icon atlas alpha
};

class FontRenderer {
public:
    GLuint fontTexture = 0;
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

    std::vector<UIVertex> vertices;

    bool init(const char* fontPath = nullptr, float size = 15.0f) {
        fontSize = size;
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

        const char* fsSource = R"(
            #version 330 core
            in vec2 vUV;
            in vec4 vCol;
            in float vUseTex;
            uniform sampler2D uFontTexture;
            uniform sampler2D uCustomTexture;
            uniform sampler2D uIconTexture;
            out vec4 FragColor;
            void main() {
                if (vUseTex > 2.5) { // Icon Atlas Alpha
                    float alpha = texture(uIconTexture, vUV).a;
                    FragColor = vec4(vCol.rgb, vCol.a * alpha);
                } else if (vUseTex > 1.5) { // Custom Texture (Thumbnail)
                    vec4 tex = texture(uCustomTexture, vUV);
                    FragColor = vec4(tex.rgb, tex.a * vCol.a);
                } else if (vUseTex > 0.5) { // Font Alpha
                    float alpha = texture(uFontTexture, vUV).a;
                    FragColor = vec4(vCol.rgb, vCol.a * alpha);
                } else { // Solid Color
                    FragColor = vCol;
                }
            }
        )";

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

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

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
            const int segments = 8;
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
            const int segments = 8;
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

    void addText(float x, float y, const std::string& text, Color4 col) {
        std::string cleanText = sanitizeUTF8(text);
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

    int addWrappedText(float x, float y, const std::string& text, float maxW, float lineH, Color4 col) {
        if (text.empty()) return 0;
        std::string line = "";
        float curY = y;
        int lineCount = 0;
        std::string cleanText = sanitizeUTF8(text);

        for (size_t i = 0; i < cleanText.size(); ++i) {
            char c = cleanText[i];
            if (c == '\n') {
                addText(x, curY, line, col);
                line = "";
                curY += lineH;
                lineCount++;
                continue;
            }
            std::string test = line + c;
            if (measureText(test) > maxW && !line.empty()) {
                addText(x, curY, line, col);
                line = std::string(1, c);
                curY += lineH;
                lineCount++;
            } else {
                line += c;
            }
        }
        if (!line.empty()) {
            addText(x, curY, line, col);
            lineCount++;
        }
        return lineCount;
    }

    float measureText(const std::string& text) {
        std::string cleanText = sanitizeUTF8(text);
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

    void render(int windowWidth, int windowHeight, GLuint customTex = 0, GLuint iconTex = 0) {
        if (vertices.empty()) return;

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
