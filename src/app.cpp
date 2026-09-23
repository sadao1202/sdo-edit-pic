#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <optional>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.hがLoadImageをマクロ定義するため、image_io::LoadImageとの衝突を避ける。
#undef LoadImage

#include <imgui.h>

#include "app_paths.hpp"
#include "file_dialog.hpp"
#include "image_codec.hpp"
#include "image_io.hpp"
#include "image_ops.hpp"

namespace {

constexpr int kDefaultJpegQuality = 100;

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
    originalDocument_ = *document_;
    previousDocument_.reset();
    appliedEditCount_ = 0;
    // 念のため、残っているクロッププレビュー状態をクリアしておく
    // （通常はIdle時のみ画像を開けるため発生しないはず）。
    previewDocument_.reset();
    previewTexture_.Release();
    mode_ = Mode::Idle;
    mosaicMask_.clear();
    std::vector<std::vector<unsigned char>>().swap(mosaicMaskHistory_);
    mosaicStrokeActive_ = false;
    mosaicMaskDirty_ = false;
    mosaicStrokePoints_.clear();
    statusMessage_.clear();
    statusIsError_ = false;
    // 新しい画像に切り替わったため、前の画像の表示位置・サイズのキャッシュは無効化する。
    ResetImageDisplayCache();
}

// mode_に応じた未適用結果（editedをmove）をdocument_に反映する共通処理。
// コピーを発生させないため、呼び出し元はstd::moveで所有権を渡すこと。
void App::ApplyEditedDocument(ImageDocument&& edited) {
    const bool wasMosaic = (mode_ == Mode::Mosaic);
    previousDocument_ = std::move(*document_);
    document_ = std::move(edited);
    texture_.Upload(document_->pixels.data(), document_->width, document_->height);
    previewDocument_.reset();
    previewTexture_.Release();
    ++appliedEditCount_;
    mode_ = Mode::Idle;
    // モザイクのモード離脱時はマスクを必ず解放する（次回モード突入時にサイズを取り直す）。
    mosaicMask_.clear();
    std::vector<std::vector<unsigned char>>().swap(mosaicMaskHistory_);
    mosaicStrokeActive_ = false;
    mosaicMaskDirty_ = false;
    mosaicStrokePoints_.clear();
    // footer構成が変わるため、前フレームのキャッシュを無効化する。
    ResetImageDisplayCache();
    statusMessage_ = wasMosaic ? "モザイクを適用しました" : "トリミングを適用しました";
    statusIsError_ = false;
}

void App::OnApplyClicked() {
    const bool cropReady = (mode_ == Mode::CropPreview);
    const bool mosaicReady = (mode_ == Mode::Mosaic && mosaicMaskDirty_);
    if ((!cropReady && !mosaicReady) || !previewDocument_.has_value()) {
        return;
    }
    ApplyEditedDocument(std::move(*previewDocument_));
}

void App::OnCancelEditClicked() {
    previewDocument_.reset();
    previewTexture_.Release();
    mode_ = Mode::Idle;
    mosaicMask_.clear();
    std::vector<std::vector<unsigned char>>().swap(mosaicMaskHistory_);
    mosaicStrokeActive_ = false;
    mosaicMaskDirty_ = false;
    mosaicStrokePoints_.clear();
    // CropPreview/Mosaic中はfooter構成が異なりレイアウトが変わるため、
    // Idleに戻った直後のフレームに古いキャッシュを使わないよう無効化する。
    ResetImageDisplayCache();
}

// Idle かつ document_ があるときに呼ばれる。モザイクモードに入り、
// document_と同サイズの空マスクとプレビューを用意する。
void App::OnMosaicClicked() {
    if (!document_.has_value() || mode_ != Mode::Idle) {
        return;
    }
    mosaicMask_.assign(static_cast<size_t>(document_->width) * static_cast<size_t>(document_->height), 0);
    std::vector<std::vector<unsigned char>>().swap(mosaicMaskHistory_);
    mosaicMaskDirty_ = false;
    mosaicStrokeActive_ = false;
    mosaicStrokePoints_.clear();
    previewDocument_ = *document_;
    previewTexture_.Upload(previewDocument_->pixels.data(), previewDocument_->width, previewDocument_->height);
    mode_ = Mode::Mosaic;
    ResetImageDisplayCache();
}

// 直前の1ストロークをmosaicMaskHistory_から復元して取り消す。ドラッグ中や履歴が
// 空の場合は何もしない。
void App::OnMosaicUndoStrokeClicked() {
    if (mode_ != Mode::Mosaic || mosaicStrokeActive_ || mosaicMaskHistory_.empty()) {
        return;
    }
    mosaicMask_ = std::move(mosaicMaskHistory_.back());
    mosaicMaskHistory_.pop_back();
    mosaicMaskDirty_ = !mosaicMaskHistory_.empty();
    mosaicStrokePoints_.clear();
    RecomputeMosaicPreview();
    statusMessage_ = "1ストローク戻しました";
    statusIsError_ = false;
}

// previewDocument_をdocument_から作り直し、mosaicMask_にApplyMosaicを適用して
// previewTexture_へアップロードする。常にdocument_（作業中の元画像）から計算し直す
// ため、塗り重ねても二重モザイクにならない。
void App::RecomputeMosaicPreview() {
    if (!document_.has_value()) {
        return;
    }
    previewDocument_ = *document_;
    image_ops::ApplyMosaic(*previewDocument_, mosaicMask_, mosaicBlockSize_);
    previewTexture_.Upload(previewDocument_->pixels.data(), previewDocument_->width, previewDocument_->height);
}

// 現在のdocument_を、ユーザーがダイアログで選んだ保存先に保存する。
// 保存は非破壊: document_ / previousDocument_ / appliedEditCount_ / mode_ /
// selectedPath_ のいずれも変更しない。
void App::OnSaveClicked() {
    if (!document_.has_value() || mode_ != Mode::Idle) {
        return;
    }

    const auto dataDirectory = app_paths::GetDataDirectory();
    const std::wstring initialDirectory = dataDirectory.value_or(L"");
    const std::wstring initialBaseName = GetBaseNameWithoutExtension(selectedPath_);

    auto outputPathOpt = file_dialog::SaveFileDialog(initialDirectory, initialBaseName);
    if (!outputPathOpt.has_value()) {
        return;
    }
    const std::wstring outputPath = *outputPathOpt;

    const std::wstring ext = GetLowerExtension(outputPath);
    if (ext != L"png" && ext != L"jpg" && ext != L"jpeg") {
        statusIsError_ = true;
        statusMessage_ = "対応していない拡張子です（.png / .jpg / .jpeg のみ）。";
        return;
    }

    std::string error;
    bool ok = false;
    if (ext == L"png") {
        ok = image_io::SaveAsPng(*document_, outputPath, error);
    } else {
        ok = image_io::SaveAsJpeg(*document_, outputPath, kDefaultJpegQuality, error);
    }

    statusIsError_ = !ok;
    statusMessage_ = ok ? ("保存先: " + WStringToUtf8(outputPath)) : error;
}

void App::OnUndoClicked() {
    if (!previousDocument_.has_value()) {
        return;
    }
    document_ = std::move(*previousDocument_);
    previousDocument_.reset();
    texture_.Upload(document_->pixels.data(), document_->width, document_->height);
    appliedEditCount_ = std::max(0, appliedEditCount_ - 1);
    ResetImageDisplayCache();
    statusMessage_ = "1つ前の状態に戻しました";
    statusIsError_ = false;
}

void App::OnRevertToOriginalClicked() {
    if (!originalDocument_.has_value()) {
        return;
    }
    document_ = *originalDocument_;
    previousDocument_.reset();
    appliedEditCount_ = 0;
    texture_.Upload(document_->pixels.data(), document_->width, document_->height);
    ResetImageDisplayCache();
    statusMessage_ = "元の画像に戻しました";
    statusIsError_ = false;
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
                previewDocument_.reset();
                previewTexture_.Release();
            } else {
                selRectLeft_ = left;
                selRectTop_ = top;
                selRectRight_ = right;
                selRectBottom_ = bottom;
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

// マウス入力を処理しmosaicMask_・mosaicStrokeActive_・mosaicMaskDirty_を更新する
// （UpdateCropInputStateと同形）。座標変換の前提は同じだが、Mosaic中は表示に
// previewTexture_（previewDocument_）を使う。previewDocument_はdocument_と同サイズ
// のため、texture_基準の座標変換式をそのまま使ってよい。
void App::UpdateMosaicInputState() {
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
    // UpdateCropInputStateと同じ理由で、下端に安全マージンを設けてボタン帯との
    // 誤操作を防ぐ。
    const float bottomSafetyMargin = ImGui::GetTextLineHeightWithSpacing();
    const float safeDisplayHeight = std::max(0.0f, lastDisplaySize_.y - bottomSafetyMargin);
    const bool insideImage = mousePos.x >= lastImageScreenPos_.x &&
                              mousePos.x <= lastImageScreenPos_.x + lastDisplaySize_.x &&
                              mousePos.y >= lastImageScreenPos_.y &&
                              mousePos.y <= lastImageScreenPos_.y + safeDisplayHeight;

    const int radius = std::max(1, mosaicBrushDiameter_ / 2);

    auto paintSegment = [&](const ImVec2& from, const ImVec2& to) {
        const int x0 = static_cast<int>(std::round(from.x));
        const int y0 = static_cast<int>(std::round(from.y));
        const int x1 = static_cast<int>(std::round(to.x));
        const int y1 = static_cast<int>(std::round(to.y));
        image_ops::PaintBrushLine(mosaicMask_, document_->width, document_->height, x0, y0, x1, y1, radius);
    };

    if (!mosaicStrokeActive_ && insideImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 px = screenToImagePx(mousePos);
        // ストローク開始前のマスクをスナップショットとして積んでおく（Undo用）。
        mosaicMaskHistory_.push_back(mosaicMask_);
        mosaicStrokeActive_ = true;
        mosaicLastImagePx_ = px;
        mosaicStrokePoints_.clear();
        mosaicStrokePoints_.push_back(px);
        paintSegment(px, px);
    } else if (mosaicStrokeActive_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 px = screenToImagePx(mousePos);
            paintSegment(mosaicLastImagePx_, px);
            mosaicLastImagePx_ = px;
            mosaicStrokePoints_.push_back(px);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // IsMouseReleasedのフレームではIsMouseDownがfalseのため、
            // 上のブロックで更新されない。離した瞬間の位置で塗り切ってから確定する。
            const ImVec2 px = screenToImagePx(mousePos);
            paintSegment(mosaicLastImagePx_, px);
            mosaicStrokeActive_ = false;
            mosaicMaskDirty_ = true;
            mosaicStrokePoints_.clear();
            // ここで初めて1回だけ、document_から作り直して再計算する。
            RecomputeMosaicPreview();
        }
    }
}

// モザイクのブラシ軌跡（ドラッグ中のみ）とカーソル位置のブラシ円アウトラインを
// 描画する。このフレームで確定したimageScreenPos/displaySizeを使う。
void App::DrawMosaicOverlay(const ImVec2& imageScreenPos, const ImVec2& displaySize) {
    if (!document_.has_value() || !texture_.IsValid()) {
        return;
    }
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || texture_.Width() <= 0 || texture_.Height() <= 0) {
        return;
    }

    ImGui::SetCursorScreenPos(imageScreenPos);
    ImGui::InvisibleButton("##mosaic_overlay", displaySize);

    const float fitScale = displaySize.x / static_cast<float>(texture_.Width());
    const float texToFullX = document_->width / static_cast<float>(texture_.Width());
    const float texToFullY = document_->height / static_cast<float>(texture_.Height());

    auto imagePxToScreen = [&](const ImVec2& px) {
        return ImVec2(imageScreenPos.x + px.x / texToFullX * fitScale, imageScreenPos.y + px.y / texToFullY * fitScale);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float brushRadiusScreen =
        std::max(1.0f, (mosaicBrushDiameter_ / 2.0f) / texToFullX * fitScale);

    if (mosaicStrokeActive_ && mosaicStrokePoints_.size() >= 2) {
        for (size_t i = 0; i + 1 < mosaicStrokePoints_.size(); ++i) {
            drawList->AddLine(imagePxToScreen(mosaicStrokePoints_[i]), imagePxToScreen(mosaicStrokePoints_[i + 1]),
                               IM_COL32(255, 80, 80, 160), brushRadiusScreen * 2.0f);
        }
    }

    if (ImGui::IsItemHovered()) {
        drawList->AddCircle(ImGui::GetMousePos(), brushRadiusScreen, IM_COL32(255, 255, 255, 220), 32, 2.0f);
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
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.has_value() || mode_ != Mode::Idle);
    if (ImGui::Button("モザイク")) {
        OnMosaicClicked();
    }
    ImGui::EndDisabled();

    if (document_.has_value()) {
        ImGui::Text("選択中: %s", WStringToUtf8(GetFileName(selectedPath_)).c_str());
        ImGui::Text("%d x %d", document_->width, document_->height);

        // CropPreview/Mosaic中は実際に編集を適用した結果画像（previewTexture_）を
        // 表示する。それ以外（Idle/Cropping）は元画像（texture_）を表示する。
        const bool showPreview =
            (mode_ == Mode::CropPreview || mode_ == Mode::Mosaic) && previewTexture_.IsValid();
        GLTexture& displayTexture = showPreview ? previewTexture_ : texture_;

        if (displayTexture.IsValid()) {
            if (!showPreview) {
                // footer高さ見積もり（mode_を参照する）より前にマウス入力を処理し、
                // このフレームで採用されるmode_を確定させる。これにより、ドラッグ確定
                // フレームでもfooter見積もりと実際の描画とでmode_の食い違いが生じない。
                UpdateCropInputState();
            } else if (mode_ == Mode::Mosaic) {
                // Mosaic中はプレビュー表示だが、ブラシ入力の受付とキャッシュ更新は
                // 必要なため、Cropとは異なりここでも入力処理を行う。
                UpdateMosaicInputState();
            }

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            // 画像より下に「このフレームで」表示される要素から、footer高さを見積もる。
            // 前フレームの実測値には頼らない（UI構成が変わるフレームでのガタつきを防ぐため）。
            float footerHeight = 0.0f;
            // [1] 案内テキスト/選択範囲サイズテキスト（Cropping中は非表示）。
            // Mosaic中は案内1行＋スライダー2行分を見積もる（漏らすと縦スクロールが再発する）。
            if (mode_ == Mode::Idle || mode_ == Mode::CropPreview) {
                footerHeight += ImGui::GetTextLineHeightWithSpacing();
            } else if (mode_ == Mode::Mosaic) {
                footerHeight += ImGui::GetTextLineHeightWithSpacing();
                footerHeight += ImGui::GetFrameHeightWithSpacing() * 2.0f;
            }
            // [3] アルファ警告テキスト（常時表示条件）
            if (document_->HasAlpha()) {
                footerHeight += ImGui::GetTextLineHeightWithSpacing();
            }
            // [4] 編集操作行（[適用][1手戻す][キャンセル]）: mode_ != Idleのときのみ1行
            if (mode_ != Mode::Idle) {
                footerHeight += ImGui::GetFrameHeightWithSpacing();
            }
            // [5] 画像操作行（[保存...][1つ前に戻す][最初の画像に戻す]）: 常時1行
            footerHeight += ImGui::GetFrameHeightWithSpacing();
            // [6] ステータスメッセージ
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
            } else if (mode_ == Mode::Mosaic) {
                DrawMosaicOverlay(imageScreenPos, displaySize);

                // Mosaic中はプレビュー表示中でも次フレームのUpdateMosaicInputStateで
                // 座標変換に使うため、キャッシュを更新する（CropPreviewとの違い）。
                lastImageScreenPos_ = imageScreenPos;
                lastDisplaySize_ = displaySize;
            }
        }

        if (mode_ == Mode::Idle) {
            ImGui::TextDisabled("ドラッグして範囲を選択するとトリミングできます");
        } else if (mode_ == Mode::CropPreview) {
            ImGui::Text("選択範囲: %d x %d px", selRectRight_ - selRectLeft_, selRectBottom_ - selRectTop_);
        } else if (mode_ == Mode::Mosaic) {
            ImGui::TextDisabled("ドラッグしてなぞった範囲にモザイクをかけます");
            ImGui::SliderInt("モザイクの粗さ", &mosaicBlockSize_, 2, 64);
            mosaicBlockSize_ = std::clamp(mosaicBlockSize_, 2, 64);
            if (ImGui::IsItemDeactivatedAfterEdit() && mosaicMaskDirty_) {
                RecomputeMosaicPreview();
            }
            ImGui::SliderInt("ブラシの太さ", &mosaicBrushDiameter_, 4, 200);
            mosaicBrushDiameter_ = std::clamp(mosaicBrushDiameter_, 4, 200);
        }

        // [3] アルファ警告テキスト（常時表示条件）
        if (document_->HasAlpha()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "JPEGで保存すると透明部分は白背景になります");
        }
    }

    // [4] 編集操作行：mode_ != Idleのときのみ表示
    if (mode_ != Mode::Idle) {
        const bool applyEnabled =
            (mode_ == Mode::CropPreview) || (mode_ == Mode::Mosaic && mosaicMaskDirty_);
        ImGui::BeginDisabled(!applyEnabled);
        if (ImGui::Button("適用")) {
            OnApplyClicked();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool undoStrokeEnabled =
            mode_ == Mode::Mosaic && !mosaicStrokeActive_ && !mosaicMaskHistory_.empty();
        ImGui::BeginDisabled(!undoStrokeEnabled);
        if (ImGui::Button("1手戻す")) {
            OnMosaicUndoStrokeClicked();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) {
            OnCancelEditClicked();
        }
    }

    // [5] 画像操作行：常に同じ位置に同じ3ボタン。編集中はグレーアウトのみ。
    ImGui::BeginDisabled(!document_.has_value() || mode_ != Mode::Idle);
    if (ImGui::Button("保存...")) {
        OnSaveClicked();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mode_ != Mode::Idle || !previousDocument_.has_value());
    if (ImGui::Button("1つ前に戻す")) {
        OnUndoClicked();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mode_ != Mode::Idle || appliedEditCount_ <= 0);
    if (ImGui::Button("最初の画像に戻す")) {
        OnRevertToOriginalClicked();
    }
    ImGui::EndDisabled();

    // [6] ステータスメッセージ
    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                            "%s", statusMessage_.c_str());
    }

    ImGui::End();
}
