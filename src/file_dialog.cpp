#define NOMINMAX
#include <windows.h>

#include <commdlg.h>

#include "file_dialog.hpp"

namespace file_dialog {

namespace {

constexpr wchar_t kFilter[] = L"PNG/JPEG画像 (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0すべてのファイル (*.*)\0*.*\0";

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

}  // namespace file_dialog
