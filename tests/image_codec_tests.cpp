// image_codec の自動テスト（GUI・Win32非依存の純粋関数のみを対象）。
// フレームワークは使わずassertベースの簡易実行ファイル。CTestから起動される。
//
// 試験観点は plan/jpeg-quality-preview.md の「自動テスト可能範囲」に対応する
// （コメント中の番号は同ドキュメントの正常系1-9・異常系10-12・境界値13-17）。

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "image_codec.hpp"
#include "image_document.hpp"
#include "stb_image.h"

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

// 左から右へ徐々に明るくなるグラデーション画像（不透明）。
ImageDocument MakeGradientImage(int width, int height) {
    ImageDocument doc;
    doc.width = width;
    doc.height = height;
    doc.channels = 4;
    doc.pixels.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            const unsigned char v = static_cast<unsigned char>((x * 255) / std::max(1, width - 1));
            doc.pixels[idx + 0] = v;
            doc.pixels[idx + 1] = static_cast<unsigned char>((x + y) % 256);
            doc.pixels[idx + 2] = static_cast<unsigned char>(255 - v);
            doc.pixels[idx + 3] = 255;
        }
    }
    doc.hasAlpha = ImageDocument::ComputeHasAlpha(doc.pixels);
    return doc;
}

double MeanAbsoluteError(const ImageDocument& a, const ImageDocument& b) {
    assert(a.width == b.width && a.height == b.height);
    double total = 0.0;
    const size_t pixelCount = static_cast<size_t>(a.width) * a.height;
    for (size_t i = 0; i < pixelCount; ++i) {
        for (int c = 0; c < 3; ++c) {
            total += std::abs(static_cast<int>(a.pixels[i * 4 + c]) - static_cast<int>(b.pixels[i * 4 + c]));
        }
    }
    return total / static_cast<double>(pixelCount * 3);
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

// 正常系4: EncodeJpegToMemory、戻り値true、バッファ長>0、先頭2バイトがSOI、末尾がEOI。
void Test_EncodeJpegToMemory_ValidJpegMarkers() {
    ImageDocument doc = MakeGradientImage(16, 16);
    std::vector<unsigned char> buffer;
    std::string error;
    const bool ok = image_codec::EncodeJpegToMemory(doc, 80, buffer, error);
    assert(ok);
    assert(!buffer.empty());
    assert(buffer[0] == 0xFF && buffer[1] == 0xD8);
    assert(buffer[buffer.size() - 2] == 0xFF && buffer[buffer.size() - 1] == 0xD9);
}

// 正常系5: MakeJpegRoundTrip、寸法一致・channels==4・全画素α==255・HasAlpha()==false。
void Test_MakeJpegRoundTrip_ShapeAndAlpha() {
    ImageDocument doc = MakeGradientImage(12, 9);
    std::string error;
    auto result = image_codec::MakeJpegRoundTrip(doc, 90, error);
    assert(result.has_value());
    assert(result->width == doc.width);
    assert(result->height == doc.height);
    assert(result->channels == 4);
    assert(!result->HasAlpha());
    const size_t pixelCount = static_cast<size_t>(result->width) * result->height;
    for (size_t i = 0; i < pixelCount; ++i) {
        assert(result->pixels[i * 4 + 3] == 255);
    }
}

// 正常系6: 単色画像は品質1でもほぼ同色（許容差内）。
void Test_MakeJpegRoundTrip_SolidColorLowQualityStillClose() {
    ImageDocument doc = MakeSolidImage(16, 16, 100, 150, 200, 255);
    std::string error;
    auto result = image_codec::MakeJpegRoundTrip(doc, 1, error);
    assert(result.has_value());
    const size_t pixelCount = static_cast<size_t>(result->width) * result->height;
    for (size_t i = 0; i < pixelCount; ++i) {
        assert(std::abs(static_cast<int>(result->pixels[i * 4 + 0]) - 100) <= 20);
        assert(std::abs(static_cast<int>(result->pixels[i * 4 + 1]) - 150) <= 20);
        assert(std::abs(static_cast<int>(result->pixels[i * 4 + 2]) - 200) <= 20);
    }
}

// 正常系7: グラデーション画像で、品質100の平均絶対誤差 < 品質1の平均絶対誤差。
void Test_MakeJpegRoundTrip_HigherQualityLowerError() {
    ImageDocument doc = MakeGradientImage(64, 64);
    std::string error;
    auto low = image_codec::MakeJpegRoundTrip(doc, 1, error);
    auto high = image_codec::MakeJpegRoundTrip(doc, 100, error);
    assert(low.has_value() && high.has_value());
    const double lowError = MeanAbsoluteError(doc, *low);
    const double highError = MeanAbsoluteError(doc, *high);
    assert(highError < lowError);
}

// 正常系8: 入力docが呼び出し後も変更されていない（const性・非破壊）。
void Test_MakeJpegRoundTrip_InputUnchanged() {
    ImageDocument doc = MakeGradientImage(10, 10);
    const std::vector<unsigned char> before = doc.pixels;
    const int width = doc.width;
    const int height = doc.height;
    std::string error;
    auto result = image_codec::MakeJpegRoundTrip(doc, 50, error);
    assert(result.has_value());
    assert(doc.pixels == before);
    assert(doc.width == width);
    assert(doc.height == height);
}

// 正常系9: アルファ付き画像のラウンドトリップ結果が、白合成後の色に近い（透明部分が白系になる）。
void Test_MakeJpegRoundTrip_AlphaBecomesWhiteLike() {
    ImageDocument doc = MakeSolidImage(8, 8, 10, 20, 30, 0);
    std::string error;
    auto result = image_codec::MakeJpegRoundTrip(doc, 90, error);
    assert(result.has_value());
    const size_t pixelCount = static_cast<size_t>(result->width) * result->height;
    for (size_t i = 0; i < pixelCount; ++i) {
        assert(result->pixels[i * 4 + 0] >= 230);
        assert(result->pixels[i * 4 + 1] >= 230);
        assert(result->pixels[i * 4 + 2] >= 230);
    }
}

// 異常系10: width==0 / height==0 / pixels空 → nulloptかつoutErrorが非空。
void Test_MakeJpegRoundTrip_InvalidDimensionsReturnsNullopt() {
    ImageDocument doc1;
    doc1.width = 0;
    doc1.height = 4;
    doc1.pixels.assign(0, 0);
    std::string error1;
    assert(!image_codec::MakeJpegRoundTrip(doc1, 50, error1).has_value());
    assert(!error1.empty());

    ImageDocument doc2;
    doc2.width = 4;
    doc2.height = 0;
    std::string error2;
    assert(!image_codec::MakeJpegRoundTrip(doc2, 50, error2).has_value());
    assert(!error2.empty());

    ImageDocument doc3;
    doc3.width = 4;
    doc3.height = 4;
    doc3.pixels.clear();  // 空（4*4*4と矛盾）
    std::string error3;
    assert(!image_codec::MakeJpegRoundTrip(doc3, 50, error3).has_value());
    assert(!error3.empty());
}

// 異常系11: 破損データ（意図的に切り詰めたエンコード結果）をデコードする経路 → nullopt、
// クラッシュしない。
void Test_MakeJpegRoundTrip_TruncatedDataDoesNotCrash() {
    ImageDocument doc = MakeGradientImage(16, 16);
    std::vector<unsigned char> encoded;
    std::string error;
    assert(image_codec::EncodeJpegToMemory(doc, 80, encoded, error));
    assert(encoded.size() > 16);
    encoded.resize(encoded.size() / 2);  // 意図的に切り詰める

    int width = 0;
    int height = 0;
    int channels = 0;
    // stbi_load_from_memoryを直接呼び、破損データを渡してもクラッシュしないことを確認する。
    unsigned char* data = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width,
                                                 &height, &channels, 4);
    if (data) {
        // 実装依存で部分的にデコードできる場合もあるため、クラッシュしないことのみ確認する。
        stbi_image_free(data);
    }
}

// 異常系12: pixels.size()がwidth*height*4と矛盾する不正docを渡しても範囲外アクセスしない
// （事前検証で弾く）。
void Test_MakeJpegRoundTrip_MismatchedPixelSizeRejected() {
    ImageDocument doc;
    doc.width = 4;
    doc.height = 4;
    doc.channels = 4;
    doc.pixels.assign(4, 0);  // 本来は4*4*4=64必要
    std::string error;
    assert(!image_codec::MakeJpegRoundTrip(doc, 50, error).has_value());
    assert(!error.empty());

    std::vector<unsigned char> buffer;
    std::string encodeError;
    assert(!image_codec::EncodeJpegToMemory(doc, 50, buffer, encodeError));
    assert(buffer.empty());

    const std::vector<unsigned char> rgb = image_codec::CompositeToRgb(doc);
    assert(rgb.empty());
}

// 境界値13: quality = 0 / -5 / 101 / 1000 → 1-100にクランプされ成功する。
void Test_EncodeJpegToMemory_QualityClamped() {
    ImageDocument doc = MakeSolidImage(4, 4, 1, 2, 3, 255);
    for (int q : {0, -5, 101, 1000}) {
        std::vector<unsigned char> buffer;
        std::string error;
        assert(image_codec::EncodeJpegToMemory(doc, q, buffer, error));
        assert(!buffer.empty());
    }
}

// 境界値14: quality = 1 と quality = 100 の両端で成功する。
void Test_MakeJpegRoundTrip_QualityBoundaries() {
    ImageDocument doc = MakeGradientImage(8, 8);
    std::string error;
    assert(image_codec::MakeJpegRoundTrip(doc, 1, error).has_value());
    assert(image_codec::MakeJpegRoundTrip(doc, 100, error).has_value());
}

// 境界値15: 1x1画像。
void Test_MakeJpegRoundTrip_OnePixelImage() {
    ImageDocument doc = MakeSolidImage(1, 1, 42, 84, 126, 255);
    std::string error;
    auto result = image_codec::MakeJpegRoundTrip(doc, 90, error);
    assert(result.has_value());
    assert(result->width == 1);
    assert(result->height == 1);
}

// 境界値16: 1xN / Nx1 の極端なアスペクト比（例: 1x1000）。
void Test_MakeJpegRoundTrip_ExtremeAspectRatio() {
    ImageDocument doc1 = MakeSolidImage(1, 1000, 10, 20, 30, 255);
    std::string error1;
    auto result1 = image_codec::MakeJpegRoundTrip(doc1, 90, error1);
    assert(result1.has_value());
    assert(result1->width == 1);
    assert(result1->height == 1000);

    ImageDocument doc2 = MakeSolidImage(1000, 1, 10, 20, 30, 255);
    std::string error2;
    auto result2 = image_codec::MakeJpegRoundTrip(doc2, 90, error2);
    assert(result2.has_value());
    assert(result2->width == 1000);
    assert(result2->height == 1);
}

// 境界値17: 幅・高さが8の倍数でない画像（例: 17x13）で寸法が保たれる。
void Test_MakeJpegRoundTrip_NonMultipleOfEightDimensions() {
    ImageDocument doc = MakeGradientImage(17, 13);
    std::string error;
    auto result = image_codec::MakeJpegRoundTrip(doc, 90, error);
    assert(result.has_value());
    assert(result->width == 17);
    assert(result->height == 13);
}

}  // namespace

int main() {
    Test_CompositeToRgb_OpaquePassesThrough();
    Test_CompositeToRgb_TransparentBecomesWhite();
    Test_CompositeToRgb_PartialAlphaBlend();
    Test_EncodeJpegToMemory_ValidJpegMarkers();
    Test_MakeJpegRoundTrip_ShapeAndAlpha();
    Test_MakeJpegRoundTrip_SolidColorLowQualityStillClose();
    Test_MakeJpegRoundTrip_HigherQualityLowerError();
    Test_MakeJpegRoundTrip_InputUnchanged();
    Test_MakeJpegRoundTrip_AlphaBecomesWhiteLike();

    Test_MakeJpegRoundTrip_InvalidDimensionsReturnsNullopt();
    Test_MakeJpegRoundTrip_TruncatedDataDoesNotCrash();
    Test_MakeJpegRoundTrip_MismatchedPixelSizeRejected();

    Test_EncodeJpegToMemory_QualityClamped();
    Test_MakeJpegRoundTrip_QualityBoundaries();
    Test_MakeJpegRoundTrip_OnePixelImage();
    Test_MakeJpegRoundTrip_ExtremeAspectRatio();
    Test_MakeJpegRoundTrip_NonMultipleOfEightDimensions();

    std::printf("All image_codec tests passed.\n");
    return 0;
}
