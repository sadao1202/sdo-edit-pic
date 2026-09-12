#include "image_io.hpp"

#include <algorithm>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace image_io {

namespace {

// stbi_write_*_to_funcのコールバック用コンテキスト。
struct WriteContext {
    FILE* file = nullptr;
    bool writeFailed = false;
};

// stbi_write_*_to_funcのコールバック。FILE*に書き込む。
void WriteToFile(void* context, void* data, int size) {
    auto* ctx = static_cast<WriteContext*>(context);
    const size_t written = std::fwrite(data, 1, static_cast<size_t>(size), ctx->file);
    if (written != static_cast<size_t>(size)) {
        ctx->writeFailed = true;
    }
}

}  // namespace

std::optional<ImageDocument> LoadImage(const std::wstring& path, std::string& outError) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) {
        outError = "ファイルを開けませんでした。";
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* data = stbi_load_from_file(file, &width, &height, &sourceChannels, 4);
    std::fclose(file);

    if (!data) {
        const char* reason = stbi_failure_reason();
        outError = reason ? std::string("画像を読み込めませんでした: ") + reason
                           : "画像を読み込めませんでした。";
        return std::nullopt;
    }

    ImageDocument doc;
    doc.width = width;
    doc.height = height;
    doc.channels = 4;
    doc.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
    doc.sourcePath = path;
    doc.hasAlpha = ImageDocument::ComputeHasAlpha(doc.pixels);

    stbi_image_free(data);
    return doc;
}

bool SaveAsPng(const ImageDocument& doc, const std::wstring& path, std::string& outError) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) {
        outError = "保存先ファイルを開けませんでした。";
        return false;
    }

    WriteContext ctx;
    ctx.file = file;
    int result = stbi_write_png_to_func(WriteToFile, &ctx, doc.width, doc.height, 4,
                                         doc.pixels.data(), doc.width * 4);
    std::fclose(file);

    if (result == 0 || ctx.writeFailed) {
        outError = "PNGの書き込みに失敗しました。";
        return false;
    }
    return true;
}

bool SaveAsJpeg(const ImageDocument& doc, const std::wstring& path, int quality, std::string& outError) {
    quality = std::clamp(quality, 1, 100);

    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) {
        outError = "保存先ファイルを開けませんでした。";
        return false;
    }

    std::vector<unsigned char> rgb;
    const unsigned char* sourceData = nullptr;

    if (doc.HasAlpha()) {
        rgb.resize(static_cast<size_t>(doc.width) * doc.height * 3);
        for (size_t i = 0; i < static_cast<size_t>(doc.width) * doc.height; ++i) {
            const unsigned char r = doc.pixels[i * 4 + 0];
            const unsigned char g = doc.pixels[i * 4 + 1];
            const unsigned char b = doc.pixels[i * 4 + 2];
            const unsigned char a = doc.pixels[i * 4 + 3];
            const float alpha = a / 255.0f;
            rgb[i * 3 + 0] = static_cast<unsigned char>(r * alpha + 255.0f * (1.0f - alpha));
            rgb[i * 3 + 1] = static_cast<unsigned char>(g * alpha + 255.0f * (1.0f - alpha));
            rgb[i * 3 + 2] = static_cast<unsigned char>(b * alpha + 255.0f * (1.0f - alpha));
        }
        sourceData = rgb.data();
    } else {
        rgb.resize(static_cast<size_t>(doc.width) * doc.height * 3);
        for (size_t i = 0; i < static_cast<size_t>(doc.width) * doc.height; ++i) {
            rgb[i * 3 + 0] = doc.pixels[i * 4 + 0];
            rgb[i * 3 + 1] = doc.pixels[i * 4 + 1];
            rgb[i * 3 + 2] = doc.pixels[i * 4 + 2];
        }
        sourceData = rgb.data();
    }

    WriteContext ctx;
    ctx.file = file;
    int result = stbi_write_jpg_to_func(WriteToFile, &ctx, doc.width, doc.height, 3, sourceData, quality);
    std::fclose(file);

    if (result == 0 || ctx.writeFailed) {
        outError = "JPEGの書き込みに失敗しました。";
        return false;
    }
    return true;
}

}  // namespace image_io
