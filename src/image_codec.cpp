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

}  // namespace image_codec
