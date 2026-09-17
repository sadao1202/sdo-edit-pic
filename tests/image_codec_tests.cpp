// image_codec の自動テスト（GUI・Win32非依存の純粋関数のみを対象）。
// フレームワークは使わずassertベースの簡易実行ファイル。CTestから起動される。
//
// 試験観点は plan/remove-jpeg-quality-feature.md の「自動テスト可能範囲」に対応する
// （コメント中の番号は同ドキュメントの正常系1に対応）。

#include <cassert>
#include <cstdio>
#include <vector>

#include "image_codec.hpp"
#include "image_document.hpp"

namespace {

ImageDocument MakeSolidImage(int width, int height, unsigned char r, unsigned char g, unsigned char b,
                              unsigned char a) {
    ImageDocument doc;
    doc.width = width;
    doc.height = height;
    doc.channels = 4;
    doc.pixels.resize(static_cast<size_t>(width) * height * 4);
    for (int i = 0; i < width * height; ++i) {
        doc.pixels[i * 4 + 0] = r;
        doc.pixels[i * 4 + 1] = g;
        doc.pixels[i * 4 + 2] = b;
        doc.pixels[i * 4 + 3] = a;
    }
    doc.hasAlpha = ImageDocument::ComputeHasAlpha(doc.pixels);
    return doc;
}

// 正常系1: CompositeToRgb、不透明画像（α=255）はRGBがそのまま透過する。
void Test_CompositeToRgb_OpaquePassesThrough() {
    ImageDocument doc = MakeSolidImage(3, 2, 10, 20, 30, 255);
    const std::vector<unsigned char> rgb = image_codec::CompositeToRgb(doc);
    assert(rgb.size() == 3 * 2 * 3);
    for (size_t i = 0; i < 6; ++i) {
        assert(rgb[i * 3 + 0] == 10);
        assert(rgb[i * 3 + 1] == 20);
        assert(rgb[i * 3 + 2] == 30);
    }
}

// 正常系2: CompositeToRgb、α=0 の画素は (255,255,255) になる。
void Test_CompositeToRgb_TransparentBecomesWhite() {
    ImageDocument doc = MakeSolidImage(2, 2, 10, 20, 30, 0);
    const std::vector<unsigned char> rgb = image_codec::CompositeToRgb(doc);
    for (size_t i = 0; i < 4; ++i) {
        assert(rgb[i * 3 + 0] == 255);
        assert(rgb[i * 3 + 1] == 255);
        assert(rgb[i * 3 + 2] == 255);
    }
}

// 正常系3: CompositeToRgb、α=128 の画素が期待ブレンド値になる（SaveAsJpegと同じ計算式）。
void Test_CompositeToRgb_PartialAlphaBlend() {
    ImageDocument doc = MakeSolidImage(1, 1, 10, 20, 30, 128);
    const std::vector<unsigned char> rgb = image_codec::CompositeToRgb(doc);
    const float alpha = 128 / 255.0f;
    const unsigned char expectedR = static_cast<unsigned char>(10 * alpha + 255.0f * (1.0f - alpha));
    const unsigned char expectedG = static_cast<unsigned char>(20 * alpha + 255.0f * (1.0f - alpha));
    const unsigned char expectedB = static_cast<unsigned char>(30 * alpha + 255.0f * (1.0f - alpha));
    assert(rgb[0] == expectedR);
    assert(rgb[1] == expectedG);
    assert(rgb[2] == expectedB);
}

}  // namespace

int main() {
    Test_CompositeToRgb_OpaquePassesThrough();
    Test_CompositeToRgb_TransparentBecomesWhite();
    Test_CompositeToRgb_PartialAlphaBlend();

    std::printf("All image_codec tests passed.\n");
    return 0;
}
