#pragma once

#include <string>

// パス文字列の純粋関数群（GUI・Win32非依存）。区切りは'/'と'\'の両方を扱う。
namespace path_utils {

// ディレクトリ・拡張子を除いたファイル名部分を返す。
std::wstring GetBaseNameWithoutExtension(const std::wstring& path);

// ファイル名部分より前のフォルダを、末尾の区切りを除いて返す。
// 区切りが無い場合は空文字列。ルート（"C:\"・"/"）のみ区切りを残す。
std::wstring GetDirectoryName(const std::wstring& path);

// 保存ダイアログの既定ファイル名（拡張子なし）として、元パスのベース名 + "_cropped" を返す。
// ベース名が空の場合は "cropped"。
std::wstring MakeDefaultSaveBaseName(const std::wstring& sourcePath);

// ファイル名の拡張子が png / jpg / jpeg（大文字小文字無視）のいずれかなら true。
// "a.b.c_cropped" のように'.'を含むだけの名前は false（拡張子補完が必要）。
bool HasSupportedImageExtension(const std::wstring& path);

}  // namespace path_utils
