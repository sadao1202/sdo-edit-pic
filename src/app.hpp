#pragma once

#include <optional>
#include <string>

#include "gl_texture.hpp"
#include "image_document.hpp"

// ImGui UI構築と状態管理。
class App {
public:
    void OnFrame();

private:
    void OnOpenClicked();
    void OnConvertClicked();

    std::optional<ImageDocument> document_;
    GLTexture texture_;
    std::wstring selectedPath_;

    int jpegQuality_ = 90;
    std::string statusMessage_;
    bool statusIsError_ = false;
};
