#pragma once

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "gl_texture.hpp"
#include "image_document.hpp"

// ImGui UI構築と状態管理。
class App {
public:
    void OnFrame();

private:
    enum class Mode {
        Idle,        // 通常表示
        Cropping,    // ドラッグ中〜矩形確定前
        CropPreview, // ドラッグ確定後、保存/戻る待ち
        Mosaic,      // モザイクモード（ストロークで塗り、適用/キャンセル待ち）
    };

    void OnOpenClicked();
    void OnApplyClicked();
    void OnCancelEditClicked();
    void OnSaveClicked();
    void OnUndoClicked();
    void OnRevertToOriginalClicked();
    void OnMosaicClicked();
    // モード共通の「適用」処理。previousDocument_へ現状のdocument_を退避したうえで、
    // editedをstd::moveでdocument_へ反映する（コピーを発生させない）。
    void ApplyEditedDocument(ImageDocument&& edited);
    // 表示位置・サイズのキャッシュ（lastImageScreenPos_/lastDisplaySize_）と
    // ドラッグ座標を無効化する。画像切り替え時やクロップ操作終了時に呼び出す。
    void ResetImageDisplayCache();
    // マウス入力を処理しmode_/ドラッグ座標を更新する。座標変換には前フレームで
    // キャッシュした表示位置・サイズ（lastImageScreenPos_/lastDisplaySize_）を使うため、
    // このフレームのレイアウト計算（footer高さ見積もり等）より前に呼び出せる。
    void UpdateCropInputState();
    // マウス入力を処理しmosaicMask_を更新する（UpdateCropInputStateと同形）。
    // ドラッグ中はマスクとオーバーレイ表示のみを更新し、離した時点で1回だけ
    // document_からpreviewDocument_を作り直してApplyMosaicを実行する。
    void UpdateMosaicInputState();
    // 選択範囲のオーバーレイ描画とホバー領域(InvisibleButton)の配置を行う。
    // このフレームで確定したimageScreenPos/displaySizeを使う。
    void DrawCropOverlay(const ImVec2& imageScreenPos, const ImVec2& displaySize);
    // モザイクのブラシ軌跡・カーソルのオーバーレイ描画とホバー領域の配置を行う。
    void DrawMosaicOverlay(const ImVec2& imageScreenPos, const ImVec2& displaySize);
    // previewDocument_をdocument_から作り直し、mosaicMask_にApplyMosaicを適用して
    // previewTexture_へアップロードする（ストローク確定時・ブロックサイズ変更時に使用）。
    void RecomputeMosaicPreview();

    std::optional<ImageDocument> originalDocument_;  // 読み込み直後の画像（以後不変）
    std::optional<ImageDocument> document_;          // 作業中の画像
    std::optional<ImageDocument> previousDocument_;  // 直前の適用前スナップショット（1段のみ）
    int appliedEditCount_ = 0;                       // 適用済み編集回数（0なら元画像と同一）
    GLTexture texture_;
    std::optional<ImageDocument> previewDocument_;
    GLTexture previewTexture_;
    std::wstring selectedPath_;

    int jpegQuality_ = 90;
    std::string statusMessage_;
    bool statusIsError_ = false;

    Mode mode_ = Mode::Idle;
    ImVec2 dragStartImagePx_{};
    ImVec2 dragCurrentImagePx_{};
    // 前フレームでImGui::Image描画に使った表示位置・サイズ。
    // UpdateCropInputStateでの画面座標→画像ピクセル座標変換の基準に使う。
    ImVec2 lastImageScreenPos_{};
    ImVec2 lastDisplaySize_{};
    int selRectLeft_ = 0;
    int selRectTop_ = 0;
    int selRectRight_ = 0;
    int selRectBottom_ = 0;

    std::vector<unsigned char> mosaicMask_;  // document_と同サイズ。Mosaic中のみ有効
    bool mosaicStrokeActive_ = false;        // ドラッグ中フラグ
    bool mosaicMaskDirty_ = false;           // 1ストローク以上塗られたか
    ImVec2 mosaicLastImagePx_{};             // ストローク補間用の前フレーム位置
    int mosaicBlockSize_ = 8;
    int mosaicBrushDiameter_ = 32;
    // アクティブなストロークの軌跡（画像ピクセル座標）。ドラッグ中の視覚フィードバック
    // （DrawMosaicOverlay）のみに使う。マスク自体はmosaicMask_で管理する。
    std::vector<ImVec2> mosaicStrokePoints_;
};
