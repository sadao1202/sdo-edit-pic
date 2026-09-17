#pragma once

#include <optional>
#include <string>
#include <vector>

#include "image_document.hpp"

// GUI・Win32非依存のJPEGエンコード/デコード処理。
// WSL上のネイティブLinuxビルド（image_codec_tests）でもリンクできるよう、
// _wfopen等のWin32専用APIには依存しない。
namespace image_codec {

// RGBA→RGB。アルファがあれば白背景に合成する（image_io::SaveAsJpegと同一ロジック）。
std::vector<unsigned char> CompositeToRgb(const ImageDocument& doc);

}  // namespace image_codec
