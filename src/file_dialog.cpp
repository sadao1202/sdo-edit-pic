#define NOMINMAX
#include <windows.h>

#include <commdlg.h>

#include <cwchar>

#include "file_dialog.hpp"

namespace file_dialog {

namespace {

constexpr wchar_t kFilter[] = L"PNG/JPEG画像 (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0すべてのファイル (*.*)\0*.*\0";

constexpr wchar_t kSaveFilter[] =
    L"PNG画像 (*.png)\0*.png\0JPEG画像 (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0";

// パスのファイル名部分（ディレクトリ区切り以降）に'.'が含まれるかどうかを返す。
// ディレクトリ側に'.'があってもファイル名部分に無ければfalse。
bool HasFileNameExtension(const std::wstring& path) {
    size_t start = path.find_last_of(L"/\\");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    return path.find_last_of(L'.') != std::wstring::npos &&
           path.find_last_of(L'.') >= start;
}

}  // namespace

std::optional<std::wstring> OpenFileDialog() {
    wchar_t fileBuffer[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) {
        return std::nullopt;
    }
    return std::wstring(fileBuffer);
}

std::optional<std::wstring> SaveFileDialog(const std::wstring& initialDirectory,
                                            const std::wstring& initialBaseName) {
    std::wstring lastAttempt = initialBaseName;
    int filterIndex = 1;

    for (;;) {
        wchar_t fileBuffer[MAX_PATH] = L"";
        wcsncpy(fileBuffer, lastAttempt.c_str(), MAX_PATH - 1);
        fileBuffer[MAX_PATH - 1] = L'\0';

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = kSaveFilter;
        ofn.nFilterIndex = filterIndex;
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (!GetSaveFileNameW(&ofn)) {
            return std::nullopt;
        }
        filterIndex = ofn.nFilterIndex;

        std::wstring result(fileBuffer);
        const bool extensionAppended = !HasFileNameExtension(result);
        if (extensionAppended) {
            result += (ofn.nFilterIndex == 2) ? L".jpg" : L".png";
        }

        // 拡張子を自動補完した場合、OFN_OVERWRITEPROMPTは拡張子付加前の
        // 文字列に対してしか判定していないため、補完後のフルパスについて
        // 改めて上書き確認を行う。ユーザーが最初から拡張子を入力していた
        // 場合はダイアログ側の確認が既に効いているため二重に出さない。
        if (extensionAppended && GetFileAttributesW(result.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring message = result + L" は既に存在します。上書きしますか?";
            int choice = MessageBoxW(nullptr, message.c_str(), L"名前を付けて保存",
                                      MB_YESNO | MB_ICONWARNING);
            if (choice != IDYES) {
                // ダイアログに戻り、ユーザーに再入力させる。
                lastAttempt = fileBuffer;
                continue;
            }
        }

        return result;
    }
}

}  // namespace file_dialog
