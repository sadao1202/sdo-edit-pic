# sdo-edit-pic

C/C++によるWindows向け画像編集ソフト。速度と使いやすさを重視する。

## 基本設計

- 言語: C++17
- GUI: Dear ImGui + GLFW + OpenGL3バックエンド
  - Win32+DirectX11も検討したが、MinGWクロスビルドのしやすさとWSL上でのネイティブLinuxビルドによる開発イテレーション速度を優先しGLFW+OpenGL3を採用
- OpenGLローダー: GLAD2（`third_party/glad/`にvendoring、`glad2`パッケージで生成: `python3 -m glad --api gl:core=3.3 --out-path third_party/glad c`）
- 画像デコード/エンコード: stb_image / stb_image_write（`third_party/stb/`にvendoring）
- Dear ImGui本体・GLFWはCMake `FetchContent`でバージョンタグ固定取得
- ファイル選択ダイアログ: Win32コモンダイアログ (`GetOpenFileNameW`/`GetSaveFileNameW`)
- ビルド: CMake + MinGW-w64クロスコンパイル (`x86_64-w64-mingw32-g++`)、静的リンクで単一exe化

## 開発環境

- 開発機: Windows 11 + WSL2 (Ubuntu 24.04)
- 必要ツール: `cmake`, `ninja-build`, `x86_64-w64-mingw32-g++` (mingw-w64)
- WSLからWindows exeを直接実行できないため、動作確認は`/mnt/c`経由でWindows側にコピーして行う

## ビルド

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

依存DLL確認: `x86_64-w64-mingw32-objdump -p build/sdo_edit_pic.exe | grep "DLL Name"`

## 前提・注意事項

- ファイルパスは日本語・空白を含む可能性があるため、`stbi_load`/`stbi_write_*`は直接パスを渡さず、`_wfopen`で開いた`FILE*`経由（`stbi_load_from_file`/`stbi_write_*_to_func`）で扱うこと
- `imgui_impl_opengl3.cpp`は`IMGUI_IMPL_OPENGL_LOADER_CUSTOM`定義時にGLヘッダをincludeしないため、CMake側で`-include glad/gl.h`を強制インクルードしている（`CMakeLists.txt`参照）
- GUI操作を伴うE2E試験はWSL上では実施できない。画像I/Oロジック等GUI非依存部分は分離してテスト可能な構造を保つこと
- アプリが書き込むファイル（`imgui.ini`、変換・トリミング後の画像）は exe と同じディレクトリではなく、`%APPDATA%\sdo-edit-pic\`（`src/app_paths.hpp`/`src/app_paths.cpp`で算出）に保存する

## 未解決の課題

- 現状ファイルダイアログのフィルタはpng/jpgをまとめた単一グループで、拡張子選択によるフォーマット指定はできない（拡張子明示入力のみ）
- image_io / gl_texture のロジックに対する自動テストは未整備
