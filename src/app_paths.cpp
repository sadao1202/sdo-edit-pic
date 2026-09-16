#include "app_paths.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shlobj.h>

namespace app_paths {

namespace {

std::string WStringToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                    nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                         result.data(), size, nullptr, nullptr);
    return result;
}

// %APPDATA%\sdo-edit-pic\ を取得し、存在しなければ作成する。
std::optional<std::wstring> ComputeDataDirectory() {
    PWSTR rawPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &rawPath);
    if (FAILED(hr) || rawPath == nullptr) {
        if (rawPath != nullptr) {
            CoTaskMemFree(rawPath);
        }
        return std::nullopt;
    }
    std::wstring appData(rawPath);
    CoTaskMemFree(rawPath);

    std::wstring dataDirectory = appData + L"\\sdo-edit-pic\\";

    if (!CreateDirectoryW(dataDirectory.c_str(), nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            return std::nullopt;
        }
    }

    return dataDirectory;
}

}  // namespace

std::optional<std::wstring> GetDataDirectory() {
    static const std::optional<std::wstring> kCached = ComputeDataDirectory();
    return kCached;
}

std::optional<std::string> GetImGuiIniPathUtf8() {
    const auto dataDirectory = GetDataDirectory();
    if (!dataDirectory.has_value()) {
        return std::nullopt;
    }
    return WStringToUtf8(*dataDirectory + L"imgui.ini");
}

}  // namespace app_paths
