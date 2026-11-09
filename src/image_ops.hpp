#pragma once

#include <optional>
#include <vector>

#include "image_document.hpp"

namespace image_ops {

// [left, right) x [top, bottom) のクロップ範囲は自動的にdocの範囲にクランプする。
// クランプ後に幅・高さが0以下ならstd::nulloptを返す。
std::optional<ImageDocument> CropImage(const ImageDocument& doc,
                                        int left, int top, int right, int bottom);

// サイズ指定トリミング用の固定サイズ枠（画像px）。
struct FixedRect {
    int w = 0;
    int h = 0;
    int left = 0;
    int top = 0;
};

// w/hを[1, imgW]/[1, imgH]に丸め、枠が画像内に収まるようleft/topをクランプして返す。
// imgW <= 0 または imgH <= 0 のときは全て0の FixedRect を返す。
FixedRect ClampFixedRect(int imgW, int imgH, int w, int h, int left, int top);

// 比率 ratioW:ratioH で画像内に収まる最大の枠を画像中央に配置して返す
// （h = round(w*ratioH/ratioW)、最小1。極端な比率でどうしても収まらない場合はh<=imgHに丸める）。
// imgW/imgH/ratioW/ratioH のいずれかが0以下のときは全て0の FixedRect を返す。
FixedRect MaxAspectRect(int imgW, int imgH, int ratioW, int ratioH);

// 幅wを[1, 画像内で比率を保てる最大幅]に丸め、h = round(w*ratioH/ratioW)（最小1、<=imgH）とし、
// 枠が画像内に収まるようleft/topをクランプして返す。無効な引数のときは全て0の FixedRect を返す。
FixedRect ClampAspectRect(int imgW, int imgH, int ratioW, int ratioH, int w, int left, int top);

// 画像エリア(areaW x areaH)に画像(imgW x imgH)を縦横比を保って収める表示サイズ。
// 拡大倍率は maxScale（既定1.0=拡大しない）まで。いずれかが0以下なら {0, 0}。
struct FitResult {
    float w = 0.0f;
    float h = 0.0f;
};
FitResult FitSize(float areaW, float areaH, float imgW, float imgH, float maxScale = 1.0f);

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
