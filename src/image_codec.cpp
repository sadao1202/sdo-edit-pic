#include "image_codec.hpp"

#include <algorithm>
#include <cstddef>

#include "stb_image.h"
#include "stb_image_write.h"

namespace image_codec {

namespace {

// docの寸法・pixelsサイズが整合しているかを検証する。不正なら範囲外アクセスを
// 避けるためfalseを返す。
bool IsValidDoc(const ImageDocument& doc) {
    if (doc.width <= 0 || doc.height <= 0) {
        return false;
    }
    const size_t expected = static_cast<size_t>(doc.width) * static_cast<size_t>(doc.height) * 4;
    return doc.pixels.size() == expected;
}

// stbi_write_jpg_to_funcのコールバック用コンテキスト。メモリバッファに追記する。
struct MemoryWriteContext {
    std::vector<unsigned char>* buffer = nullptr;
};

void WriteToMemory(void* context, void* data, int size) {
    auto* ctx = static_cast<MemoryWriteContext*>(context);
    const auto* bytes = static_cast<unsigned char*>(data);
    ctx->buffer->insert(ctx->buffer->end(), bytes, bytes + size);
}

}  // namespace

std::vector<unsigned char> CompositeToRgb(const ImageDocument& doc) {
    std::vector<unsigned char> rgb;
    if (!IsValidDoc(doc)) {
        return rgb;
    }
    const size_t pixelCount = static_cast<size_t>(doc.width) * static_cast<size_t>(doc.height);
    rgb.resize(pixelCount * 3);

    if (doc.HasAlpha()) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const unsigned char r = doc.pixels[i * 4 + 0];
            const unsigned char g = doc.pixels[i * 4 + 1];
            const unsigned char b = doc.pixels[i * 4 + 2];
            const unsigned char a = doc.pixels[i * 4 + 3];
            const float alpha = a / 255.0f;
            rgb[i * 3 + 0] = static_cast<unsigned char>(r * alpha + 255.0f * (1.0f - alpha));
            rgb[i * 3 + 1] = static_cast<unsigned char>(g * alpha + 255.0f * (1.0f - alpha));
            rgb[i * 3 + 2] = static_cast<unsigned char>(b * alpha + 255.0f * (1.0f - alpha));
        }
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            rgb[i * 3 + 0] = doc.pixels[i * 4 + 0];
            rgb[i * 3 + 1] = doc.pixels[i * 4 + 1];
            rgb[i * 3 + 2] = doc.pixels[i * 4 + 2];
        }
    }
    return rgb;
}

bool EncodeJpegToMemory(const ImageDocument& doc, int quality, std::vector<unsigned char>& outBuffer,
                         std::string& outError) {
    outBuffer.clear();
    if (!IsValidDoc(doc)) {
        outError = "不正な画像データです。";
        return false;
    }
    quality = std::clamp(quality, 1, 100);

    const std::vector<unsigned char> rgb = CompositeToRgb(doc);
    if (rgb.empty()) {
        outError = "不正な画像データです。";
        return false;
    }

    MemoryWriteContext ctx;
    ctx.buffer = &outBuffer;
    const int result =
        stbi_write_jpg_to_func(WriteToMemory, &ctx, doc.width, doc.height, 3, rgb.data(), quality);
    if (result == 0 || outBuffer.empty()) {
        outError = "JPEGのエンコードに失敗しました。";
        outBuffer.clear();
        return false;
    }
    return true;
}

std::optional<ImageDocument> MakeJpegRoundTrip(const ImageDocument& doc, int quality, std::string& outError) {
    if (!IsValidDoc(doc)) {
        outError = "不正な画像データです。";
        return std::nullopt;
    }

    std::vector<unsigned char> encoded;
    if (!EncodeJpegToMemory(doc, quality, encoded, outError)) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* data = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width,
                                                 &height, &sourceChannels, 4);
    if (!data) {
        const char* reason = stbi_failure_reason();
        outError = reason ? std::string("JPEGのデコードに失敗しました: ") + reason
                           : "JPEGのデコードに失敗しました。";
        return std::nullopt;
    }

    ImageDocument result;
    result.width = width;
    result.height = height;
    result.channels = 4;
    result.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
    result.sourcePath = doc.sourcePath;
    result.hasAlpha = false;
    stbi_image_free(data);
    return result;
}

}  // namespace image_codec
