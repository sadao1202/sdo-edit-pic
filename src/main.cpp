#include <glad/gl.h>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "app.hpp"

namespace {

// WIN32サブシステム(コンソールなし)でも初期化失敗をユーザーに通知するためのヘルパー。
void ReportFatalError(const wchar_t* message) {
#ifdef _WIN32
    MessageBoxW(nullptr, message, L"sdo-edit-pic", MB_OK | MB_ICONERROR);
#else
    (void)message;
#endif
}

#ifdef _WIN32
// OS標準搭載フォントを優先順に探し、日本語グリフ付きで読み込む。
// いずれも見つからない場合は何もせず、デフォルトフォントのまま続行する。
void LoadJapaneseFont(ImGuiIO& io) {
    static const wchar_t* kCandidates[] = {
        L"C:\\Windows\\Fonts\\YuGothM.ttc",
        L"C:\\Windows\\Fonts\\meiryo.ttc",
        L"C:\\Windows\\Fonts\\msgothic.ttc",
    };

    for (const wchar_t* candidate : kCandidates) {
        if (GetFileAttributesW(candidate) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }

        char utf8Path[MAX_PATH * 4] = {};
        WideCharToMultiByte(CP_UTF8, 0, candidate, -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);

        ImFontConfig config;
        io.Fonts->AddFontFromFileTTF(utf8Path, 20.0f, &config, io.Fonts->GetGlyphRangesJapanese());
        return;
    }
}
#endif

}  // namespace

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFWの初期化に失敗しました。\n");
        ReportFatalError(L"GLFWの初期化に失敗しました。");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(900, 650, "sdo-edit-pic", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "ウィンドウの作成に失敗しました。\n");
        ReportFatalError(L"ウィンドウの作成に失敗しました。");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "OpenGL関数のロードに失敗しました。\n");
        ReportFatalError(L"OpenGL関数のロードに失敗しました。");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

#ifdef _WIN32
    LoadJapaneseFont(io);
#endif

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.OnFrame();

        ImGui::Render();

        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
