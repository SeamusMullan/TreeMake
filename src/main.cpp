#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ─── Data types ──────────────────────────────────────────────

enum class PresetKind { Configure, Build, Test, Package, Workflow };

static const char* KindLabel(PresetKind k) {
    switch (k) {
        case PresetKind::Configure: return "Configure Presets";
        case PresetKind::Build:     return "Build Presets";
        case PresetKind::Test:      return "Test Presets";
        case PresetKind::Package:   return "Package Presets";
        case PresetKind::Workflow:  return "Workflow Presets";
    }
    return "?";
}

struct Preset {
    std::string              name;
    std::string              displayName;
    PresetKind               kind;
    bool                     hidden = false;
    std::vector<std::string> inherits;

    // Raw fields straight from JSON
    std::string generator;
    std::string binaryDir;
    std::string installDir;
    std::string toolchainFile;
    std::string configurePreset;   // for build/test presets
    std::string configuration;     // for build presets
    std::map<std::string, std::string> cacheVariables;
    json        rawJson;           // keep full JSON for anything we don't model
};

// ─── Application state ──────────────────────────────────────

struct AppState {
    std::string                          filePath;
    std::string                          errorMsg;
    int                                  version = 0;
    std::map<std::string, Preset>        presets;           // name -> preset
    std::map<PresetKind, std::vector<std::string>> tree;    // kind -> ordered names
    std::string                          selected;          // currently selected preset name
    std::string                          resolvedText;      // cached resolved text
    char                                 pathBuf[1024] = {};
    bool                                 showHidden = false;
};

// ─── Parsing helpers ─────────────────────────────────────────

static std::vector<std::string> GetInherits(const json& j) {
    std::vector<std::string> out;
    if (!j.contains("inherits")) return out;
    auto& v = j["inherits"];
    if (v.is_string()) {
        out.push_back(v.get<std::string>());
    } else if (v.is_array()) {
        for (auto& e : v) out.push_back(e.get<std::string>());
    }
    return out;
}

static std::map<std::string, std::string> GetCacheVars(const json& j) {
    std::map<std::string, std::string> out;
    if (!j.contains("cacheVariables") || !j["cacheVariables"].is_object()) return out;
    for (auto& [k, v] : j["cacheVariables"].items()) {
        if (v.is_string()) {
            out[k] = v.get<std::string>();
        } else if (v.is_object() && v.contains("value")) {
            // { "type": "BOOL", "value": "ON" } form
            std::string val = v["value"].is_string() ? v["value"].get<std::string>()
                                                      : v["value"].dump();
            if (v.contains("type"))
                val += "  [" + v["type"].get<std::string>() + "]";
            out[k] = val;
        } else {
            out[k] = v.dump();
        }
    }
    return out;
}

static std::string JStr(const json& j, const char* key) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return {};
}

static void ParsePresets(const json& arr, PresetKind kind, AppState& st) {
    for (auto& p : arr) {
        Preset pr;
        pr.name          = JStr(p, "name");
        pr.displayName   = JStr(p, "displayName");
        pr.kind          = kind;
        pr.hidden        = p.value("hidden", false);
        pr.inherits      = GetInherits(p);
        pr.generator     = JStr(p, "generator");
        pr.binaryDir     = JStr(p, "binaryDir");
        pr.installDir    = JStr(p, "installDir");
        pr.toolchainFile = JStr(p, "toolchainFile");
        pr.configurePreset = JStr(p, "configurePreset");
        pr.configuration = JStr(p, "configuration");
        pr.cacheVariables = GetCacheVars(p);
        pr.rawJson       = p;

        if (pr.name.empty()) continue;
        st.presets[pr.name] = std::move(pr);
        st.tree[kind].push_back(st.presets.rbegin()->second.name);
    }
}

static bool LoadFile(const std::string& path, AppState& st) {
    st.presets.clear();
    st.tree.clear();
    st.selected.clear();
    st.resolvedText.clear();
    st.errorMsg.clear();

    if (!fs::exists(path)) {
        st.errorMsg = "File not found: " + path;
        return false;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        st.errorMsg = "Cannot open: " + path;
        return false;
    }

    json root;
    try {
        root = json::parse(f, nullptr, true, true); // allow comments
    } catch (const json::parse_error& e) {
        st.errorMsg = std::string("JSON parse error: ") + e.what();
        return false;
    }

    st.version  = root.value("version", 0);
    st.filePath = path;

    if (root.contains("configurePresets"))  ParsePresets(root["configurePresets"],  PresetKind::Configure, st);
    if (root.contains("buildPresets"))      ParsePresets(root["buildPresets"],      PresetKind::Build,     st);
    if (root.contains("testPresets"))       ParsePresets(root["testPresets"],       PresetKind::Test,      st);
    if (root.contains("packagePresets"))    ParsePresets(root["packagePresets"],    PresetKind::Package,   st);
    if (root.contains("workflowPresets"))   ParsePresets(root["workflowPresets"],   PresetKind::Workflow,  st);

    // Also try merging CMakeUserPresets.json if it lives next to the file
    auto userPath = fs::path(path).parent_path() / "CMakeUserPresets.json";
    if (fs::exists(userPath)) {
        std::ifstream uf(userPath);
        try {
            json uroot = json::parse(uf, nullptr, true, true);
            if (uroot.contains("configurePresets"))  ParsePresets(uroot["configurePresets"],  PresetKind::Configure, st);
            if (uroot.contains("buildPresets"))      ParsePresets(uroot["buildPresets"],      PresetKind::Build,     st);
            if (uroot.contains("testPresets"))       ParsePresets(uroot["testPresets"],       PresetKind::Test,      st);
            if (uroot.contains("packagePresets"))    ParsePresets(uroot["packagePresets"],    PresetKind::Package,   st);
            if (uroot.contains("workflowPresets"))   ParsePresets(uroot["workflowPresets"],   PresetKind::Workflow,  st);
        } catch (...) {
            // user presets optional — silently skip on error
        }
    }

    return true;
}

// ─── Preset resolution (flatten inherits) ────────────────────

struct ResolvedPreset {
    std::string generator;
    std::string binaryDir;
    std::string installDir;
    std::string toolchainFile;
    std::string configurePreset;
    std::string configuration;
    std::map<std::string, std::string> cacheVariables;
};

static ResolvedPreset Resolve(const std::string& name, const AppState& st,
                               std::set<std::string>& visited) {
    ResolvedPreset r;
    auto it = st.presets.find(name);
    if (it == st.presets.end() || visited.count(name)) return r;
    visited.insert(name);

    const Preset& p = it->second;

    // Resolve parents first (rightmost parent has lowest priority)
    for (auto pit = p.inherits.rbegin(); pit != p.inherits.rend(); ++pit) {
        ResolvedPreset parent = Resolve(*pit, st, visited);
        // Merge: parent values are defaults, child overrides
        if (r.generator.empty())        r.generator        = parent.generator;
        if (r.binaryDir.empty())        r.binaryDir        = parent.binaryDir;
        if (r.installDir.empty())       r.installDir       = parent.installDir;
        if (r.toolchainFile.empty())    r.toolchainFile    = parent.toolchainFile;
        if (r.configurePreset.empty())  r.configurePreset  = parent.configurePreset;
        if (r.configuration.empty())    r.configuration    = parent.configuration;
        for (auto& [k, v] : parent.cacheVariables) {
            if (!r.cacheVariables.count(k)) r.cacheVariables[k] = v;
        }
    }

    // Now apply this preset's own values (child overrides parent)
    if (!p.generator.empty())       r.generator       = p.generator;
    if (!p.binaryDir.empty())       r.binaryDir       = p.binaryDir;
    if (!p.installDir.empty())      r.installDir      = p.installDir;
    if (!p.toolchainFile.empty())   r.toolchainFile   = p.toolchainFile;
    if (!p.configurePreset.empty()) r.configurePreset = p.configurePreset;
    if (!p.configuration.empty())   r.configuration   = p.configuration;
    for (auto& [k, v] : p.cacheVariables) {
        r.cacheVariables[k] = v; // child wins
    }

    return r;
}

static std::string BuildResolvedText(const std::string& name, const AppState& st) {
    auto it = st.presets.find(name);
    if (it == st.presets.end()) return "Preset not found.";
    const Preset& p = it->second;

    std::set<std::string> visited;
    ResolvedPreset r = Resolve(name, st, visited);

    std::string out;
    out += "# Preset: " + name + "\n";
    if (!p.displayName.empty()) out += "  Display Name : " + p.displayName + "\n";
    out += "  Kind         : " + std::string(KindLabel(p.kind)) + "\n";
    out += "  Hidden       : " + std::string(p.hidden ? "true" : "false") + "\n";
    if (!p.inherits.empty()) {
        out += "  Inherits     : ";
        for (size_t i = 0; i < p.inherits.size(); ++i) {
            if (i) out += ", ";
            out += p.inherits[i];
        }
        out += "\n";
    }
    out += "\n";

    out += "## Resolved Fields\n";
    if (!r.generator.empty())       out += "  generator        : " + r.generator       + "\n";
    if (!r.binaryDir.empty())       out += "  binaryDir        : " + r.binaryDir       + "\n";
    if (!r.installDir.empty())      out += "  installDir       : " + r.installDir      + "\n";
    if (!r.toolchainFile.empty())   out += "  toolchainFile    : " + r.toolchainFile   + "\n";
    if (!r.configurePreset.empty()) out += "  configurePreset  : " + r.configurePreset + "\n";
    if (!r.configuration.empty())   out += "  configuration    : " + r.configuration   + "\n";

    if (!r.cacheVariables.empty()) {
        out += "\n## Resolved Cache Variables  (child overrides parent)\n";
        // Find longest key for alignment
        size_t maxLen = 0;
        for (auto& [k, v] : r.cacheVariables)
            maxLen = std::max(maxLen, k.size());

        for (auto& [k, v] : r.cacheVariables) {
            out += "  -D" + k;
            out += std::string(maxLen - k.size() + 1, ' ');
            out += "= " + v + "\n";
        }
    }

    // Also dump the equivalent cmake command-line flags
    out += "\n## Equivalent cmake flags\n  cmake";
    if (!r.generator.empty())     out += " -G \"" + r.generator + "\"";
    if (!r.toolchainFile.empty()) out += " -DCMAKE_TOOLCHAIN_FILE=\"" + r.toolchainFile + "\"";
    for (auto& [k, v] : r.cacheVariables)
        out += " -D" + k + "=\"" + v + "\"";
    if (!r.binaryDir.empty())     out += " -B \"" + r.binaryDir + "\"";
    out += " .\n";

    return out;
}

// ─── GUI ─────────────────────────────────────────────────────

static void DrawUI(AppState& st) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("CMake Preset Viewer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ── Top bar: file path ──
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    bool enter = ImGui::InputTextWithHint("##path", "Path to CMakePresets.json",
                                           st.pathBuf, sizeof(st.pathBuf),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load", {70, 0}) || enter) {
        LoadFile(st.pathBuf, st);
    }

    if (!st.errorMsg.empty()) {
        ImGui::TextColored({1, 0.3f, 0.3f, 1}, "%s", st.errorMsg.c_str());
    }
    if (!st.filePath.empty()) {
        ImGui::TextDisabled("Loaded: %s  (version %d, %zu presets)",
                            st.filePath.c_str(), st.version, st.presets.size());
    }

    ImGui::Checkbox("Show hidden presets", &st.showHidden);
    ImGui::Separator();

    // ── Two-panel layout ──
    float panelW = ImGui::GetContentRegionAvail().x * 0.30f;
    ImGui::BeginChild("tree_panel", {panelW, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    for (auto kind : {PresetKind::Configure, PresetKind::Build,
                      PresetKind::Test, PresetKind::Package, PresetKind::Workflow}) {
        auto it = st.tree.find(kind);
        if (it == st.tree.end() || it->second.empty()) continue;

        if (ImGui::TreeNodeEx(KindLabel(kind), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& name : it->second) {
                auto& p = st.presets[name];
                if (p.hidden && !st.showHidden) continue;

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (name == st.selected) flags |= ImGuiTreeNodeFlags_Selected;

                if (p.hidden) ImGui::PushStyleColor(ImGuiCol_Text, {0.5f, 0.5f, 0.5f, 1.0f});

                std::string label = p.displayName.empty() ? name : p.displayName + "  (" + name + ")";
                ImGui::TreeNodeEx(label.c_str(), flags);

                if (ImGui::IsItemClicked()) {
                    st.selected     = name;
                    st.resolvedText = BuildResolvedText(name, st);
                }

                if (p.hidden) ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ── Detail panel ──
    ImGui::BeginChild("detail_panel", {0, 0}, ImGuiChildFlags_Borders);

    if (st.selected.empty()) {
        ImGui::TextDisabled("Select a preset from the tree.");
    } else {
        if (ImGui::Button("Copy to clipboard")) {
            ImGui::SetClipboardText(st.resolvedText.c_str());
        }
        ImGui::Separator();
        ImGui::TextUnformatted(st.resolvedText.c_str(),
                               st.resolvedText.c_str() + st.resolvedText.size());
    }

    ImGui::EndChild();
    ImGui::End();
}

// ─── Main ────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "CMake Preset Viewer", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Slightly nicer style tweaks
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding  = 4.0f;
    style.GrabRounding   = 4.0f;
    style.WindowRounding = 0.0f;
    style.FramePadding   = {8, 4};
    style.ItemSpacing    = {8, 6};

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    AppState st;

    // If a path was given on the command line, load it immediately
    if (argc > 1) {
        snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", argv[1]);
        LoadFile(argv[1], st);
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawUI(st);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
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
