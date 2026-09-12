#pragma once

#include <optional>
#include <string>

#include "image_document.hpp"

namespace image_io {

// stbのデコード成否で判定する（拡張子は見ない）。失敗時はoutErrorを設定しnulloptを返す。
std::optional<ImageDocument> LoadImage(const std::wstring& path, std::string& outError);

// RGBAのまま保存する。
bool SaveAsPng(const ImageDocument& doc, const std::wstring& path, std::string& outError);

// アルファがある場合は白背景に合成してからJPEGとして保存する。qualityは1-100にクランプする。
bool SaveAsJpeg(const ImageDocument& doc, const std::wstring& path, int quality, std::string& outError);

}  // namespace image_io
