#pragma once

#include <glad/gl.h>

// OpenGLテクスチャのRAIIラッパー。GL_MAX_TEXTURE_SIZEを超える画像は
// 表示用に縮小したデータを別途アップロードする（保存用データは変更しない）。
class GLTexture {
public:
    GLTexture();
    ~GLTexture();

    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    void Upload(const unsigned char* rgba, int width, int height);
    void Release();

    GLuint Id() const { return id_; }
    int Width() const { return displayWidth_; }
    int Height() const { return displayHeight_; }
    bool IsValid() const { return id_ != 0; }

private:
    GLuint id_ = 0;
    int displayWidth_ = 0;
    int displayHeight_ = 0;
};
