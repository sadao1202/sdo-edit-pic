#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>

#include <imgui.h>

#include "file_dialog.hpp"
#include "image_io.hpp"

namespace {

// パスの拡張子を小文字化して返す（ドットなし）。
std::wstring GetLowerExtension(const std::wstring& path) {
    const size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos) {
        return L"";
    }
    std::wstring ext = path.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext;
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
    texture_.Upload(document_->pixels.data(), document_->width, document_->height);
    statusMessage_ = "読み込みました。";
    statusIsError_ = false;
}

void App::OnSaveAsClicked() {
    if (!document_.has_value()) {
        return;
    }

    auto path = file_dialog::SaveFileDialog();
    if (!path.has_value()) {
        return;
    }

    const std::wstring ext = GetLowerExtension(*path);
    std::string error;
    bool ok = false;

    if (ext == L"jpg" || ext == L"jpeg") {
        ok = image_io::SaveAsJpeg(*document_, *path, jpegQuality_, error);
    } else if (ext == L"png") {
        ok = image_io::SaveAsPng(*document_, *path, error);
    } else {
        error = "対応していない拡張子です（.png / .jpg / .jpeg のみ）。";
    }

    statusIsError_ = !ok;
    statusMessage_ = ok ? "保存しました。" : error;
}

void App::OnFrame() {
    ImGui::Begin("sdo-edit-pic");

    if (ImGui::Button("開く")) {
        OnOpenClicked();
    }

    if (document_.has_value()) {
        ImGui::SameLine();
        ImGui::Text("%d x %d", document_->width, document_->height);

        if (texture_.IsValid()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_.Id())),
                         ImVec2(static_cast<float>(texture_.Width()), static_cast<float>(texture_.Height())));
        }
    }

    ImGui::SliderInt("JPEG品質", &jpegQuality_, 1, 100);
    jpegQuality_ = std::clamp(jpegQuality_, 1, 100);

    if (document_.has_value() && document_->HasAlpha()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "透明部分は白背景に変換されます");
    }

    ImGui::BeginDisabled(!document_.has_value());
    if (ImGui::Button("別名で保存")) {
        OnSaveAsClicked();
    }
    ImGui::EndDisabled();

    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                            "%s", statusMessage_.c_str());
    }

    ImGui::End();
}
