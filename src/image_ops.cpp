#include "image_ops.hpp"

#include <algorithm>
#include <cstring>

namespace image_ops {

std::optional<ImageDocument> CropImage(const ImageDocument& doc,
                                        int left, int top, int right, int bottom) {
    left = std::clamp(left, 0, doc.width);
    right = std::clamp(right, 0, doc.width);
    top = std::clamp(top, 0, doc.height);
    bottom = std::clamp(bottom, 0, doc.height);

    if (right <= left || bottom <= top) {
        return std::nullopt;
    }

    const int width = right - left;
    const int height = bottom - top;

    ImageDocument result;
    result.width = width;
    result.height = height;
    result.channels = 4;
    result.pixels.resize(static_cast<size_t>(width) * height * 4);
    result.sourcePath = doc.sourcePath;

    const size_t srcStride = static_cast<size_t>(doc.width) * 4;
    const size_t dstStride = static_cast<size_t>(width) * 4;
    for (int row = 0; row < height; ++row) {
        const unsigned char* srcRow =
            doc.pixels.data() + static_cast<size_t>(top + row) * srcStride + static_cast<size_t>(left) * 4;
        unsigned char* dstRow = result.pixels.data() + static_cast<size_t>(row) * dstStride;
        std::memcpy(dstRow, srcRow, dstStride);
    }

    result.hasAlpha = ImageDocument::ComputeHasAlpha(result.pixels);
    return result;
}

}  // namespace image_ops
