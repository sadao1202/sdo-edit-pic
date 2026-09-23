#pragma once

#include <optional>
#include <string>

namespace file_dialog {

// キャンセル時はstd::nulloptを返す。
std::optional<std::wstring> OpenFileDialog();

// 保存先パスを選ばせる。
// initialDirectory: 初期ディレクトリ（空文字ならOS既定に委ねる）
// initialBaseName : 初期ファイル名（拡張子を含まないベース名）
// キャンセル時はstd::nulloptを返す。
// 戻り値のパスは、ユーザーが拡張子を入力しなかった場合に選択中フィルタに応じて
// .png / .jpg を補完済み。
std::optional<std::wstring> SaveFileDialog(const std::wstring& initialDirectory,
                                            const std::wstring& initialBaseName);

}  // namespace file_dialog
