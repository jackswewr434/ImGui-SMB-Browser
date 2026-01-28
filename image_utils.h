#pragma once

#include <GL/glew.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "images.h"
#include "stb_image.h"

static GLuint folder_texture = 0;
static GLuint file_texture = 0;

static GLuint LoadTextureFromMemory(const unsigned char* data, size_t size) {
    if (!data || size == 0) return 0;
    int w = 0, h = 0, channels = 0;
    // Try to decode using stb_image if implemented
    unsigned char* img = stbi_load_from_memory((const stbi_uc*)data, (int)size, &w, &h, &channels, 4);
    if (!img) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(img);
    return tex;
}

inline void InitImageTextures() {
    if (!folder_texture) folder_texture = LoadTextureFromMemory(folder_img, sizeof(folder_img));
    if (!file_texture) file_texture = LoadTextureFromMemory(file_img, sizeof(file_img));
    // Fallback: if decoding failed (no stb implementation), create simple colored placeholders
    auto create_solid = [](unsigned char r, unsigned char g, unsigned char b) -> GLuint {
        const int S = 16;
        std::vector<unsigned char> pixels(S * S * 4);
        for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
            int idx = (y * S + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 255;
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    };
    if (!folder_texture) folder_texture = create_solid(0x66, 0x99, 0xFF); // blue-ish folder
    if (!file_texture) file_texture = create_solid(0xFF, 0xCC, 0x66); // yellow-ish file
}

inline void FreeImageTextures() {
    if (folder_texture) { glDeleteTextures(1, &folder_texture); folder_texture = 0; }
    if (file_texture) { glDeleteTextures(1, &file_texture); file_texture = 0; }
}

inline GLuint GetFolderTexture() { return folder_texture; }
inline GLuint GetFileTexture() { return file_texture; }
