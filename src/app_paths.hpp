#pragma once

#include <optional>
#include <string>

// アプリ専用データディレクトリ（%APPDATA%\sdo-edit-pic\）関連のパス取得。
// Windows専用（SHGetKnownFolderPathに依存）。
namespace app_paths {

// アプリ専用データディレクトリを返す（末尾に'\'を付与）。
// 初回呼び出し時にディレクトリを作成する（既に存在する場合は成功扱い）。
// 取得・作成に失敗した場合はstd::nullopt。
// 結果は関数ローカルstaticにキャッシュされ、以降の呼び出しではAPIを叩かない。
std::optional<std::wstring> GetDataDirectory();

// アプリ専用データディレクトリ内のimgui.iniのフルパスをUTF-8で返す。
// GetDataDirectory()が失敗した場合はstd::nullopt。
std::optional<std::string> GetImGuiIniPathUtf8();

}  // namespace app_paths
