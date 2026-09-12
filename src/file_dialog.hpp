#pragma once

#include <optional>
#include <string>

namespace file_dialog {

// キャンセル時はstd::nulloptを返す。
std::optional<std::wstring> OpenFileDialog();
std::optional<std::wstring> SaveFileDialog();

}  // namespace file_dialog
