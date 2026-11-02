#include "path_utils.hpp"

#include <cwctype>

namespace path_utils {

std::wstring GetBaseNameWithoutExtension(const std::wstring& path) {
    size_t start = path.find_last_of(L"/\\");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos < start) {
        return path.substr(start);
    }
    return path.substr(start, dotPos - start);
}

std::wstring GetDirectoryName(const std::wstring& path) {
    const size_t sepPos = path.find_last_of(L"/\\");
    if (sepPos == std::wstring::npos) {
        return L"";
    }
    // "C:\photo.jpg"・"/photo.jpg" はルートなので区切りを残す。
    if (sepPos == 0 || path[sepPos - 1] == L':') {
        return path.substr(0, sepPos + 1);
    }
    return path.substr(0, sepPos);
}

std::wstring MakeDefaultSaveBaseName(const std::wstring& sourcePath) {
    const std::wstring base = GetBaseNameWithoutExtension(sourcePath);
    if (base.empty()) {
        return L"cropped";
    }
    return base + L"_cropped";
}

bool HasSupportedImageExtension(const std::wstring& path) {
    size_t start = path.find_last_of(L"/\\");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    const size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos < start) {
        return false;
    }
    std::wstring ext = path.substr(dotPos + 1);
    for (wchar_t& c : ext) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return ext == L"png" || ext == L"jpg" || ext == L"jpeg";
}

}  // namespace path_utils
