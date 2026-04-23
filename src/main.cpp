#include "types.h"
#include "recent_files.h"
#include "preset_loader.h"
#include "ui_draw.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <nfd.h>

#include "icon_data.h"

#include <cstdio>

// Global pointer for GLFW callbacks
static AppState* g_appState = nullptr;

static void DropCallback(GLFWwindow*, int count, const char* paths[]) {
    if (count > 0 && g_appState) {
        if (fs::is_directory(paths[0]))
            g_appState->pendingDirLoad = paths[0];
        else
            g_appState->pendingLoad = paths[0];
    }
}

int main(int argc, char* argv[]) {
    if (!glfwInit()) { fprintf(stderr, "Failed to init GLFW\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1400, 800, "TreeMake", nullptr, nullptr);
    if (!window) { fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    GLFWimage icon;
    icon.width  = ICON_WIDTH;
    icon.height = ICON_HEIGHT;
    icon.pixels = (unsigned char*)ICON_RGBA;
    glfwSetWindowIcon(window, 1, &icon);

    NFD_Init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    AppState st;
    g_appState = &st;
    LoadSettings(st);
    ApplyUIScale(st);

    glfwSetDropCallback(window, DropCallback);

    LoadRecentFiles(st);

    if (argc > 1) {
        snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", argv[1]);
        if (fs::is_directory(argv[1]))
            LoadProjectDirectory(argv[1], st);
        else
            LoadFile(argv[1], st);
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!st.pendingLoad.empty()) {
            if (fs::is_directory(st.pendingLoad))
                LoadProjectDirectory(st.pendingLoad, st);
            else
                LoadFile(st.pendingLoad, st);
            st.pendingLoad.clear();
        }
        if (!st.pendingDirLoad.empty()) {
            LoadProjectDirectory(st.pendingDirLoad, st);
            st.pendingDirLoad.clear();
        }

        if (st.needFontRebuild)
            ApplyUIScale(st);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawUI(st);
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    NFD_Quit();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
    int argc = 1;
    char* argv[2] = { (char*)"treemake", nullptr };
    if (lpCmdLine && lpCmdLine[0]) { argv[1] = lpCmdLine; argc = 2; }
    return main(argc, argv);
}
#endif
