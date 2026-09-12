#include "gl_texture.hpp"

#include <algorithm>
#include <vector>

GLTexture::GLTexture() {
    glGenTextures(1, &id_);
}

GLTexture::~GLTexture() {
    Release();
}

void GLTexture::Release() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
}

void GLTexture::Upload(const unsigned char* rgba, int width, int height) {
    if (id_ == 0) {
        glGenTextures(1, &id_);
    }

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    const unsigned char* uploadData = rgba;
    int uploadWidth = width;
    int uploadHeight = height;
    std::vector<unsigned char> scaled;

    const int longestSide = std::max(width, height);
    if (maxTextureSize > 0 && longestSide > maxTextureSize) {
        const double scale = static_cast<double>(maxTextureSize) / longestSide;
        uploadWidth = std::max(1, static_cast<int>(width * scale));
        uploadHeight = std::max(1, static_cast<int>(height * scale));

        scaled.resize(static_cast<size_t>(uploadWidth) * uploadHeight * 4);
        for (int y = 0; y < uploadHeight; ++y) {
            const int srcY = std::min(height - 1, static_cast<int>(y / scale));
            for (int x = 0; x < uploadWidth; ++x) {
                const int srcX = std::min(width - 1, static_cast<int>(x / scale));
                const size_t srcIndex = (static_cast<size_t>(srcY) * width + srcX) * 4;
                const size_t dstIndex = (static_cast<size_t>(y) * uploadWidth + x) * 4;
                scaled[dstIndex + 0] = rgba[srcIndex + 0];
                scaled[dstIndex + 1] = rgba[srcIndex + 1];
                scaled[dstIndex + 2] = rgba[srcIndex + 2];
                scaled[dstIndex + 3] = rgba[srcIndex + 3];
            }
        }
        uploadData = scaled.data();
    }

    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, uploadWidth, uploadHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploadData);
    glBindTexture(GL_TEXTURE_2D, 0);

    displayWidth_ = uploadWidth;
    displayHeight_ = uploadHeight;
}
