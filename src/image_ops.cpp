#include "image_ops.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace image_ops {

namespace {

// マスク上に中心(cx, cy)・半径radiusの円を描く（値255）。画像外はクリップする。
void PaintCircle(std::vector<unsigned char>& mask, int width, int height, int cx, int cy, int radius) {
    const int left = std::max(0, cx - radius);
    const int right = std::min(width - 1, cx + radius);
    const int top = std::max(0, cy - radius);
    const int bottom = std::min(height - 1, cy + radius);
    const int radiusSq = radius * radius;
    for (int y = top; y <= bottom; ++y) {
        const int dy = y - cy;
        unsigned char* row = mask.data() + static_cast<size_t>(y) * width;
        for (int x = left; x <= right; ++x) {
            const int dx = x - cx;
            if (dx * dx + dy * dy <= radiusSq) {
                row[x] = 255;
            }
        }
    }
}

}  // namespace

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

bool ApplyMosaic(ImageDocument& doc, const std::vector<unsigned char>& mask, int blockSize) {
    if (doc.width <= 0 || doc.height <= 0 || blockSize <= 0) {
        return false;
    }
    if (mask.size() != static_cast<size_t>(doc.width) * static_cast<size_t>(doc.height)) {
        return false;
    }

    const int width = doc.width;
    const int height = doc.height;
    const size_t stride = static_cast<size_t>(width) * 4;

    for (int blockTop = 0; blockTop < height; blockTop += blockSize) {
        const int blockBottom = std::min(height, blockTop + blockSize);
        for (int blockLeft = 0; blockLeft < width; blockLeft += blockSize) {
            const int blockRight = std::min(width, blockLeft + blockSize);

            bool masked = false;
            for (int y = blockTop; y < blockBottom && !masked; ++y) {
                const unsigned char* maskRow = mask.data() + static_cast<size_t>(y) * width;
                for (int x = blockLeft; x < blockRight; ++x) {
                    if (maskRow[x] != 0) {
                        masked = true;
                        break;
                    }
                }
            }
            if (!masked) {
                continue;
            }

            long sumR = 0, sumG = 0, sumB = 0;
            long count = 0;
            for (int y = blockTop; y < blockBottom; ++y) {
                const unsigned char* row = doc.pixels.data() + static_cast<size_t>(y) * stride;
                for (int x = blockLeft; x < blockRight; ++x) {
                    const unsigned char* px = row + static_cast<size_t>(x) * 4;
                    sumR += px[0];
                    sumG += px[1];
                    sumB += px[2];
                    ++count;
                }
            }
            if (count == 0) {
                continue;
            }
            const unsigned char avgR = static_cast<unsigned char>(sumR / count);
            const unsigned char avgG = static_cast<unsigned char>(sumG / count);
            const unsigned char avgB = static_cast<unsigned char>(sumB / count);

            for (int y = blockTop; y < blockBottom; ++y) {
                unsigned char* row = doc.pixels.data() + static_cast<size_t>(y) * stride;
                for (int x = blockLeft; x < blockRight; ++x) {
                    unsigned char* px = row + static_cast<size_t>(x) * 4;
                    px[0] = avgR;
                    px[1] = avgG;
                    px[2] = avgB;
                }
            }
        }
    }

    return true;
}

void PaintBrushLine(std::vector<unsigned char>& mask, int width, int height,
                     int x0, int y0, int x1, int y1, int radius) {
    if (radius <= 0 || width <= 0 || height <= 0) {
        return;
    }
    if (mask.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return;
    }

    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int steps = std::max(std::abs(dx), std::abs(dy));

    if (steps == 0) {
        PaintCircle(mask, width, height, x0, y0, radius);
        return;
    }

    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int x = x0 + static_cast<int>(std::round(dx * t));
        const int y = y0 + static_cast<int>(std::round(dy * t));
        PaintCircle(mask, width, height, x, y, radius);
    }
}

}  // namespace image_ops
