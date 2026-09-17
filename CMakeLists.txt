cmake_minimum_required(VERSION 3.20)
project(sdo_edit_pic CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SHOW_CONSOLE "Show console window for debugging" OFF)
# OFFにすると、GLFW/ImGuiのFetchContentとsdo_edit_pic本体（GUI・Win32依存）を
# 一切構成しない。WSL上でimage_ops_testsのみをネイティブLinuxビルドする際に使う
# （例: cmake -S . -B build-test -DSDO_BUILD_APP=OFF）。
option(SDO_BUILD_APP "Build the GUI application (requires GLFW/ImGui and Win32 libs)" ON)

if(SDO_BUILD_APP)
  include(FetchContent)

  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
  )
  FetchContent_MakeAvailable(glfw)

  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9
  )
  FetchContent_MakeAvailable(imgui)

  add_library(glad STATIC third_party/glad/src/gl.c)
  target_include_directories(glad PUBLIC third_party/glad/include)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )
  target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
  )
  # imgui_impl_opengl3.cppはIMGUI_IMPL_OPENGL_LOADER_CUSTOM定義時、GLヘッダのincludeを
  # 自前で行う前提のため、glad/gl.hを強制インクルードする。
  set_source_files_properties(
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    PROPERTIES COMPILE_OPTIONS "-include;glad/gl.h"
  )
  target_link_libraries(imgui PUBLIC glfw glad)

  if(SHOW_CONSOLE)
    set(APP_WIN32_FLAG "")
  else()
    set(APP_WIN32_FLAG WIN32)
  endif()

  set(APP_RC_SOURCES "")
  if(MINGW)
    set(APP_RC_SOURCES resources/app.rc)
  endif()

  add_executable(sdo_edit_pic ${APP_WIN32_FLAG}
    src/main.cpp
    src/app.cpp
    src/app_paths.cpp
    src/image_document.cpp
    src/image_io.cpp
    src/image_codec.cpp
    src/stb_impl.cpp
    src/image_ops.cpp
    src/gl_texture.cpp
    src/file_dialog.cpp
    ${APP_RC_SOURCES}
  )

  set_target_properties(sdo_edit_pic PROPERTIES OUTPUT_NAME "画像編集")

  target_include_directories(sdo_edit_pic PRIVATE
    third_party/stb
  )

  target_link_libraries(sdo_edit_pic PRIVATE
    imgui
    glad
    glfw
    opengl32
    comdlg32
    shell32
    ole32
  )

  if(MINGW)
    target_link_options(sdo_edit_pic PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

# GUI・Windows API非依存の純粋関数（image_ops等）のみを対象にした最小テスト。
# WSL上のネイティブLinuxビルドで実行できるよう、GUIライブラリや-mingw依存物とは
# リンクしない独立の実行ファイルとする。
enable_testing()

add_executable(image_ops_tests
  src/image_document.cpp
  src/image_ops.cpp
  tests/image_ops_tests.cpp
)
target_include_directories(image_ops_tests PRIVATE src)

add_test(NAME image_ops_tests COMMAND image_ops_tests)

# image_codec（JPEGエンコード/デコード、Win32非依存）の自動テスト。
# stb実装本体（stb_impl.cpp）とimage_codec.cpp、image_document.cppのみをリンクし、
# WSL上のネイティブLinuxビルドで実行できる。
add_executable(image_codec_tests
  src/image_document.cpp
  src/image_codec.cpp
  src/stb_impl.cpp
  tests/image_codec_tests.cpp
)
target_include_directories(image_codec_tests PRIVATE src third_party/stb)

add_test(NAME image_codec_tests COMMAND image_codec_tests)
