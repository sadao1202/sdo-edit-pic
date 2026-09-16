#pragma once

#include <optional>

#include "image_document.hpp"

namespace image_ops {

// [left, right) x [top, bottom) のクロップ範囲は自動的にdocの範囲にクランプする。
// クランプ後に幅・高さが0以下ならstd::nulloptを返す。
std::optional<ImageDocument> CropImage(const ImageDocument& doc,
                                        int left, int top, int right, int bottom);

}  // namespace image_ops
