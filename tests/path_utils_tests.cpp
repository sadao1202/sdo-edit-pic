// path_utils の自動テスト（GUI・Win32非依存の純粋関数のみを対象）。
// フレームワークは使わずassertベースの簡易実行ファイル。CTestから起動される。
//
// 試験観点は plan/save-default-name.md の「試験観点」に対応する。

#include <cassert>
#include <cstdio>
#include <string>

#include "path_utils.hpp"

namespace {

// 正常系: フォルダと既定名。
void Test_Normal() {
    assert(path_utils::GetDirectoryName(L"C:\\a\\photo.jpg") == L"C:\\a");
    assert(path_utils::MakeDefaultSaveBaseName(L"C:\\a\\photo.jpg") == L"photo_cropped");
}

// 正常系: 日本語・空白を含むパス。
void Test_JapaneseAndSpace() {
    const std::wstring path = L"C:\\画像 フォルダ\\写真 1.png";
    assert(path_utils::GetDirectoryName(path) == L"C:\\画像 フォルダ");
    assert(path_utils::MakeDefaultSaveBaseName(path) == L"写真 1_cropped");
}

// 異常系: 区切りなし・空パスはフォルダが空（呼び出し側でフォールバック）。
void Test_NoDirectory() {
    assert(path_utils::GetDirectoryName(L"photo.jpg").empty());
    assert(path_utils::GetDirectoryName(L"").empty());
    assert(path_utils::MakeDefaultSaveBaseName(L"photo.jpg") == L"photo_cropped");
}

// 異常系: ベース名が空なら "cropped"。
void Test_EmptyBaseName() {
    assert(path_utils::MakeDefaultSaveBaseName(L"C:\\a\\.png") == L"cropped");
    assert(path_utils::MakeDefaultSaveBaseName(L"") == L"cropped");
}

// 境界値: 拡張子なし、複数ドット、'/'区切り、末尾区切り、ルート。
void Test_Boundaries() {
    assert(path_utils::MakeDefaultSaveBaseName(L"C:\\a\\photo") == L"photo_cropped");
    assert(path_utils::MakeDefaultSaveBaseName(L"a.b.c.jpg") == L"a.b.c_cropped");
    // ドットがフォルダ名側にだけある場合は拡張子なし扱い。
    assert(path_utils::MakeDefaultSaveBaseName(L"C:\\a.b\\photo") == L"photo_cropped");
    assert(path_utils::GetDirectoryName(L"C:/a/photo.jpg") == L"C:/a");
    assert(path_utils::GetDirectoryName(L"C:\\a\\") == L"C:\\a");
    assert(path_utils::MakeDefaultSaveBaseName(L"C:\\a\\") == L"cropped");
    assert(path_utils::GetDirectoryName(L"C:\\photo.jpg") == L"C:\\");
    assert(path_utils::GetDirectoryName(L"/photo.jpg") == L"/");
}

// 境界値: 長いパスでも切れずに処理できる。
void Test_LongPath() {
    const std::wstring dir = L"C:\\" + std::wstring(300, L'd');
    const std::wstring name = std::wstring(300, L'n');
    assert(path_utils::GetDirectoryName(dir + L"\\" + name + L".png") == dir);
    assert(path_utils::MakeDefaultSaveBaseName(dir + L"\\" + name + L".png") == name + L"_cropped");
}

// 拡張子補完の判定: png/jpg/jpegのみ補完不要。'.'を含むだけの名前は補完が必要。
void Test_HasSupportedImageExtension() {
    assert(path_utils::HasSupportedImageExtension(L"x.PNG"));
    assert(path_utils::HasSupportedImageExtension(L"C:\\a\\x.jpeg"));
    assert(path_utils::HasSupportedImageExtension(L"a.b.c.jpg"));
    assert(!path_utils::HasSupportedImageExtension(L"a.b.c_cropped"));
    assert(!path_utils::HasSupportedImageExtension(L"IMG.2024"));
    assert(!path_utils::HasSupportedImageExtension(L"photo_cropped"));
    assert(!path_utils::HasSupportedImageExtension(L"C:\\a.png\\photo"));
    assert(!path_utils::HasSupportedImageExtension(L""));
}

}  // namespace

int main() {
    Test_Normal();
    Test_JapaneseAndSpace();
    Test_NoDirectory();
    Test_EmptyBaseName();
    Test_Boundaries();
    Test_LongPath();
    Test_HasSupportedImageExtension();
    std::printf("path_utils_tests: all passed\n");
    return 0;
}
