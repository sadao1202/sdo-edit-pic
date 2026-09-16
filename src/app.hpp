#pragma once

#include <optional>
#include <string>

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
    };

    void OnOpenClicked();
    void OnConvertClicked();
    void OnCropSaveClicked();
    void OnCropBackClicked();
    // 表示位置・サイズのキャッシュ（lastImageScreenPos_/lastDisplaySize_）と
    // ドラッグ座標を無効化する。画像切り替え時やクロップ操作終了時に呼び出す。
    void ResetImageDisplayCache();
    // マウス入力を処理しmode_/ドラッグ座標を更新する。座標変換には前フレームで
    // キャッシュした表示位置・サイズ（lastImageScreenPos_/lastDisplaySize_）を使うため、
    // このフレームのレイアウト計算（footer高さ見積もり等）より前に呼び出せる。
    void UpdateCropInputState();
    // 選択範囲のオーバーレイ描画とホバー領域(InvisibleButton)の配置を行う。
    // このフレームで確定したimageScreenPos/displaySizeを使う。
    void DrawCropOverlay(const ImVec2& imageScreenPos, const ImVec2& displaySize);

    std::optional<ImageDocument> document_;
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
    bool hasPendingSelection_ = false;
    int selRectLeft_ = 0;
    int selRectTop_ = 0;
    int selRectRight_ = 0;
    int selRectBottom_ = 0;
};
