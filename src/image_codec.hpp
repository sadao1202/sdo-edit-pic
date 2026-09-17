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

// docをJPEG（quality:1-100にクランプ）としてメモリ上にエンコードする。失敗時はfalse。
bool EncodeJpegToMemory(const ImageDocument& doc, int quality, std::vector<unsigned char>& outBuffer,
                         std::string& outError);

// 上記エンコード結果を即デコードし、RGBA（α=255）のImageDocumentとして返す。失敗時はnullopt。
std::optional<ImageDocument> MakeJpegRoundTrip(const ImageDocument& doc, int quality, std::string& outError);

}  // namespace image_codec
