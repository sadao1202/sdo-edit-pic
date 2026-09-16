#pragma once

#include <optional>
#include <vector>

#include "image_document.hpp"

namespace image_ops {

// [left, right) x [top, bottom) のクロップ範囲は自動的にdocの範囲にクランプする。
// クランプ後に幅・高さが0以下ならstd::nulloptを返す。
std::optional<ImageDocument> CropImage(const ImageDocument& doc,
                                        int left, int top, int right, int bottom);

// maskは doc.width*doc.height の1バイト/画素。非0の画素が「モザイク対象」。
// blockSize x blockSize の固定グリッド（画像原点(0,0)基準）で、対象画素を1つ以上
// 含むブロックをそのブロック内RGB平均色で塗りつぶす。アルファは変更しない。
// mask のサイズ不一致、blockSize <= 0、doc が空（width==0 || height==0）のときは
// false を返し doc を変更しない。
bool ApplyMosaic(ImageDocument& doc, const std::vector<unsigned char>& mask, int blockSize);

// maskに (x0,y0)-(x1,y1) の線分に沿った半径radiusの円を描き込む（値255）。
// 画像外ははみ出しをクリップする。radius <= 0 は何もしない。
void PaintBrushLine(std::vector<unsigned char>& mask, int width, int height,
                     int x0, int y0, int x1, int y1, int radius);

}  // namespace image_ops
