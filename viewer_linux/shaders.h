#pragma once

#include "gl_loader.h"
#include <stdio.h>

class ImageShader {
public:
    GLuint program = 0;
    GLuint bgProgram = 0;
    GLuint vao = 0, vbo = 0;

    GLint uWindowSizeLoc = -1;
    GLint uImagePosLoc = -1;
    GLint uImageSizeLoc = -1;
    GLint uScaleLoc = -1;
    GLint uRotationLoc = -1;
    GLint uBgModeLoc = -1;
    GLint uCheckerSizeLoc = -1;
    GLint uPixelGridLoc = -1;
    GLint uTextureLoc = -1;

    GLint uBgWinSizeLoc = -1;
    GLint uBgModeBgLoc = -1;
    GLint uBgIsDarkLoc = -1;

    bool init() {
        // -------------------------------------------------------------
        // 1. Image Quad Shader
        // -------------------------------------------------------------
        const char* vsSource = R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            layout (location = 1) in vec2 aUV;

            uniform vec2 uWindowSize;
            uniform vec2 uImagePos;
            uniform vec2 uImageSize;
            uniform float uScale;
            uniform int uRotation;

            out vec2 vUV;
            out vec2 vPixelCoord;

            void main() {
                vUV = aUV;
                vPixelCoord = aUV * uImageSize;

                // Rotate local coords around image center (0.5, 0.5)
                vec2 local = aPos - vec2(0.5, 0.5);
                float rad = radians(float(uRotation));
                float cosA = cos(rad);
                float sinA = sin(rad);
                vec2 rotPos = vec2(local.x * cosA - local.y * sinA, local.x * sinA + local.y * cosA);

                // World pixel position centered on uImagePos
                vec2 worldPx = uImagePos + rotPos * (uImageSize * uScale);

                // Convert from window pixel space (0..W, 0..H) to NDC (-1..1, 1..-1)
                vec2 ndc = vec2((worldPx.x / uWindowSize.x) * 2.0 - 1.0,
                                1.0 - (worldPx.y / uWindowSize.y) * 2.0);

                gl_Position = vec4(ndc, 0.0, 1.0);
            }
        )";

        const char* fsSource = R"(
            #version 330 core
            in vec2 vUV;
            in vec2 vPixelCoord;

            uniform sampler2D uTexture;
            uniform vec2 uImageSize;
            uniform float uScale;
            uniform int uBgMode;
            uniform float uCheckerSize;
            uniform int uPixelGrid;

            out vec4 FragColor;

            vec4 getCheckerboard(vec2 coord) {
                if (uBgMode == 2) return vec4(0.0, 0.0, 0.0, 1.0);
                if (uBgMode == 3) return vec4(1.0, 1.0, 1.0, 1.0);
                vec2 check = floor(coord / uCheckerSize);
                float pattern = mod(check.x + check.y, 2.0);
                vec3 c1 = vec3(0.14, 0.14, 0.16);
                vec3 c2 = vec3(0.09, 0.09, 0.11);
                return vec4(mix(c1, c2, pattern), 1.0);
            }

            void main() {
                if (vUV.x < 0.0 || vUV.x > 1.0 || vUV.y < 0.0 || vUV.y > 1.0) {
                    discard;
                }

                vec4 texColor = texture(uTexture, vUV);

                // Blend with checkerboard underneath image alpha
                vec4 bg = getCheckerboard(gl_FragCoord.xy);
                vec4 finalColor = vec4(mix(bg.rgb, texColor.rgb, texColor.a), 1.0);

                // Pixel Grid Overlay (at scale >= 8.0)
                if (uPixelGrid == 1 && uScale >= 8.0) {
                    vec2 grid = fract(vPixelCoord);
                    vec2 gridDist = min(grid, 1.0 - grid) * uScale;
                    float minEdge = min(gridDist.x, gridDist.y);
                    if (minEdge < 1.0) {
                        float gridAlpha = (1.0 - minEdge) * 0.25;
                        finalColor.rgb = mix(finalColor.rgb, vec3(1.0, 1.0, 1.0), gridAlpha);
                    }
                }

                FragColor = finalColor;
            }
        )";

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsSource, nullptr);
        glCompileShader(vs);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsSource, nullptr);
        glCompileShader(fs);

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        glDeleteShader(vs);
        glDeleteShader(fs);

        uWindowSizeLoc = glGetUniformLocation(program, "uWindowSize");
        uImagePosLoc = glGetUniformLocation(program, "uImagePos");
        uImageSizeLoc = glGetUniformLocation(program, "uImageSize");
        uScaleLoc = glGetUniformLocation(program, "uScale");
        uRotationLoc = glGetUniformLocation(program, "uRotation");
        uBgModeLoc = glGetUniformLocation(program, "uBgMode");
        uCheckerSizeLoc = glGetUniformLocation(program, "uCheckerSize");
        uPixelGridLoc = glGetUniformLocation(program, "uPixelGrid");
        uTextureLoc = glGetUniformLocation(program, "uTexture");

        // -------------------------------------------------------------
        // 2. Fullscreen Background Canvas Checkerboard Shader
        // -------------------------------------------------------------
        const char* bgVsSource = R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            void main() {
                gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);
            }
        )";

        const char* bgFsSource = R"(
            #version 330 core
            uniform vec2 uWindowSize;
            uniform int uBgMode;
            uniform int uIsDark;

            out vec4 FragColor;

            void main() {
                if (uBgMode == 2) {
                    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                    return;
                }
                if (uBgMode == 3) {
                    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
                    return;
                }

                // FilePilot-style subtle checkerboard grid
                vec2 check = floor(gl_FragCoord.xy / 28.0);
                float pattern = mod(check.x + check.y, 2.0);

                if (uIsDark == 1) {
                    vec3 c1 = vec3(0.09, 0.09, 0.11);
                    vec3 c2 = vec3(0.065, 0.065, 0.08);
                    FragColor = vec4(mix(c1, c2, pattern), 1.0);
                } else {
                    vec3 c1 = vec3(0.95, 0.96, 0.97);
                    vec3 c2 = vec3(0.89, 0.91, 0.93);
                    FragColor = vec4(mix(c1, c2, pattern), 1.0);
                }
            }
        )";

        GLuint bgVs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(bgVs, 1, &bgVsSource, nullptr);
        glCompileShader(bgVs);

        GLuint bgFs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(bgFs, 1, &bgFsSource, nullptr);
        glCompileShader(bgFs);

        bgProgram = glCreateProgram();
        glAttachShader(bgProgram, bgVs);
        glAttachShader(bgProgram, bgFs);
        glLinkProgram(bgProgram);

        glDeleteShader(bgVs);
        glDeleteShader(bgFs);

        uBgWinSizeLoc = glGetUniformLocation(bgProgram, "uWindowSize");
        uBgModeBgLoc = glGetUniformLocation(bgProgram, "uBgMode");
        uBgIsDarkLoc = glGetUniformLocation(bgProgram, "uIsDark");

        // Quad geometry (unit quad 0..1 in Pos and UV)
        float quadVertices[] = {
            // Pos (X, Y)   UV (U, V)
            0.0f, 0.0f,     0.0f, 0.0f,
            1.0f, 0.0f,     1.0f, 0.0f,
            1.0f, 1.0f,     1.0f, 1.0f,

            0.0f, 0.0f,     0.0f, 0.0f,
            1.0f, 1.0f,     1.0f, 1.0f,
            0.0f, 1.0f,     0.0f, 1.0f,
        };

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);
        return true;
    }

    void drawBackground(int windowW, int windowH, int bgMode, bool isDark) {
        glUseProgram(bgProgram);
        glUniform2f(uBgWinSizeLoc, (float)windowW, (float)windowH);
        glUniform1i(uBgModeBgLoc, bgMode);
        glUniform1i(uBgIsDarkLoc, isDark ? 1 : 0);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glUseProgram(0);
    }

    void draw(int windowW, int windowH,
              float posX, float posY,
              float imgW, float imgH,
              float scale, int rotation,
              int bgMode, bool pixelGrid,
              GLuint textureID) {
        if (!textureID) return;

        glUseProgram(program);

        glUniform2f(uWindowSizeLoc, (float)windowW, (float)windowH);
        glUniform2f(uImagePosLoc, posX, posY);
        glUniform2f(uImageSizeLoc, imgW, imgH);
        glUniform1f(uScaleLoc, scale);
        glUniform1i(uRotationLoc, rotation);
        glUniform1i(uBgModeLoc, bgMode);
        glUniform1f(uCheckerSizeLoc, 24.0f);
        glUniform1i(uPixelGridLoc, pixelGrid ? 1 : 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glUniform1i(uTextureLoc, 0);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glUseProgram(0);
    }
};
