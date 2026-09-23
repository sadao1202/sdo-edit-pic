#include "image_io.hpp"

#include <algorithm>
#include <cstdio>

#include "image_codec.hpp"
#include "stb_image.h"
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

    const std::vector<unsigned char> rgb = image_codec::CompositeToRgb(doc);
    if (rgb.empty()) {
        std::fclose(file);
        outError = "不正な画像データです。";
        return false;
    }

    WriteContext ctx;
    ctx.file = file;
    int result = stbi_write_jpg_to_func(WriteToFile, &ctx, doc.width, doc.height, 3, rgb.data(), quality);
    std::fclose(file);

    if (result == 0 || ctx.writeFailed) {
        outError = "JPEGの書き込みに失敗しました。";
        return false;
    }
    return true;
}

}  // namespace image_io
