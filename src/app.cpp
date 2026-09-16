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

#include "app_paths.hpp"
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

void App::ResetImageDisplayCache() {
    lastImageScreenPos_ = ImVec2();
    lastDisplaySize_ = ImVec2();
    dragStartImagePx_ = ImVec2();
    dragCurrentImagePx_ = ImVec2();
}

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
    // 念のため、残っているクロッププレビュー状態をクリアしておく
    // （通常はIdle時のみ画像を開けるため発生しないはず）。
    previewDocument_.reset();
    previewTexture_.Release();
    hasPendingSelection_ = false;
    mode_ = Mode::Idle;
    statusMessage_.clear();
    statusIsError_ = false;
    // 新しい画像に切り替わったため、前の画像の表示位置・サイズのキャッシュは無効化する。
    ResetImageDisplayCache();
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

    const auto dataDirectory = app_paths::GetDataDirectory();
    if (!dataDirectory.has_value()) {
        statusIsError_ = true;
        statusMessage_ = "保存先ディレクトリの取得に失敗しました。";
        return;
    }
    const std::wstring outputPath =
        *dataDirectory + GetBaseNameWithoutExtension(selectedPath_) + L"." + targetExt;

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
    if (!document_.has_value() || !hasPendingSelection_ || !previewDocument_.has_value()) {
        return;
    }

    const std::wstring ext = GetLowerExtension(selectedPath_);
    if (ext != L"png" && ext != L"jpg" && ext != L"jpeg") {
        statusIsError_ = true;
        statusMessage_ = "対応していない拡張子です（.png / .jpg / .jpeg のみ）。";
        return;
    }

    const auto dataDirectory = app_paths::GetDataDirectory();
    if (!dataDirectory.has_value()) {
        statusIsError_ = true;
        statusMessage_ = "保存先ディレクトリの取得に失敗しました。";
        return;
    }

    const std::wstring outputPath =
        *dataDirectory + GetBaseNameWithoutExtension(selectedPath_) + L"_cropped." + ext;

    std::string error;
    bool ok = false;
    if (ext == L"png") {
        ok = image_io::SaveAsPng(*previewDocument_, outputPath, error);
    } else {
        ok = image_io::SaveAsJpeg(*previewDocument_, outputPath, jpegQuality_, error);
    }

    statusIsError_ = !ok;
    statusMessage_ = ok ? ("保存先: " + WStringToUtf8(outputPath)) : error;

    if (ok) {
        hasPendingSelection_ = false;
        previewDocument_.reset();
        previewTexture_.Release();
        mode_ = Mode::Idle;
        // CropPreview中はfooter構成が異なりレイアウトが変わるため、
        // Idleに戻った直後のフレームに古いキャッシュを使わないよう無効化する。
        ResetImageDisplayCache();
    }
}

void App::OnCropBackClicked() {
    hasPendingSelection_ = false;
    previewDocument_.reset();
    previewTexture_.Release();
    mode_ = Mode::Idle;
    // CropPreview中はfooter構成が異なりレイアウトが変わるため、
    // Idleに戻った直後のフレームに古いキャッシュを使わないよう無効化する。
    ResetImageDisplayCache();
}

// マウス入力を処理し、mode_・ドラッグ座標・選択範囲確定を更新する。
// 画面座標→画像ピクセル座標の変換には、前フレームでキャッシュした表示位置・サイズ
// （lastImageScreenPos_/lastDisplaySize_）を使う。これにより、このフレームのレイアウト
// 計算（footer高さ見積もり・fitScale計算）より前に呼び出しても座標変換が完結できる
// （＝レイアウト計算とmode_更新の循環依存を避けられる）。
// 通常操作ではウィンドウサイズ・レイアウトはフレーム間でほぼ変化しないため、
// 1フレーム前の表示位置・サイズを基準にしても実用上問題ない。
void App::UpdateCropInputState() {
    if (!document_.has_value() || !texture_.IsValid()) {
        return;
    }
    if (lastDisplaySize_.x <= 0.0f || lastDisplaySize_.y <= 0.0f || texture_.Width() <= 0 ||
        texture_.Height() <= 0) {
        return;
    }

    const float fitScale = lastDisplaySize_.x / static_cast<float>(texture_.Width());
    const float texToFullX = document_->width / static_cast<float>(texture_.Width());
    const float texToFullY = document_->height / static_cast<float>(texture_.Height());

    auto screenToImagePx = [&](const ImVec2& screenPos) {
        const ImVec2 mouseInImage(screenPos.x - lastImageScreenPos_.x, screenPos.y - lastImageScreenPos_.y);
        float imagePxX = mouseInImage.x / fitScale * texToFullX;
        float imagePxY = mouseInImage.y / fitScale * texToFullY;
        imagePxX = std::clamp(imagePxX, 0.0f, static_cast<float>(document_->width));
        imagePxY = std::clamp(imagePxY, 0.0f, static_cast<float>(document_->height));
        return ImVec2(imagePxX, imagePxY);
    };

    const ImVec2 mousePos = ImGui::GetMousePos();
    // InvisibleButtonのIsItemHoveredの代わりに、キャッシュした表示矩形との
    // 簡易な内外判定を用いる（他ウィンドウによる遮蔽は考慮しないが、
    // 本アプリは全画面固定の単一ウィンドウ構成のため実用上問題ない）。
    // 下端はfooter高さの変動（statusMessage_の有無等で1行分縮む場合がある）を
    // 吸収するため、安全マージン分だけ内側に縮めて判定する。これにより、
    // 画像下のボタン群が上方向にシフトした際に、前フレームでキャッシュされた
    // （縮む前の大きい）矩形とボタン位置が重なる帯でのクリックが誤って
    // Croppingモードへの遷移を引き起こすことを防ぐ。
    const float bottomSafetyMargin = ImGui::GetTextLineHeightWithSpacing();
    const float safeDisplayHeight = std::max(0.0f, lastDisplaySize_.y - bottomSafetyMargin);
    const bool insideImage = mousePos.x >= lastImageScreenPos_.x &&
                              mousePos.x <= lastImageScreenPos_.x + lastDisplaySize_.x &&
                              mousePos.y >= lastImageScreenPos_.y &&
                              mousePos.y <= lastImageScreenPos_.y + safeDisplayHeight;

    if (mode_ == Mode::Idle && insideImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragStartImagePx_ = screenToImagePx(mousePos);
        dragCurrentImagePx_ = dragStartImagePx_;
        mode_ = Mode::Cropping;
    } else if (mode_ == Mode::Cropping) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragCurrentImagePx_ = screenToImagePx(mousePos);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // IsMouseReleasedがtrueのフレームではIsMouseDownはfalseのため、
            // 上のブロックで更新されない。離した瞬間の位置で確定するため再計算する。
            dragCurrentImagePx_ = screenToImagePx(mousePos);
            int left = static_cast<int>(std::round(std::min(dragStartImagePx_.x, dragCurrentImagePx_.x)));
            int right = static_cast<int>(std::round(std::max(dragStartImagePx_.x, dragCurrentImagePx_.x)));
            int top = static_cast<int>(std::round(std::min(dragStartImagePx_.y, dragCurrentImagePx_.y)));
            int bottom = static_cast<int>(std::round(std::max(dragStartImagePx_.y, dragCurrentImagePx_.y)));
            left = std::clamp(left, 0, document_->width);
            right = std::clamp(right, 0, document_->width);
            top = std::clamp(top, 0, document_->height);
            bottom = std::clamp(bottom, 0, document_->height);

            auto cropped = image_ops::CropImage(*document_, left, top, right, bottom);
            if (!cropped.has_value()) {
                // 退化選択（移動量ゼロなど）は誤クリック救済のため無視する。
                mode_ = Mode::Idle;
                hasPendingSelection_ = false;
                previewDocument_.reset();
                previewTexture_.Release();
            } else {
                selRectLeft_ = left;
                selRectTop_ = top;
                selRectRight_ = right;
                selRectBottom_ = bottom;
                hasPendingSelection_ = true;
                previewDocument_ = std::move(cropped);
                previewTexture_.Upload(previewDocument_->pixels.data(), previewDocument_->width,
                                        previewDocument_->height);
                mode_ = Mode::CropPreview;
            }
        }
    }
}

// 選択範囲のオーバーレイ描画とホバー領域(InvisibleButton)の配置を行う。
// imageScreenPos/displaySizeはImGui::Image直前に取得した、このフレームで確定した
// フィット表示の位置・サイズ。ここで配置したInvisibleButtonは次フレームの
// UpdateCropInputStateでの入力判定には使わず、あくまでこのフレームの見た目
// （ホバー時のカーソル等）のためのもの。
void App::DrawCropOverlay(const ImVec2& imageScreenPos, const ImVec2& displaySize) {
    if (!document_.has_value() || !texture_.IsValid()) {
        return;
    }
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || texture_.Width() <= 0 || texture_.Height() <= 0) {
        return;
    }

    ImGui::SetCursorScreenPos(imageScreenPos);
    ImGui::InvisibleButton("##crop_overlay", displaySize);

    const float fitScale = displaySize.x / static_cast<float>(texture_.Width());
    const float texToFullX = document_->width / static_cast<float>(texture_.Width());
    const float texToFullY = document_->height / static_cast<float>(texture_.Height());

    if (mode_ == Mode::Cropping) {
        const float left = std::min(dragStartImagePx_.x, dragCurrentImagePx_.x);
        const float right = std::max(dragStartImagePx_.x, dragCurrentImagePx_.x);
        const float top = std::min(dragStartImagePx_.y, dragCurrentImagePx_.y);
        const float bottom = std::max(dragStartImagePx_.y, dragCurrentImagePx_.y);

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

        // CropPreview中は実際にクロップした結果画像（previewTexture_）を表示する。
        // それ以外（Idle/Cropping）は元画像（texture_）を表示する。
        const bool showPreview = (mode_ == Mode::CropPreview) && previewTexture_.IsValid();
        GLTexture& displayTexture = showPreview ? previewTexture_ : texture_;

        if (displayTexture.IsValid()) {
            if (!showPreview) {
                // footer高さ見積もり（mode_を参照する）より前にマウス入力を処理し、
                // このフレームで採用されるmode_を確定させる。これにより、ドラッグ確定
                // フレームでもfooter見積もりと実際の描画とでmode_の食い違いが生じない。
                UpdateCropInputState();
            }

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            // 画像より下に「このフレームで」表示される要素から、footer高さを見積もる。
            // 前フレームの実測値には頼らない（UI構成が変わるフレームでのガタつきを防ぐため）。
            float footerHeight = 0.0f;
            // 案内テキスト/選択範囲サイズテキスト（Cropping中は非表示）
            if (mode_ == Mode::Idle || mode_ == Mode::CropPreview) {
                footerHeight += ImGui::GetTextLineHeightWithSpacing();
            }
            // JPEG品質スライダー
            if (ext == L"png") {
                footerHeight += ImGui::GetFrameHeightWithSpacing();
            }
            // アルファ警告テキスト
            if (document_->HasAlpha() && ext == L"png") {
                footerHeight += ImGui::GetTextLineHeightWithSpacing();
            }
            // 「変換して保存」ボタン（常に表示）
            footerHeight += ImGui::GetFrameHeightWithSpacing();
            // 「保存」「戻る」ボタン行
            if (mode_ != Mode::Idle) {
                footerHeight += ImGui::GetFrameHeightWithSpacing();
            }
            // ステータスメッセージ
            if (!statusMessage_.empty()) {
                footerHeight += ImGui::GetTextLineHeightWithSpacing();
            }
            const float availableImageHeight = std::max(0.0f, avail.y - footerHeight);
            const float fitScale =
                std::min({avail.x / static_cast<float>(displayTexture.Width()),
                          availableImageHeight / static_cast<float>(displayTexture.Height()), 1.0f});
            const ImVec2 displaySize(displayTexture.Width() * fitScale, displayTexture.Height() * fitScale);
            const ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();

            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(displayTexture.Id())), displaySize);
            if (!showPreview) {
                DrawCropOverlay(imageScreenPos, displaySize);

                // 次フレームのUpdateCropInputStateで使うため、このフレームで確定した
                // 表示位置・サイズをキャッシュしておく（Idle/Cropping時のみ）。
                lastImageScreenPos_ = imageScreenPos;
                lastDisplaySize_ = displaySize;
            }
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
