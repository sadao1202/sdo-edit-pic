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
#include "path_utils.hpp"

namespace {

constexpr int kDefaultJpegQuality = 100;

// 画面比率トリミングで選べる比率（横長→縦長の順）。
struct CropAspect {
    const char* label;
    int rw;
    int rh;
};
constexpr CropAspect kCropAspects[] = {
    {"1:1", 1, 1},  {"4:3", 4, 3},  {"3:2", 3, 2},  {"16:9", 16, 9}, {"21:9", 21, 9},
    {"3:4", 3, 4},  {"2:3", 2, 3},  {"9:16", 9, 16}, {"9:21", 9, 21},
};
constexpr int kCropAspectCount = static_cast<int>(sizeof(kCropAspects) / sizeof(kCropAspects[0]));

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
    // 残っている編集系の状態をすべてクリアしておく
    // （通常はIdle時のみ画像を開けるため発生しないはず）。
    ResetEditUiState();
    statusMessage_.clear();
    statusIsError_ = false;
}

// 編集系の状態を一括で初期化してIdleに戻す。リセット漏れを防ぐため、状態を持つ変数を
// 増やしたらここに追加すること。ドラッグ座標も画像切り替え直後に古い値を使わないよう無効化する。
void App::ResetEditUiState() {
    previewDocument_.reset();
    previewTexture_.Release();
    mode_ = Mode::Idle;
    cropMethod_ = 0;
    cropFixedDragging_ = false;
    // モザイクのモード離脱時はマスクを必ず解放する（次回モード突入時にサイズを取り直す）。
    mosaicMask_.clear();
    std::vector<std::vector<unsigned char>>().swap(mosaicMaskHistory_);
    mosaicStrokeActive_ = false;
    mosaicMaskDirty_ = false;
    mosaicStrokePoints_.clear();
    ResetImageDisplayCache();
}

// mode_に応じた未適用結果（editedをmove）をdocument_に反映する共通処理。
// コピーを発生させないため、呼び出し元はstd::moveで所有権を渡すこと。
void App::ApplyEditedDocument(ImageDocument&& edited) {
    const bool wasMosaic = (mode_ == Mode::Mosaic);
    previousDocument_ = std::move(*document_);
    document_ = std::move(edited);
    texture_.Upload(document_->pixels.data(), document_->width, document_->height);
    ++appliedEditCount_;
    ResetEditUiState();
    statusMessage_ = wasMosaic ? "モザイクを適用しました" : "トリミングを適用しました";
    statusIsError_ = false;
}

void App::OnApplyClicked() {
    if (mode_ == Mode::CropFixed && document_.has_value()) {
        auto cropped = image_ops::CropImage(*document_, cropFixedLeft_, cropFixedTop_,
                                             cropFixedLeft_ + cropFixedW_, cropFixedTop_ + cropFixedH_);
        if (cropped.has_value()) {
            ApplyEditedDocument(std::move(*cropped));
        }
        return;
    }
    const bool cropReady = (mode_ == Mode::CropPreview);
    const bool mosaicReady = (mode_ == Mode::Mosaic && mosaicMaskDirty_);
    if ((!cropReady && !mosaicReady) || !previewDocument_.has_value()) {
        return;
    }
    ApplyEditedDocument(std::move(*previewDocument_));
}

void App::OnCancelEditClicked() {
    ResetEditUiState();
}

// Idle かつ document_ があるときに呼ばれる。自由選択トリミングの待機に入る。
void App::OnCropClicked() {
    if (!document_.has_value() || mode_ != Mode::Idle) {
        return;
    }
    ResetEditUiState();
    mode_ = Mode::CropFree;
}

// Idle かつ document_ があるときに呼ばれる。モザイクモードに入り、
// document_と同サイズの空マスクとプレビューを用意する。
void App::OnMosaicClicked() {
    if (!document_.has_value() || mode_ != Mode::Idle) {
        return;
    }
    ResetEditUiState();
    mosaicMask_.assign(static_cast<size_t>(document_->width) * static_cast<size_t>(document_->height), 0);
    previewDocument_ = *document_;
    previewTexture_.Upload(previewDocument_->pixels.data(), previewDocument_->width, previewDocument_->height);
    mode_ = Mode::Mosaic;
}

// CropFree/CropFixed で document_ があるときに呼ばれる。枠サイズは前回値（未設定なら画像の50%）を
// 画像内に丸めたうえで、画像中央に配置する。
// CropFixed中（画面比率からの切替）は現在の枠をそのまま使い、高さを編集可能に戻すだけ。
void App::EnterCropFixed() {
    if (!document_.has_value() || (mode_ != Mode::CropFree && mode_ != Mode::CropFixed)) {
        return;
    }
    if (mode_ == Mode::CropFixed) {
        cropMethod_ = 1;
        return;
    }
    const int w = cropFixedW_ > 0 ? cropFixedW_ : document_->width / 2;
    const int h = cropFixedH_ > 0 ? cropFixedH_ : document_->height / 2;
    auto rect = image_ops::ClampFixedRect(document_->width, document_->height, w, h, 0, 0);
    rect = image_ops::ClampFixedRect(document_->width, document_->height, rect.w, rect.h,
                                      (document_->width - rect.w) / 2, (document_->height - rect.h) / 2);
    cropFixedW_ = rect.w;
    cropFixedH_ = rect.h;
    cropFixedLeft_ = rect.left;
    cropFixedTop_ = rect.top;
    cropFixedDragging_ = false;
    cropMethod_ = 1;
    mode_ = Mode::CropFixed;
    ResetImageDisplayCache();
}

// CropFreeからは比率で収まる最大枠を中央に配置する。CropFixed中（サイズ指定から切替）は
// 枠の中心と現在の幅を維持できる範囲で新しい比率に合わせる。
void App::EnterCropAspect() {
    if (!document_.has_value() || (mode_ != Mode::CropFree && mode_ != Mode::CropFixed)) {
        return;
    }
    if (mode_ == Mode::CropFixed) {
        SetCropAspectWidth(cropFixedW_, cropFixedLeft_ + cropFixedW_ / 2.0, cropFixedTop_ + cropFixedH_ / 2.0);
        cropMethod_ = 2;
        return;
    }
    const auto& aspect = kCropAspects[cropAspectIdx_];
    const auto rect = image_ops::MaxAspectRect(document_->width, document_->height, aspect.rw, aspect.rh);
    cropFixedW_ = rect.w;
    cropFixedH_ = rect.h;
    cropFixedLeft_ = rect.left;
    cropFixedTop_ = rect.top;
    cropFixedDragging_ = false;
    cropMethod_ = 2;
    mode_ = Mode::CropFixed;
    ResetImageDisplayCache();
}

void App::SetCropAspectWidth(int w, double cx, double cy) {
    const auto& aspect = kCropAspects[cropAspectIdx_];
    // 幅・高さを先に確定させてから、中心を維持する位置を求める。
    const auto sized = image_ops::ClampAspectRect(document_->width, document_->height, aspect.rw, aspect.rh,
                                                   w, 0, 0);
    const auto rect = image_ops::ClampAspectRect(
        document_->width, document_->height, aspect.rw, aspect.rh, sized.w,
        static_cast<int>(std::round(cx - sized.w / 2.0)), static_cast<int>(std::round(cy - sized.h / 2.0)));
    cropFixedW_ = rect.w;
    cropFixedH_ = rect.h;
    cropFixedLeft_ = rect.left;
    cropFixedTop_ = rect.top;
}

void App::ExitCropFixed() {
    if (mode_ != Mode::CropFixed) {
        return;
    }
    mode_ = Mode::CropFree;
    cropMethod_ = 0;
    cropFixedDragging_ = false;
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

    // 初期フォルダは元画像のフォルダ。取れなければアプリのデータディレクトリ。
    std::wstring initialDirectory = path_utils::GetDirectoryName(selectedPath_);
    if (initialDirectory.empty()) {
        initialDirectory = app_paths::GetDataDirectory().value_or(L"");
    }
    const std::wstring initialBaseName = path_utils::MakeDefaultSaveBaseName(selectedPath_);

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
// 画面座標→画像ピクセル座標の変換には、このフレームの冒頭で算出した画像矩形
// （imageScreenPos/displaySize。画像エリアの大きさと画像寸法のみから決まる）を使う。
// レイアウトはモードや操作パネルの内容に依存しないため、前フレームの値のキャッシュは不要。
void App::UpdateCropInputState(const ImVec2& imageScreenPos, const ImVec2& displaySize) {
    if (!document_.has_value() || !texture_.IsValid()) {
        return;
    }
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || texture_.Width() <= 0 || texture_.Height() <= 0) {
        return;
    }

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

    const ImVec2 mousePos = ImGui::GetMousePos();
    // 画像エリアと操作パネルは別Childで重ならないため、矩形内外の単純な判定で足りる。
    const bool insideImage = mousePos.x >= imageScreenPos.x && mousePos.x <= imageScreenPos.x + displaySize.x &&
                              mousePos.y >= imageScreenPos.y && mousePos.y <= imageScreenPos.y + displaySize.y;

    if (mode_ == Mode::CropFree && insideImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
                // 退化選択（移動量ゼロなど）は誤クリック救済のため無視し、待機に戻る。
                mode_ = Mode::CropFree;
                previewDocument_.reset();
            } else {
                selRectLeft_ = left;
                selRectTop_ = top;
                selRectRight_ = right;
                selRectBottom_ = bottom;
                // CropPreview中は元画像上に枠を重ねて表示するため、previewTexture_は使わない。
                previewDocument_ = std::move(cropped);
                mode_ = Mode::CropPreview;
            }
        }
    } else if (mode_ == Mode::CropFixed) {
        // 枠は移動のみ。枠内でクリックしたらドラッグ開始し、画像内にクランプして追従させる。
        auto moveFrameTo = [&](const ImVec2& px) {
            const auto rect = image_ops::ClampFixedRect(
                document_->width, document_->height, cropFixedW_, cropFixedH_,
                static_cast<int>(std::round(px.x - cropFixedGrabOffset_.x)),
                static_cast<int>(std::round(px.y - cropFixedGrabOffset_.y)));
            cropFixedLeft_ = rect.left;
            cropFixedTop_ = rect.top;
        };
        if (!cropFixedDragging_ && insideImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 px = screenToImagePx(mousePos);
            if (px.x >= cropFixedLeft_ && px.x <= cropFixedLeft_ + cropFixedW_ && px.y >= cropFixedTop_ &&
                px.y <= cropFixedTop_ + cropFixedH_) {
                cropFixedDragging_ = true;
                cropFixedGrabOffset_ = ImVec2(px.x - cropFixedLeft_, px.y - cropFixedTop_);
            }
        } else if (cropFixedDragging_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                moveFrameTo(screenToImagePx(mousePos));
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                cropFixedDragging_ = false;
            }
        }
    }
}

// 選択範囲のオーバーレイ描画とホバー領域(InvisibleButton)の配置を行う。
// imageScreenPos/displaySizeはこのフレームで算出したフィット表示の位置・サイズ。
// ここで配置したInvisibleButtonは入力判定には使わず、あくまで見た目
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
    } else if (mode_ == Mode::CropFixed || mode_ == Mode::CropPreview) {
        // 選択枠（画像px）。CropFixedは移動中の枠、CropPreviewは確定した選択範囲。
        const bool fixed = (mode_ == Mode::CropFixed);
        const int selL = fixed ? cropFixedLeft_ : selRectLeft_;
        const int selT = fixed ? cropFixedTop_ : selRectTop_;
        const int selR = fixed ? cropFixedLeft_ + cropFixedW_ : selRectRight_;
        const int selB = fixed ? cropFixedTop_ + cropFixedH_ : selRectBottom_;
        const ImVec2 r0(imageScreenPos.x + selL / texToFullX * fitScale,
                         imageScreenPos.y + selT / texToFullY * fitScale);
        const ImVec2 r1(imageScreenPos.x + selR / texToFullX * fitScale,
                         imageScreenPos.y + selB / texToFullY * fitScale);
        const ImVec2 i0 = imageScreenPos;
        const ImVec2 i1(imageScreenPos.x + displaySize.x, imageScreenPos.y + displaySize.y);

        // 枠外を4つの矩形で暗くする。
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 dim = IM_COL32(0, 0, 0, 140);
        drawList->AddRectFilled(i0, ImVec2(i1.x, r0.y), dim);
        drawList->AddRectFilled(ImVec2(i0.x, r1.y), i1, dim);
        drawList->AddRectFilled(ImVec2(i0.x, r0.y), ImVec2(r0.x, r1.y), dim);
        drawList->AddRectFilled(ImVec2(r1.x, r0.y), ImVec2(i1.x, r1.y), dim);
        drawList->AddRect(r0, r1, IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
    }
}

// マウス入力を処理しmosaicMask_・mosaicStrokeActive_・mosaicMaskDirty_を更新する
// （UpdateCropInputStateと同形）。Mosaic中の表示はpreviewTexture_だが、previewDocument_は
// document_と同サイズのため、imageScreenPos/displaySizeにはtexture_基準の座標変換式をそのまま使える。
void App::UpdateMosaicInputState(const ImVec2& imageScreenPos, const ImVec2& displaySize) {
    if (!document_.has_value() || !texture_.IsValid()) {
        return;
    }
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || texture_.Width() <= 0 || texture_.Height() <= 0) {
        return;
    }

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

    const ImVec2 mousePos = ImGui::GetMousePos();
    const bool insideImage = mousePos.x >= imageScreenPos.x && mousePos.x <= imageScreenPos.x + displaySize.x &&
                              mousePos.y >= imageScreenPos.y && mousePos.y <= imageScreenPos.y + displaySize.y;

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
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // ウィンドウ全体を、左の画像エリアと右の操作パネル（固定幅）に分ける。
    // 画像エリアの大きさはウィンドウサイズだけで決まり、モード・操作パネルの内容
    // （行数の増減）・ステータス表示には一切依存しない。これにより画像の位置・サイズは
    // モードを遷移しても変わらない。
    constexpr float kPanelWidth = 300.0f;
    const ImVec2 windowAvail = ImGui::GetContentRegionAvail();
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 areaPos = ImGui::GetCursorScreenPos();
    const ImVec2 areaSize(std::max(1.0f, windowAvail.x - kPanelWidth - itemSpacingX),
                           std::max(1.0f, windowAvail.y));

    // 表示対象のテクスチャと、その画像矩形（画面位置・サイズ）を求める。
    // 入力は画像エリアの大きさと表示画像の寸法のみ（前フレームの値には頼らない）。
    // Mosaic中は編集結果（previewTexture_。document_と同寸法）、それ以外は元画像（texture_）を
    // 表示する。CropPreviewも元画像上に枠を重ねる表示のため、全モードで表示寸法は
    // document_の寸法と一致し、矩形は不変。
    ImVec2 imageScreenPos(areaPos.x, areaPos.y);
    ImVec2 displaySize(0.0f, 0.0f);
    bool showPreview = false;
    auto layoutImage = [&]() {
        showPreview = (mode_ == Mode::Mosaic) && previewTexture_.IsValid();
        const GLTexture& tex = showPreview ? previewTexture_ : texture_;
        if (!document_.has_value() || !tex.IsValid()) {
            displaySize = ImVec2(0.0f, 0.0f);
            return;
        }
        const auto fit = image_ops::FitSize(areaSize.x, areaSize.y, static_cast<float>(tex.Width()),
                                             static_cast<float>(tex.Height()));
        displaySize = ImVec2(fit.w, fit.h);
        // 画像エリア内で中央配置する。ピクセル境界に揃えてサブピクセルのにじみを避ける。
        imageScreenPos = ImVec2(std::floor(areaPos.x + (areaSize.x - fit.w) * 0.5f),
                                 std::floor(areaPos.y + (areaSize.y - fit.h) * 0.5f));
    };

    // 入力処理は、操作前（このフレーム冒頭）の表示状態の矩形で行う。
    layoutImage();
    if (document_.has_value() && displaySize.x > 0.0f) {
        if (!showPreview) {
            UpdateCropInputState(imageScreenPos, displaySize);
        } else if (mode_ == Mode::Mosaic) {
            UpdateMosaicInputState(imageScreenPos, displaySize);
        }
    }
    // 入力でmode_が変わった（例: ドラッグ確定でCropPreviewへ）場合の表示に合わせて再計算する。
    layoutImage();

    // [左] 画像エリア
    ImGui::BeginChild("##image_area", areaSize, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (document_.has_value() && displaySize.x > 0.0f) {
        const GLTexture& displayTexture = showPreview ? previewTexture_ : texture_;
        ImGui::SetCursorScreenPos(imageScreenPos);
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(displayTexture.Id())), displaySize);
        if (!showPreview) {
            DrawCropOverlay(imageScreenPos, displaySize);
        } else if (mode_ == Mode::Mosaic) {
            DrawMosaicOverlay(imageScreenPos, displaySize);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // [右] 操作パネル（固定幅）。縦の増減はここだけで起き、画像エリアには影響しない。
    // 幅が足りなくても切れないよう、ボタンは全幅で縦積み、文章は折り返す。
    ImGui::BeginChild("##panel", ImVec2(kPanelWidth, areaSize.y), ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.0f);
    const ImVec2 fullWidth(-FLT_MIN, 0.0f);

    ImGui::Text("画像編集 - JPG/PNG変換");
    ImGui::Separator();

    ImGui::BeginDisabled(mode_ != Mode::Idle);
    if (ImGui::Button("画像を選択...", fullWidth)) {
        OnOpenClicked();
    }
    ImGui::EndDisabled();

    bool cropClicked = false;
    bool mosaicClicked = false;
    if (document_.has_value()) {
        ImGui::Text("選択中: %s", WStringToUtf8(GetFileName(selectedPath_)).c_str());
        ImGui::Text("%d x %d", document_->width, document_->height);
        ImGui::Separator();

        if (mode_ == Mode::Idle) {
            ImGui::Text("修正したい項目をえらんでください");
            if (ImGui::Button("トリミング", fullWidth)) {
                cropClicked = true;
            }
            if (ImGui::Button("モザイク", fullWidth)) {
                mosaicClicked = true;
            }
        } else if (mode_ == Mode::CropFree) {
            ImGui::TextDisabled("ドラッグして範囲を選択してください");
        } else if (mode_ == Mode::CropPreview) {
            ImGui::Text("選択範囲: %d x %d px", selRectRight_ - selRectLeft_, selRectBottom_ - selRectTop_);
        } else if (mode_ == Mode::CropFixed) {
            ImGui::Text("選択範囲: %d x %d px", cropFixedW_, cropFixedH_);
        } else if (mode_ == Mode::Mosaic) {
            ImGui::TextDisabled("ドラッグしてなぞった範囲にモザイクをかけます");
            ImGui::Text("モザイクの粗さ");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##mosaic_block", &mosaicBlockSize_, 2, 64);
            mosaicBlockSize_ = std::clamp(mosaicBlockSize_, 2, 64);
            if (ImGui::IsItemDeactivatedAfterEdit() && mosaicMaskDirty_) {
                RecomputeMosaicPreview();
            }
            ImGui::Text("ブラシの太さ");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##mosaic_brush", &mosaicBrushDiameter_, 4, 200);
            mosaicBrushDiameter_ = std::clamp(mosaicBrushDiameter_, 4, 200);
        }

        // トリミング方式セレクタ。CropFree/CropFixed以外（ドラッグ中・プレビュー中）は
        // 表示のみで操作不可にする。狭幅で切れないよう縦積みにする。
        const bool showCropRadio = (mode_ == Mode::CropFree || mode_ == Mode::Cropping ||
                                    mode_ == Mode::CropPreview || mode_ == Mode::CropFixed);
        if (showCropRadio) {
            const int frameCropMethod = cropMethod_;
            ImGui::BeginDisabled(mode_ != Mode::CropFree && mode_ != Mode::CropFixed);
            if (ImGui::RadioButton("自由選択", cropMethod_ == 0)) {
                ExitCropFixed();
            }
            if (ImGui::RadioButton("サイズ指定", cropMethod_ == 1)) {
                EnterCropFixed();
            }
            if (ImGui::RadioButton("画面比率", cropMethod_ == 2)) {
                EnterCropAspect();
            }
            ImGui::EndDisabled();

            // W/H入力はCropFixedのみ。方式切替の同フレーム内で構成が食い違わないよう、
            // 切替前の方式(frameCropMethod)で描画する。
            if (mode_ == Mode::CropFixed && frameCropMethod == 2) {
                // 画面比率: 比率コンボ＋幅入力。高さは比率から算出する表示のみ。
                const double cx = cropFixedLeft_ + cropFixedW_ / 2.0;
                const double cy = cropFixedTop_ + cropFixedH_ / 2.0;
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::BeginCombo("比率", kCropAspects[cropAspectIdx_].label)) {
                    for (int i = 0; i < kCropAspectCount; ++i) {
                        if (ImGui::Selectable(kCropAspects[i].label, i == cropAspectIdx_)) {
                            cropAspectIdx_ = i;
                            SetCropAspectWidth(cropFixedW_, cx, cy);
                        }
                    }
                    ImGui::EndCombo();
                }
                const int oldW = cropFixedW_;
                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputInt("幅(px)", &cropFixedW_);
                if (cropFixedW_ != oldW) {
                    SetCropAspectWidth(cropFixedW_, cx, cy);
                }
                ImGui::Text("高さ: %d px", cropFixedH_);
            } else if (mode_ == Mode::CropFixed) {
                const int oldW = cropFixedW_;
                const int oldH = cropFixedH_;
                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputInt("幅(px)", &cropFixedW_);
                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputInt("高さ(px)", &cropFixedH_);
                if (cropFixedW_ != oldW || cropFixedH_ != oldH) {
                    // 画像サイズ超過・0以下を丸め、枠の中心を維持したまま画像内に収める。
                    const double cx = cropFixedLeft_ + oldW / 2.0;
                    const double cy = cropFixedTop_ + oldH / 2.0;
                    const int w = std::clamp(cropFixedW_, 1, document_->width);
                    const int h = std::clamp(cropFixedH_, 1, document_->height);
                    const auto rect = image_ops::ClampFixedRect(
                        document_->width, document_->height, w, h,
                        static_cast<int>(std::round(cx - w / 2.0)), static_cast<int>(std::round(cy - h / 2.0)));
                    cropFixedW_ = rect.w;
                    cropFixedH_ = rect.h;
                    cropFixedLeft_ = rect.left;
                    cropFixedTop_ = rect.top;
                }
            }
        }

        // アルファ警告テキスト（常時表示条件）
        if (document_->HasAlpha()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "JPEGで保存すると透明部分は白背景になります");
        }
    }

    // 編集操作：mode_ != Idleのときのみ表示
    if (mode_ != Mode::Idle) {
        ImGui::Separator();
        const bool applyEnabled =
            (mode_ == Mode::CropPreview) || (mode_ == Mode::CropFixed) || (mode_ == Mode::Mosaic && mosaicMaskDirty_);
        ImGui::BeginDisabled(!applyEnabled);
        if (ImGui::Button("適用", fullWidth)) {
            OnApplyClicked();
        }
        ImGui::EndDisabled();
        const bool undoStrokeEnabled =
            mode_ == Mode::Mosaic && !mosaicStrokeActive_ && !mosaicMaskHistory_.empty();
        ImGui::BeginDisabled(!undoStrokeEnabled);
        if (ImGui::Button("1手戻す", fullWidth)) {
            OnMosaicUndoStrokeClicked();
        }
        ImGui::EndDisabled();
        if (ImGui::Button("キャンセル", fullWidth)) {
            OnCancelEditClicked();
        }
    }

    // 画像操作：常に同じ3ボタン。編集中はグレーアウトのみ。
    ImGui::Separator();
    ImGui::BeginDisabled(!document_.has_value() || mode_ != Mode::Idle);
    if (ImGui::Button("保存...", fullWidth)) {
        OnSaveClicked();
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(mode_ != Mode::Idle || !previousDocument_.has_value());
    if (ImGui::Button("1つ前に戻す", fullWidth)) {
        OnUndoClicked();
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(mode_ != Mode::Idle || appliedEditCount_ <= 0);
    if (ImGui::Button("最初の画像に戻す", fullWidth)) {
        OnRevertToOriginalClicked();
    }
    ImGui::EndDisabled();

    // ステータスメッセージ（長文はパネル内で折り返す）
    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                            "%s", statusMessage_.c_str());
    }

    // 項目ボタンの処理は、このフレームの描画が済んでから行う。
    if (cropClicked) {
        OnCropClicked();
    } else if (mosaicClicked) {
        OnMosaicClicked();
    }

    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    ImGui::End();
}
