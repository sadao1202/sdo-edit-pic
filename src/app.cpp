#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
#include "image_ops.hpp"

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

void App::OnCropSaveClicked() {
    if (!document_.has_value() || !hasPendingSelection_) {
        return;
    }

    const std::wstring ext = GetLowerExtension(selectedPath_);
    if (ext != L"png" && ext != L"jpg" && ext != L"jpeg") {
        statusIsError_ = true;
        statusMessage_ = "対応していない拡張子です（.png / .jpg / .jpeg のみ）。";
        return;
    }

    auto cropped = image_ops::CropImage(*document_, selRectLeft_, selRectTop_, selRectRight_, selRectBottom_);
    if (!cropped.has_value()) {
        statusIsError_ = true;
        statusMessage_ = "選択範囲が不正です。";
        return;
    }

    const auto exeDirectory = GetExeDirectory();
    if (!exeDirectory.has_value()) {
        statusIsError_ = true;
        statusMessage_ = "保存先ディレクトリの取得に失敗しました。";
        return;
    }

    const std::wstring outputPath =
        *exeDirectory + GetBaseNameWithoutExtension(selectedPath_) + L"_cropped." + ext;

    std::string error;
    bool ok = false;
    if (ext == L"png") {
        ok = image_io::SaveAsPng(*cropped, outputPath, error);
    } else {
        ok = image_io::SaveAsJpeg(*cropped, outputPath, jpegQuality_, error);
    }

    statusIsError_ = !ok;
    statusMessage_ = ok ? ("保存先: " + WStringToUtf8(outputPath)) : error;

    if (ok) {
        hasPendingSelection_ = false;
        mode_ = Mode::Idle;
    }
}

void App::OnCropBackClicked() {
    hasPendingSelection_ = false;
    mode_ = Mode::Idle;
}

// 画面座標系でのドラッグ操作を検出し、フル解像度画像ピクセル座標での選択範囲を確定する。
// imageScreenPos/displaySizeはImGui::Image直前に取得したフィット表示の位置・サイズ。
void App::UpdateCropInteraction(const ImVec2& imageScreenPos, const ImVec2& displaySize) {
    if (!document_.has_value() || !texture_.IsValid()) {
        return;
    }
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || texture_.Width() <= 0 || texture_.Height() <= 0) {
        return;
    }

    ImGui::SetCursorScreenPos(imageScreenPos);
    ImGui::InvisibleButton("##crop_overlay", displaySize);
    const bool hovered = ImGui::IsItemHovered();

    const float fitScale = displaySize.x / static_cast<float>(texture_.Width());
    const float texToFullX = document_->width / static_cast<float>(texture_.Width());
    const float texToFullY = document_->height / static_cast<float>(texture_.Height());

    auto screenToImagePx = [&](const ImVec2& screenPos) {
        const ImVec2 mouseInImage(screenPos.x - imageScreenPos.x, screenPos.y - imageScreenPos.y);
        float imagePxX = mouseInImage.x / fitScale * texToFullX;
        float imagePxY = mouseInImage.y / fitScale * texToFullY;
        imagePxX = std::clamp(imagePxX, 0.0f, static_cast<float>(document_->width));
        imagePxY = std::clamp(imagePxY, 0.0f, static_cast<float>(document_->height));
        return ImVec2(imagePxX, imagePxY);
    };

    if (mode_ == Mode::Idle && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragStartImagePx_ = screenToImagePx(ImGui::GetMousePos());
        dragCurrentImagePx_ = dragStartImagePx_;
        mode_ = Mode::Cropping;
    } else if (mode_ == Mode::Cropping) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragCurrentImagePx_ = screenToImagePx(ImGui::GetMousePos());
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // IsMouseReleasedがtrueのフレームではIsMouseDownはfalseのため、
            // 上のブロックで更新されない。離した瞬間の位置で確定するため再計算する。
            dragCurrentImagePx_ = screenToImagePx(ImGui::GetMousePos());
            int left = static_cast<int>(std::round(std::min(dragStartImagePx_.x, dragCurrentImagePx_.x)));
            int right = static_cast<int>(std::round(std::max(dragStartImagePx_.x, dragCurrentImagePx_.x)));
            int top = static_cast<int>(std::round(std::min(dragStartImagePx_.y, dragCurrentImagePx_.y)));
            int bottom = static_cast<int>(std::round(std::max(dragStartImagePx_.y, dragCurrentImagePx_.y)));
            left = std::clamp(left, 0, document_->width);
            right = std::clamp(right, 0, document_->width);
            top = std::clamp(top, 0, document_->height);
            bottom = std::clamp(bottom, 0, document_->height);

            if (right <= left || bottom <= top) {
                // 退化選択（移動量ゼロなど）は誤クリック救済のため無視する。
                mode_ = Mode::Idle;
                hasPendingSelection_ = false;
            } else {
                selRectLeft_ = left;
                selRectTop_ = top;
                selRectRight_ = right;
                selRectBottom_ = bottom;
                hasPendingSelection_ = true;
                mode_ = Mode::CropPreview;
            }
        }
    }

    if (mode_ == Mode::Cropping || (mode_ == Mode::CropPreview && hasPendingSelection_)) {
        float left, top, right, bottom;
        if (mode_ == Mode::Cropping) {
            left = std::min(dragStartImagePx_.x, dragCurrentImagePx_.x);
            right = std::max(dragStartImagePx_.x, dragCurrentImagePx_.x);
            top = std::min(dragStartImagePx_.y, dragCurrentImagePx_.y);
            bottom = std::max(dragStartImagePx_.y, dragCurrentImagePx_.y);
        } else {
            left = static_cast<float>(selRectLeft_);
            top = static_cast<float>(selRectTop_);
            right = static_cast<float>(selRectRight_);
            bottom = static_cast<float>(selRectBottom_);
        }

        // 画像ピクセル座標→画面座標への逆変換。
        const ImVec2 p0(imageScreenPos.x + left / texToFullX * fitScale,
                         imageScreenPos.y + top / texToFullY * fitScale);
        const ImVec2 p1(imageScreenPos.x + right / texToFullX * fitScale,
                         imageScreenPos.y + bottom / texToFullY * fitScale);
        ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
    }
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

    ImGui::BeginDisabled(mode_ != Mode::Idle);
    if (ImGui::Button("画像を選択...")) {
        OnOpenClicked();
    }
    ImGui::EndDisabled();

    const std::wstring ext = GetLowerExtension(selectedPath_);

    if (document_.has_value()) {
        ImGui::Text("選択中: %s", WStringToUtf8(GetFileName(selectedPath_)).c_str());
        ImGui::Text("%d x %d", document_->width, document_->height);

        if (texture_.IsValid()) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float fitScale = std::min({avail.x / static_cast<float>(texture_.Width()),
                                              avail.y / static_cast<float>(texture_.Height()), 1.0f});
            const ImVec2 displaySize(texture_.Width() * fitScale, texture_.Height() * fitScale);
            const ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();

            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_.Id())), displaySize);
            UpdateCropInteraction(imageScreenPos, displaySize);
        }

        if (mode_ == Mode::Idle) {
            ImGui::TextDisabled("ドラッグして範囲を選択するとトリミングできます");
        } else if (mode_ == Mode::CropPreview) {
            ImGui::Text("選択範囲: %d x %d px", selRectRight_ - selRectLeft_, selRectBottom_ - selRectTop_);
        }

        if (ext == L"png") {
            ImGui::SliderInt("JPEG品質", &jpegQuality_, 1, 100);
            jpegQuality_ = std::clamp(jpegQuality_, 1, 100);
        }

        if (document_->HasAlpha() && ext == L"png") {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "透明部分は白背景に変換されます");
        }
    }

    ImGui::BeginDisabled(!document_.has_value() || mode_ != Mode::Idle);
    if (ImGui::Button("変換して保存")) {
        OnConvertClicked();
    }
    ImGui::EndDisabled();

    if (mode_ != Mode::Idle) {
        ImGui::BeginDisabled(mode_ != Mode::CropPreview);
        if (ImGui::Button("保存")) {
            OnCropSaveClicked();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("戻る")) {
            OnCropBackClicked();
        }
    }

    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                            "%s", statusMessage_.c_str());
    }

    ImGui::End();
}
