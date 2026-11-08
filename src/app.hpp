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
        Idle,        // 項目選択画面（トリミング/モザイクのボタンを表示。画像上のドラッグは無効）
        CropFree,    // 自由選択トリミングの待機（ここでのみドラッグで範囲選択を開始できる）
        Cropping,    // ドラッグ中〜矩形確定前
        CropPreview, // ドラッグ確定後、保存/戻る待ち
        CropFixed,   // サイズ指定/画面比率トリミング（固定サイズ枠を移動、適用/キャンセル待ち）
        Mosaic,      // モザイクモード（ストロークで塗り、適用/キャンセル待ち）
    };

    void OnOpenClicked();
    void OnApplyClicked();
    void OnCancelEditClicked();
    void OnSaveClicked();
    void OnUndoClicked();
    void OnRevertToOriginalClicked();
    void OnMosaicClicked();
    // 項目選択画面の「トリミング」押下。自由選択の待機（CropFree）に入る。
    void OnCropClicked();
    // 編集系の状態（mode_・プレビュー・トリミング枠・モザイクマスク・表示キャッシュ）を
    // すべて初期化してIdleに戻す。適用/キャンセル/画像を開く/モード突入から呼ぶ。
    void ResetEditUiState();
    // 方式セレクタで「サイズ指定」が選ばれたとき、固定サイズ枠を画像中央に置いて入る。
    void EnterCropFixed();
    // 方式セレクタで「画面比率」が選ばれたとき、比率の枠に入る（CropFreeまたはCropFixedから）。
    void EnterCropAspect();
    // 画面比率モードで幅wの枠に更新する（高さは比率から算出）。(cx, cy)を中心に維持して画像内に収める。
    void SetCropAspectWidth(int w, double cx, double cy);
    // 方式セレクタで「自由選択」に戻されたとき、サイズ指定/画面比率を抜けてCropFreeに戻る。
    void ExitCropFixed();
    // 直前の1ストロークをmosaicMaskHistory_から復元して取り消す。
    void OnMosaicUndoStrokeClicked();
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

    int cropMethod_ = 0;         // 0=自由選択, 1=サイズ指定, 2=画面比率
    int cropAspectIdx_ = 1;      // 画面比率モードで選択中の比率（kCropAspects のindex。初期4:3）
    // サイズ指定トリミングの枠（画像px）。W/Hは次回突入時の初期値としても使う（0=未設定）。
    int cropFixedW_ = 0;
    int cropFixedH_ = 0;
    int cropFixedLeft_ = 0;
    int cropFixedTop_ = 0;
    bool cropFixedDragging_ = false;
    ImVec2 cropFixedGrabOffset_{};  // ドラッグ開始時の「マウス位置 - 枠左上」（画像px）

    std::vector<unsigned char> mosaicMask_;  // document_と同サイズ。Mosaic中のみ有効
    // モザイクの各ストローク開始直前のmosaicMask_スナップショット（上限なし）。
    // 空＝モード突入時から1ストロークも確定/保持していない状態。
    std::vector<std::vector<unsigned char>> mosaicMaskHistory_;
    bool mosaicStrokeActive_ = false;        // ドラッグ中フラグ
    bool mosaicMaskDirty_ = false;           // 1ストローク以上塗られたか
    ImVec2 mosaicLastImagePx_{};             // ストローク補間用の前フレーム位置
    int mosaicBlockSize_ = 8;
    int mosaicBrushDiameter_ = 32;
    // アクティブなストロークの軌跡（画像ピクセル座標）。ドラッグ中の視覚フィードバック
    // （DrawMosaicOverlay）のみに使う。マスク自体はmosaicMask_で管理する。
    std::vector<ImVec2> mosaicStrokePoints_;
};
