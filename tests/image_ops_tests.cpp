// image_ops の自動テスト（GUI・Windows API非依存の純粋関数のみを対象）。
// フレームワークは使わずassertベースの簡易実行ファイル。CTestから起動される。
//
// 試験観点は plan/mosaic-feature.md の「自動テスト可能範囲」に対応する
// （コメント中の番号は同ドキュメントの正常系1-7・異常系8-12・境界値13-17）。

#include <cassert>
#include <cstdio>
#include <vector>

#include "image_document.hpp"
#include "image_ops.hpp"

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

void SetPixel(ImageDocument& doc, int x, int y, unsigned char r, unsigned char g, unsigned char b,
              unsigned char a) {
    const size_t idx = (static_cast<size_t>(y) * doc.width + x) * 4;
    doc.pixels[idx + 0] = r;
    doc.pixels[idx + 1] = g;
    doc.pixels[idx + 2] = b;
    doc.pixels[idx + 3] = a;
}

// 正常系1: 単色画像の全画素マスク → 出力も同じ単色。
void Test_ApplyMosaic_SolidColorUnchanged() {
    ImageDocument doc = MakeSolidImage(4, 4, 100, 150, 200, 255);
    std::vector<unsigned char> mask(16, 1);
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(ok);
    for (int i = 0; i < 16; ++i) {
        assert(doc.pixels[i * 4 + 0] == 100);
        assert(doc.pixels[i * 4 + 1] == 150);
        assert(doc.pixels[i * 4 + 2] == 200);
        assert(doc.pixels[i * 4 + 3] == 255);
    }
}

// 正常系2: 2色の市松模様、blockSize=2、全画素マスク → 各ブロックが2色の平均色になる。
void Test_ApplyMosaic_CheckerboardAverages() {
    ImageDocument doc;
    doc.width = 2;
    doc.height = 2;
    doc.channels = 4;
    doc.pixels.resize(2 * 2 * 4);
    SetPixel(doc, 0, 0, 0, 0, 0, 255);
    SetPixel(doc, 1, 0, 100, 100, 100, 255);
    SetPixel(doc, 0, 1, 100, 100, 100, 255);
    SetPixel(doc, 1, 1, 0, 0, 0, 255);
    std::vector<unsigned char> mask(4, 1);
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(ok);
    // (0+100+100+0)/4 = 50
    for (int i = 0; i < 4; ++i) {
        assert(doc.pixels[i * 4 + 0] == 50);
        assert(doc.pixels[i * 4 + 1] == 50);
        assert(doc.pixels[i * 4 + 2] == 50);
    }
}

// 正常系3: 部分マスク: マスクが立っているブロックのみ変化し、それ以外のブロックは
// 1バイトも変化しない。
void Test_ApplyMosaic_PartialMaskLeavesOtherBlocksUntouched() {
    ImageDocument doc;
    doc.width = 4;
    doc.height = 2;
    doc.channels = 4;
    doc.pixels.resize(4 * 2 * 4);
    // 左ブロック(0-1列)は市松、右ブロック(2-3列)も市松にしておく。
    SetPixel(doc, 0, 0, 0, 0, 0, 255);
    SetPixel(doc, 1, 0, 200, 200, 200, 255);
    SetPixel(doc, 0, 1, 200, 200, 200, 255);
    SetPixel(doc, 1, 1, 0, 0, 0, 255);
    SetPixel(doc, 2, 0, 10, 20, 30, 255);
    SetPixel(doc, 3, 0, 40, 50, 60, 255);
    SetPixel(doc, 2, 1, 70, 80, 90, 255);
    SetPixel(doc, 3, 1, 11, 22, 33, 255);
    const std::vector<unsigned char> before = doc.pixels;

    std::vector<unsigned char> mask(8, 0);
    mask[0] = 1;  // 左上ブロック内の1画素のみ
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(ok);

    // 左ブロック(0,0)-(1,1)は平均100に変化しているはず。
    assert(doc.pixels[0] == 100 && doc.pixels[1] == 100 && doc.pixels[2] == 100);
    // 右ブロック(2,3列)は1バイトも変化していないはず。
    for (int y = 0; y < 2; ++y) {
        for (int x = 2; x < 4; ++x) {
            const size_t idx = (static_cast<size_t>(y) * 4 + x) * 4;
            assert(doc.pixels[idx + 0] == before[idx + 0]);
            assert(doc.pixels[idx + 1] == before[idx + 1]);
            assert(doc.pixels[idx + 2] == before[idx + 2]);
            assert(doc.pixels[idx + 3] == before[idx + 3]);
        }
    }
}

// 正常系4: アルファ維持: 各画素のA成分が入出力で完全一致する（RGBのみ変化）。
void Test_ApplyMosaic_AlphaPreserved() {
    ImageDocument doc;
    doc.width = 2;
    doc.height = 2;
    doc.channels = 4;
    doc.pixels.resize(2 * 2 * 4);
    SetPixel(doc, 0, 0, 10, 20, 30, 0);
    SetPixel(doc, 1, 0, 40, 50, 60, 64);
    SetPixel(doc, 0, 1, 70, 80, 90, 128);
    SetPixel(doc, 1, 1, 100, 110, 120, 255);
    std::vector<unsigned char> mask(4, 1);
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(ok);
    assert(doc.pixels[3] == 0);
    assert(doc.pixels[7] == 64);
    assert(doc.pixels[11] == 128);
    assert(doc.pixels[15] == 255);
}

// 正常系5: マスク画素が1つだけ立っているとき、その画素を含むブロック全体が塗られる。
void Test_ApplyMosaic_SinglePixelMaskFillsWholeBlock() {
    ImageDocument doc;
    doc.width = 4;
    doc.height = 4;
    doc.channels = 4;
    doc.pixels.resize(4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            SetPixel(doc, x, y, static_cast<unsigned char>(x * 10), static_cast<unsigned char>(y * 10), 0, 255);
        }
    }
    std::vector<unsigned char> mask(16, 0);
    // ブロック(2,2)-(3,3)内の1画素だけを立てる（blockSize=2）。
    mask[3 * 4 + 3] = 1;
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(ok);
    const unsigned char expectedR = (20 + 30 + 20 + 30) / 4;  // x=2,3
    const unsigned char expectedG = (20 + 20 + 30 + 30) / 4;  // y=2,3
    for (int y = 2; y < 4; ++y) {
        for (int x = 2; x < 4; ++x) {
            const size_t idx = (static_cast<size_t>(y) * 4 + x) * 4;
            assert(doc.pixels[idx + 0] == expectedR);
            assert(doc.pixels[idx + 1] == expectedG);
        }
    }
}

// 正常系6: PaintBrushLine: 線分の両端と中間点が半径内で255になる。半径外は0のまま。
void Test_PaintBrushLine_EndpointsAndMidpointPainted() {
    const int width = 20;
    const int height = 20;
    std::vector<unsigned char> mask(width * height, 0);
    image_ops::PaintBrushLine(mask, width, height, 2, 2, 10, 2, 2);
    assert(mask[2 * width + 2] == 255);
    assert(mask[2 * width + 10] == 255);
    assert(mask[2 * width + 6] == 255);  // 中間点
    // 半径外（y方向に十分離れた点）は塗られない。
    assert(mask[10 * width + 6] == 0);
}

// 正常系7: PaintBrushLineを同じマスクに複数回呼ぶと塗りが累積する（既存の255は消えない）。
void Test_PaintBrushLine_Accumulates() {
    const int width = 20;
    const int height = 20;
    std::vector<unsigned char> mask(width * height, 0);
    image_ops::PaintBrushLine(mask, width, height, 2, 2, 2, 2, 1);
    assert(mask[2 * width + 2] == 255);
    image_ops::PaintBrushLine(mask, width, height, 15, 15, 15, 15, 1);
    assert(mask[2 * width + 2] == 255);  // 前の塗りが消えていない
    assert(mask[15 * width + 15] == 255);
}

// 異常系8: マスクサイズ不一致 → falseを返しdocは不変。
void Test_ApplyMosaic_MaskSizeMismatchReturnsFalse() {
    ImageDocument doc = MakeSolidImage(4, 4, 1, 2, 3, 255);
    const std::vector<unsigned char> before = doc.pixels;
    std::vector<unsigned char> mask(4, 1);  // 本来は16必要
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(!ok);
    assert(doc.pixels == before);
}

// 異常系9: blockSize <= 0 → false、doc不変。
void Test_ApplyMosaic_NonPositiveBlockSizeReturnsFalse() {
    ImageDocument doc = MakeSolidImage(4, 4, 1, 2, 3, 255);
    const std::vector<unsigned char> before = doc.pixels;
    std::vector<unsigned char> mask(16, 1);
    assert(!image_ops::ApplyMosaic(doc, mask, 0));
    assert(doc.pixels == before);
    assert(!image_ops::ApplyMosaic(doc, mask, -1));
    assert(doc.pixels == before);
}

// 異常系10: 空画像（width==0 || height==0）→ false、クラッシュしない。
void Test_ApplyMosaic_EmptyImageReturnsFalse() {
    ImageDocument doc;
    doc.width = 0;
    doc.height = 0;
    std::vector<unsigned char> mask;
    assert(!image_ops::ApplyMosaic(doc, mask, 2));

    ImageDocument doc2;
    doc2.width = 4;
    doc2.height = 0;
    std::vector<unsigned char> mask2;
    assert(!image_ops::ApplyMosaic(doc2, mask2, 2));
}

// 異常系11: PaintBrushLine: 画像外の座標を渡してもバッファ外書き込みをしない
// （結果はクリップされる）。
void Test_PaintBrushLine_OutOfBoundsClipsWithoutCrash() {
    const int width = 10;
    const int height = 10;
    std::vector<unsigned char> mask(width * height, 0);
    image_ops::PaintBrushLine(mask, width, height, -50, -50, 50, 50, 5);
    // 中心付近が塗られている（クリップされつつ画像内は塗られる）。
    bool anyPainted = false;
    for (unsigned char v : mask) {
        if (v == 255) {
            anyPainted = true;
            break;
        }
    }
    assert(anyPainted);
    assert(mask.size() == static_cast<size_t>(width) * height);
}

// 異常系12: radius <= 0 で何も変化しない。
void Test_PaintBrushLine_NonPositiveRadiusNoOp() {
    const int width = 10;
    const int height = 10;
    std::vector<unsigned char> mask(width * height, 0);
    image_ops::PaintBrushLine(mask, width, height, 2, 2, 8, 8, 0);
    for (unsigned char v : mask) {
        assert(v == 0);
    }
    image_ops::PaintBrushLine(mask, width, height, 2, 2, 8, 8, -3);
    for (unsigned char v : mask) {
        assert(v == 0);
    }
}

// 境界値13: blockSizeが画像サイズ以上（8x8画像にblockSize=64）→ 画像全体が1ブロック
// として平均色になる。
void Test_ApplyMosaic_BlockSizeLargerThanImage() {
    ImageDocument doc;
    doc.width = 8;
    doc.height = 8;
    doc.channels = 4;
    doc.pixels.resize(8 * 8 * 4);
    long sumR = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const unsigned char r = static_cast<unsigned char>((x + y) * 4);
            SetPixel(doc, x, y, r, r, r, 255);
            sumR += r;
        }
    }
    const unsigned char expected = static_cast<unsigned char>(sumR / 64);
    std::vector<unsigned char> mask(64, 1);
    assert(image_ops::ApplyMosaic(doc, mask, 64));
    for (int i = 0; i < 64; ++i) {
        assert(doc.pixels[i * 4 + 0] == expected);
    }
}

// 境界値14: 画像サイズがブロックサイズで割り切れない場合（10x10、blockSize=4）、
// 右端・下端の端数ブロック（2px幅）も正しく処理され、はみ出し書き込みがない。
void Test_ApplyMosaic_UnevenBlocksNoOverflow() {
    ImageDocument doc;
    doc.width = 10;
    doc.height = 10;
    doc.channels = 4;
    doc.pixels.assign(10 * 10 * 4, 0);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            SetPixel(doc, x, y, 50, 50, 50, 255);
        }
    }
    std::vector<unsigned char> mask(100, 1);
    assert(image_ops::ApplyMosaic(doc, mask, 4));
    assert(doc.pixels.size() == 10 * 10 * 4);
    // 端数ブロック（x=8,9 / y=8,9）を含め、全画素が単色画像のため平均も50のまま。
    for (int i = 0; i < 100; ++i) {
        assert(doc.pixels[i * 4 + 0] == 50);
    }
}

// 境界値15: blockSize=2（下限）、blockSize=64（上限）、ブラシ直径4（下限）・200（上限）で
// 正常動作。
void Test_BoundaryParameterValues() {
    ImageDocument doc = MakeSolidImage(64, 64, 5, 6, 7, 255);
    std::vector<unsigned char> mask(64 * 64, 1);
    assert(image_ops::ApplyMosaic(doc, mask, 2));
    assert(image_ops::ApplyMosaic(doc, mask, 64));

    std::vector<unsigned char> brushMask(64 * 64, 0);
    image_ops::PaintBrushLine(brushMask, 64, 64, 0, 0, 63, 63, 4 / 2);
    image_ops::PaintBrushLine(brushMask, 64, 64, 0, 0, 63, 63, 200 / 2);
}

// 境界値16: 1x1画像に対するApplyMosaicが成功し、画素が変化しない（平均＝自身）。
void Test_ApplyMosaic_OnePixelImage() {
    ImageDocument doc = MakeSolidImage(1, 1, 12, 34, 56, 78);
    std::vector<unsigned char> mask(1, 1);
    assert(image_ops::ApplyMosaic(doc, mask, 8));
    assert(doc.pixels[0] == 12);
    assert(doc.pixels[1] == 34);
    assert(doc.pixels[2] == 56);
    assert(doc.pixels[3] == 78);
}

// 境界値17: マスクが全て0 → trueを返すが画像は1バイトも変化しない。
void Test_ApplyMosaic_AllZeroMaskNoChange() {
    ImageDocument doc = MakeSolidImage(4, 4, 9, 8, 7, 200);
    // 単色だと変化が見えないため、画素ごとに異なる値にしておく。
    for (int i = 0; i < 16; ++i) {
        doc.pixels[i * 4 + 0] = static_cast<unsigned char>(i);
    }
    const std::vector<unsigned char> before = doc.pixels;
    std::vector<unsigned char> mask(16, 0);
    const bool ok = image_ops::ApplyMosaic(doc, mask, 2);
    assert(ok);
    assert(doc.pixels == before);
}

// 以下は plan/mosaic-ui-improvements.md の「自動テスト可能範囲」
// （正常系1-4、境界値5-6、異常系7）に対応する。
// 履歴管理自体はApp（GUI結合）にあり直接テストできないため、Undoの正しさが
// 依存する「マスク復元だけでプレビューが再現できる」決定性を確認する。

// 正常系1: 同一のdocumentコピーと同一マスク・同一blockSizeでApplyMosaicを2回実行すると、
// 完全に同一のピクセル列になる。
void Test_ApplyMosaic_Deterministic() {
    ImageDocument doc1;
    doc1.width = 6;
    doc1.height = 6;
    doc1.channels = 4;
    doc1.pixels.resize(6 * 6 * 4);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            SetPixel(doc1, x, y, static_cast<unsigned char>(x * 20), static_cast<unsigned char>(y * 20), 30, 255);
        }
    }
    ImageDocument doc2 = doc1;
    std::vector<unsigned char> mask(36, 0);
    for (int i = 0; i < 36; i += 2) {
        mask[i] = 1;
    }
    assert(image_ops::ApplyMosaic(doc1, mask, 3));
    assert(image_ops::ApplyMosaic(doc2, mask, 3));
    assert(doc1.pixels == doc2.pixels);
}

// 正常系2: マスクAで塗った結果と、「マスクAのコピーを取る → さらにBを塗る → コピーへ戻す」
// で得た結果が一致する（スナップショット復元の等価性）。
void Test_ApplyMosaic_SnapshotRestoreEquivalence() {
    ImageDocument baseDoc;
    baseDoc.width = 8;
    baseDoc.height = 8;
    baseDoc.channels = 4;
    baseDoc.pixels.resize(8 * 8 * 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            SetPixel(baseDoc, x, y, static_cast<unsigned char>(x * 10), static_cast<unsigned char>(y * 10), 40, 255);
        }
    }

    std::vector<unsigned char> maskA(64, 0);
    image_ops::PaintBrushLine(maskA, 8, 8, 1, 1, 3, 1, 1);

    // 期待値: マスクAだけを適用した結果。
    ImageDocument expected = baseDoc;
    assert(image_ops::ApplyMosaic(expected, maskA, 2));

    // 実際: マスクAのスナップショットを取ってからBを塗り、スナップショットへ復元。
    std::vector<unsigned char> maskSnapshot = maskA;
    std::vector<unsigned char> maskWithB = maskA;
    image_ops::PaintBrushLine(maskWithB, 8, 8, 5, 5, 7, 5, 1);
    assert(maskWithB != maskSnapshot);  // Bが確かに追加で塗られている
    std::vector<unsigned char> restored = maskSnapshot;  // Undo相当の復元

    ImageDocument actual = baseDoc;
    assert(image_ops::ApplyMosaic(actual, restored, 2));

    assert(actual.pixels == expected.pixels);
}

// 正常系3: 全0マスクでApplyMosaicを実行すると画像が一切変化しない（全Undo後の状態＝
// 元画像と一致）。
void Test_ApplyMosaic_AllZeroMaskEqualsOriginal() {
    ImageDocument doc = MakeSolidImage(4, 4, 1, 2, 3, 255);
    for (int i = 0; i < 16; ++i) {
        doc.pixels[i * 4 + 1] = static_cast<unsigned char>(i * 3);
    }
    const std::vector<unsigned char> before = doc.pixels;
    std::vector<unsigned char> mask(16, 0);
    assert(image_ops::ApplyMosaic(doc, mask, 4));
    assert(doc.pixels == before);
}

// 正常系4: PaintBrushLineを同じ引数で2回呼んでもマスクが変わらない（冪等性）。
void Test_PaintBrushLine_IdempotentForSameArgs() {
    const int width = 12;
    const int height = 12;
    std::vector<unsigned char> mask(width * height, 0);
    image_ops::PaintBrushLine(mask, width, height, 1, 1, 9, 5, 2);
    const std::vector<unsigned char> after1 = mask;
    image_ops::PaintBrushLine(mask, width, height, 1, 1, 9, 5, 2);
    assert(mask == after1);
}

// 境界値5: 1x1画像のマスクでPaintBrushLine / ApplyMosaicが範囲外アクセスしない。
void Test_OnePixelImage_PaintAndApplyMosaicNoCrash() {
    std::vector<unsigned char> mask(1, 0);
    image_ops::PaintBrushLine(mask, 1, 1, 0, 0, 0, 0, 5);
    assert(mask[0] == 255);

    ImageDocument doc = MakeSolidImage(1, 1, 9, 8, 7, 255);
    assert(image_ops::ApplyMosaic(doc, mask, 8));
    assert(doc.pixels[0] == 9 && doc.pixels[1] == 8 && doc.pixels[2] == 7 && doc.pixels[3] == 255);
}

// 境界値6: blockSizeの下限2・上限64で決定性テスト（観点1）が成立する。
void Test_ApplyMosaic_DeterministicAtBoundaryBlockSizes() {
    for (const int blockSize : {2, 64}) {
        ImageDocument doc1;
        doc1.width = 8;
        doc1.height = 8;
        doc1.channels = 4;
        doc1.pixels.resize(8 * 8 * 4);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                SetPixel(doc1, x, y, static_cast<unsigned char>(x * 30), static_cast<unsigned char>(y * 30), 5, 255);
            }
        }
        ImageDocument doc2 = doc1;
        std::vector<unsigned char> mask(64, 1);
        assert(image_ops::ApplyMosaic(doc1, mask, blockSize));
        assert(image_ops::ApplyMosaic(doc2, mask, blockSize));
        assert(doc1.pixels == doc2.pixels);
    }
}

// 異常系7: マスク長がwidth*heightと一致しない場合にApplyMosaicが安全に失敗する
// （既存挙動の回帰確認。Test_ApplyMosaic_MaskSizeMismatchReturnsFalseと同趣旨だが
// 本設計書の試験観点番号に合わせて明示的に記載する）。
void Test_ApplyMosaic_MismatchedMaskLengthFailsSafely() {
    ImageDocument doc = MakeSolidImage(5, 5, 4, 5, 6, 255);
    const std::vector<unsigned char> before = doc.pixels;
    std::vector<unsigned char> tooShortMask(10, 1);
    assert(!image_ops::ApplyMosaic(doc, tooShortMask, 2));
    assert(doc.pixels == before);

    std::vector<unsigned char> tooLongMask(40, 1);
    assert(!image_ops::ApplyMosaic(doc, tooLongMask, 2));
    assert(doc.pixels == before);
}

// CropImageの回帰確認テスト。
void Test_CropImage_BasicCrop() {
    ImageDocument doc;
    doc.width = 4;
    doc.height = 4;
    doc.channels = 4;
    doc.pixels.resize(4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            SetPixel(doc, x, y, static_cast<unsigned char>(x), static_cast<unsigned char>(y), 0, 255);
        }
    }
    auto cropped = image_ops::CropImage(doc, 1, 1, 3, 3);
    assert(cropped.has_value());
    assert(cropped->width == 2);
    assert(cropped->height == 2);
    assert(cropped->pixels[0] == 1 && cropped->pixels[1] == 1);
}

void Test_CropImage_DegenerateRangeReturnsNullopt() {
    ImageDocument doc;
    doc.width = 4;
    doc.height = 4;
    doc.channels = 4;
    doc.pixels.resize(4 * 4 * 4);
    assert(!image_ops::CropImage(doc, 2, 2, 2, 2).has_value());
    assert(!image_ops::CropImage(doc, 3, 0, 1, 4).has_value());
}

void Test_CropImage_ClampsOutOfRange() {
    ImageDocument doc;
    doc.width = 4;
    doc.height = 4;
    doc.channels = 4;
    doc.pixels.resize(4 * 4 * 4, 7);
    auto cropped = image_ops::CropImage(doc, -10, -10, 100, 100);
    assert(cropped.has_value());
    assert(cropped->width == 4);
    assert(cropped->height == 4);
}

}  // namespace

int main() {
    Test_ApplyMosaic_SolidColorUnchanged();
    Test_ApplyMosaic_CheckerboardAverages();
    Test_ApplyMosaic_PartialMaskLeavesOtherBlocksUntouched();
    Test_ApplyMosaic_AlphaPreserved();
    Test_ApplyMosaic_SinglePixelMaskFillsWholeBlock();
    Test_PaintBrushLine_EndpointsAndMidpointPainted();
    Test_PaintBrushLine_Accumulates();

    Test_ApplyMosaic_MaskSizeMismatchReturnsFalse();
    Test_ApplyMosaic_NonPositiveBlockSizeReturnsFalse();
    Test_ApplyMosaic_EmptyImageReturnsFalse();
    Test_PaintBrushLine_OutOfBoundsClipsWithoutCrash();
    Test_PaintBrushLine_NonPositiveRadiusNoOp();

    Test_ApplyMosaic_BlockSizeLargerThanImage();
    Test_ApplyMosaic_UnevenBlocksNoOverflow();
    Test_BoundaryParameterValues();
    Test_ApplyMosaic_OnePixelImage();
    Test_ApplyMosaic_AllZeroMaskNoChange();

    Test_ApplyMosaic_Deterministic();
    Test_ApplyMosaic_SnapshotRestoreEquivalence();
    Test_ApplyMosaic_AllZeroMaskEqualsOriginal();
    Test_PaintBrushLine_IdempotentForSameArgs();
    Test_OnePixelImage_PaintAndApplyMosaicNoCrash();
    Test_ApplyMosaic_DeterministicAtBoundaryBlockSizes();
    Test_ApplyMosaic_MismatchedMaskLengthFailsSafely();

    Test_CropImage_BasicCrop();
    Test_CropImage_DegenerateRangeReturnsNullopt();
    Test_CropImage_ClampsOutOfRange();

    std::printf("All image_ops tests passed.\n");
    return 0;
}
