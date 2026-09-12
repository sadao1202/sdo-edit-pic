#pragma once

#include <string>
#include <vector>

// デコード済み画像データ。常にRGBA8(4ch)に正規化して保持する。
struct ImageDocument {
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<unsigned char> pixels;
    std::wstring sourcePath;
    // デコード時に一度だけ計算されるキャッシュ値。
    bool hasAlpha = false;

    bool HasAlpha() const { return hasAlpha; }

    // pixels(RGBA8)を走査してアルファ有無を判定する。デコード時に一度だけ呼ぶこと。
    static bool ComputeHasAlpha(const std::vector<unsigned char>& pixels);
};
