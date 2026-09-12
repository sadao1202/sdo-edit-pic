#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <optional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.hがLoadImageをマクロ定義するため、image_io::LoadImageとの衝突を避ける。
#undef LoadImage

#include <imgui.h>

#include "file_dialog.hpp"
#include "image_io.hpp"

namespace {

// パスの拡張子を小文字化して返す（ドットなし）。
// ファイル名部分（ディレクトリ区切り以降）に限定してドットを探す。
std::wstring GetLowerExtension(const std::wstring& path) {
    size_t start = path.find_last_of(L"/\\");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    const size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos < start) {
        return L"";
    }
    std::wstring ext = path.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext;
}

// ディレクトリ・拡張子を除いたファイル名部分を返す。
std::wstring GetBaseNameWithoutExtension(const std::wstring& path) {
    size_t start = path.find_last_of(L"/\\");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos < start) {
        return path.substr(start);
    }
    return path.substr(start, dotPos - start);
}

// ファイル名部分のみ（拡張子含む）を返す。
std::wstring GetFileName(const std::wstring& path) {
    size_t start = path.find_last_of(L"/\\");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    return path.substr(start);
}

// exe自身が置かれているディレクトリを返す（末尾に'\'を付与）。
// バッファが切り詰められた場合や取得に失敗した場合はstd::nulloptを返す。
std::optional<std::wstring> GetExeDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0) {
        return std::nullopt;
    }
    if (length >= MAX_PATH) {
        // バッファに収まりきらず切り詰められた。
        return std::nullopt;
    }
    std::wstring exePath(buffer, length);
    size_t slashPos = exePath.find_last_of(L"/\\");
    if (slashPos == std::wstring::npos) {
        return std::nullopt;
    }
    return exePath.substr(0, slashPos + 1);
}

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

}  // namespace

void App::OnOpenClicked() {
    auto path = file_dialog::OpenFileDialog();
    if (!path.has_value()) {
        return;
    }

    std::string error;
    auto doc = image_io::LoadImage(*path, error);
    if (!doc.has_value()) {
        statusMessage_ = error;
        statusIsError_ = true;
        return;
    }

    document_ = std::move(doc);
    selectedPath_ = *path;
    texture_.Upload(document_->pixels.data(), document_->width, document_->height);
    statusMessage_.clear();
    statusIsError_ = false;
}

void App::OnConvertClicked() {
    if (!document_.has_value()) {
        return;
    }

    const std::wstring ext = GetLowerExtension(selectedPath_);
    std::wstring targetExt;
    if (ext == L"png") {
        targetExt = L"jpg";
    } else if (ext == L"jpg" || ext == L"jpeg") {
        targetExt = L"png";
    } else {
        statusIsError_ = true;
        statusMessage_ = "対応していない拡張子です（.png / .jpg / .jpeg のみ）。";
        return;
    }

    const auto exeDirectory = GetExeDirectory();
    if (!exeDirectory.has_value()) {
        statusIsError_ = true;
        statusMessage_ = "保存先ディレクトリの取得に失敗しました。";
        return;
    }
    const std::wstring outputPath =
        *exeDirectory + GetBaseNameWithoutExtension(selectedPath_) + L"." + targetExt;

    std::string error;
    bool ok = false;
    if (targetExt == L"jpg") {
        ok = image_io::SaveAsJpeg(*document_, outputPath, jpegQuality_, error);
    } else {
        ok = image_io::SaveAsPng(*document_, outputPath, error);
    }

    statusIsError_ = !ok;
    statusMessage_ = ok ? ("保存先: " + WStringToUtf8(outputPath)) : error;
}

void App::OnFrame() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("sdo-edit-pic", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("画像編集 - JPG/PNG変換");
    ImGui::Separator();

    if (ImGui::Button("画像を選択...")) {
        OnOpenClicked();
    }

    const std::wstring ext = GetLowerExtension(selectedPath_);

    if (document_.has_value()) {
        ImGui::Text("選択中: %s", WStringToUtf8(GetFileName(selectedPath_)).c_str());
        ImGui::Text("%d x %d", document_->width, document_->height);

        if (texture_.IsValid()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_.Id())),
                         ImVec2(static_cast<float>(texture_.Width()), static_cast<float>(texture_.Height())));
        }

        if (ext == L"png") {
            ImGui::SliderInt("JPEG品質", &jpegQuality_, 1, 100);
            jpegQuality_ = std::clamp(jpegQuality_, 1, 100);
        }

        if (document_->HasAlpha() && ext == L"png") {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "透明部分は白背景に変換されます");
        }
    }

    ImGui::BeginDisabled(!document_.has_value());
    if (ImGui::Button("変換して保存")) {
        OnConvertClicked();
    }
    ImGui::EndDisabled();

    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                            "%s", statusMessage_.c_str());
    }

    ImGui::End();
}
