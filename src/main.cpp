#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <nfd.h>

#include "icon_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════
//  Data types
// ═══════════════════════════════════════════════════════════════

enum class PresetKind { Configure, Build, Test, Package, Workflow };

static const char* KindLabel(PresetKind k) {
    switch (k) {
        case PresetKind::Configure: return "Configure";
        case PresetKind::Build:     return "Build";
        case PresetKind::Test:      return "Test";
        case PresetKind::Package:   return "Package";
        case PresetKind::Workflow:  return "Workflow";
    }
    return "?";
}

static ImU32 KindColor(PresetKind k) {
    switch (k) {
        case PresetKind::Configure: return IM_COL32(100, 160, 255, 255);
        case PresetKind::Build:     return IM_COL32(100, 220, 130, 255);
        case PresetKind::Test:      return IM_COL32(240, 180,  80, 255);
        case PresetKind::Package:   return IM_COL32(220, 120, 220, 255);
        case PresetKind::Workflow:  return IM_COL32(220, 100, 100, 255);
    }
    return IM_COL32(180, 180, 180, 255);
}

struct Preset {
    std::string              name;
    std::string              displayName;
    PresetKind               kind;
    bool                     hidden = false;
    std::vector<std::string> inherits;
    std::string              sourceFile;

    std::string generator, binaryDir, installDir, toolchainFile;
    std::string configurePreset, configuration;
    std::map<std::string, std::string> cacheVariables;
    json        rawJson;
};

struct GraphNode {
    std::string name;
    ImVec2      pos  = {0, 0};
    ImVec2      size = {200, 40};
};

struct AppState {
    std::string                          filePath;
    std::string                          errorMsg;
    int                                  version = 0;
    std::map<std::string, Preset>        presets;
    std::map<PresetKind, std::vector<std::string>> tree;
    std::string                          selected;
    std::string                          resolvedText;
    char                                 pathBuf[1024] = {};
    bool                                 showHidden = false;
    int                                  activeTab = 0;

    // Graph
    std::map<std::string, GraphNode>     graphNodes;
    ImVec2                               graphScroll = {0, 0};
    float                                graphZoom = 1.0f;
    bool                                 graphLayoutDone = false;
    std::string                          graphDragging;

    // Diff
    int                                  diffLeft  = -1;
    int                                  diffRight = -1;
    std::string                          diffTextLeft;
    std::string                          diffTextRight;
    std::vector<std::string>             presetNames; // sorted list for combo boxes

    // Recent files
    std::vector<std::string>             recentFiles;

    // Pending file load (from drag-drop or dialog, deferred to main loop)
    std::string                          pendingLoad;
};

// Global pointer for GLFW callbacks
static AppState* g_appState = nullptr;

// ═══════════════════════════════════════════════════════════════
//  Recent files
// ═══════════════════════════════════════════════════════════════

static fs::path GetRecentFilePath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) return fs::path(appdata) / "TreeMake" / "recent.txt";
    return fs::path(".") / ".treemake_recent";
#else
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / ".config" / "treemake" / "recent.txt";
    return fs::path(".") / ".treemake_recent";
#endif
}

static void LoadRecentFiles(AppState& st) {
    st.recentFiles.clear();
    auto path = GetRecentFilePath();
    if (!fs::exists(path)) return;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && st.recentFiles.size() < 15)
            st.recentFiles.push_back(line);
    }
}

static void SaveRecentFiles(const AppState& st) {
    auto path = GetRecentFilePath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    for (auto& p : st.recentFiles) f << p << "\n";
}

static void AddToRecent(AppState& st, const std::string& filePath) {
    std::error_code ec;
    std::string canonical = fs::canonical(filePath, ec).string();
    if (ec) canonical = fs::absolute(filePath).string();

    // Remove if already present
    st.recentFiles.erase(
        std::remove(st.recentFiles.begin(), st.recentFiles.end(), canonical),
        st.recentFiles.end());
    // Insert at front
    st.recentFiles.insert(st.recentFiles.begin(), canonical);
    // Cap at 15
    if (st.recentFiles.size() > 15) st.recentFiles.resize(15);
    SaveRecentFiles(st);
}

// ═══════════════════════════════════════════════════════════════
//  JSON helpers
// ═══════════════════════════════════════════════════════════════

static std::vector<std::string> GetInherits(const json& j) {
    std::vector<std::string> out;
    if (!j.contains("inherits")) return out;
    auto& v = j["inherits"];
    if (v.is_string())     out.push_back(v.get<std::string>());
    else if (v.is_array()) for (auto& e : v) out.push_back(e.get<std::string>());
    return out;
}

static std::map<std::string, std::string> GetCacheVars(const json& j) {
    std::map<std::string, std::string> out;
    if (!j.contains("cacheVariables") || !j["cacheVariables"].is_object()) return out;
    for (auto& [k, v] : j["cacheVariables"].items()) {
        if (v.is_string()) {
            out[k] = v.get<std::string>();
        } else if (v.is_object() && v.contains("value")) {
            std::string val = v["value"].is_string() ? v["value"].get<std::string>() : v["value"].dump();
            if (v.contains("type")) val += "  [" + v["type"].get<std::string>() + "]";
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

// ═══════════════════════════════════════════════════════════════
//  File loading
// ═══════════════════════════════════════════════════════════════

static void ParsePresetsFromArray(const json& arr, PresetKind kind,
                                   const std::string& sourceFile, AppState& st) {
    for (auto& p : arr) {
        std::string name = JStr(p, "name");
        if (name.empty()) continue;

        Preset pr;
        pr.name            = name;
        pr.displayName     = JStr(p, "displayName");
        pr.kind            = kind;
        pr.hidden          = p.value("hidden", false);
        pr.inherits        = GetInherits(p);
        pr.generator       = JStr(p, "generator");
        pr.binaryDir       = JStr(p, "binaryDir");
        pr.installDir      = JStr(p, "installDir");
        pr.toolchainFile   = JStr(p, "toolchainFile");
        pr.configurePreset = JStr(p, "configurePreset");
        pr.configuration   = JStr(p, "configuration");
        pr.cacheVariables  = GetCacheVars(p);
        pr.rawJson         = p;
        pr.sourceFile      = sourceFile;

        auto& treeVec = st.tree[kind];
        treeVec.erase(std::remove(treeVec.begin(), treeVec.end(), name), treeVec.end());
        st.presets[name] = std::move(pr);
        treeVec.push_back(name);
    }
}

static void LoadPresetFile(const std::string& path, AppState& st,
                            std::set<std::string>& visitedFiles) {
    std::error_code ec;
    fs::path canonical = fs::canonical(path, ec);
    if (ec) canonical = fs::absolute(path);
    std::string key = canonical.string();

    if (visitedFiles.count(key)) return;
    visitedFiles.insert(key);

    if (!fs::exists(canonical)) {
        st.errorMsg += (st.errorMsg.empty() ? "" : "\n") + std::string("File not found: ") + path;
        return;
    }

    std::ifstream f(canonical);
    if (!f.is_open()) return;

    json root;
    try {
        root = json::parse(f, nullptr, true, true);
    } catch (const json::parse_error& e) {
        st.errorMsg += (st.errorMsg.empty() ? "" : "\n") + std::string("Parse error: ") + e.what();
        return;
    }

    if (st.version == 0) st.version = root.value("version", 0);

    if (root.contains("include") && root["include"].is_array()) {
        fs::path parentDir = canonical.parent_path();
        for (auto& inc : root["include"]) {
            if (!inc.is_string()) continue;
            LoadPresetFile((parentDir / inc.get<std::string>()).string(), st, visitedFiles);
        }
    }

    std::string shortName = canonical.filename().string();
    if (root.contains("configurePresets")) ParsePresetsFromArray(root["configurePresets"], PresetKind::Configure, shortName, st);
    if (root.contains("buildPresets"))     ParsePresetsFromArray(root["buildPresets"],     PresetKind::Build,     shortName, st);
    if (root.contains("testPresets"))      ParsePresetsFromArray(root["testPresets"],      PresetKind::Test,      shortName, st);
    if (root.contains("packagePresets"))   ParsePresetsFromArray(root["packagePresets"],   PresetKind::Package,   shortName, st);
    if (root.contains("workflowPresets"))  ParsePresetsFromArray(root["workflowPresets"],  PresetKind::Workflow,  shortName, st);
}

static bool LoadFile(const std::string& path, AppState& st) {
    st.presets.clear();
    st.tree.clear();
    st.selected.clear();
    st.resolvedText.clear();
    st.errorMsg.clear();
    st.graphNodes.clear();
    st.graphLayoutDone = false;
    st.diffLeft = st.diffRight = -1;
    st.diffTextLeft.clear();
    st.diffTextRight.clear();
    st.presetNames.clear();

    if (!fs::exists(path)) { st.errorMsg = "File not found: " + path; return false; }

    std::set<std::string> visited;
    LoadPresetFile(path, st, visited);

    auto userPath = fs::path(path).parent_path() / "CMakeUserPresets.json";
    if (fs::exists(userPath))
        LoadPresetFile(userPath.string(), st, visited);

    st.filePath = path;
    snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", path.c_str());

    // Build sorted preset names list for diff combos
    for (auto& [name, _] : st.presets) st.presetNames.push_back(name);
    std::sort(st.presetNames.begin(), st.presetNames.end());

    AddToRecent(st, path);
    return st.errorMsg.empty();
}

// ═══════════════════════════════════════════════════════════════
//  Native file dialog
// ═══════════════════════════════════════════════════════════════

static void OpenFileDialog(AppState& st) {
    nfdu8char_t* outPath = nullptr;
    nfdu8filteritem_t filters[1] = { { "CMake Presets", "json" } };
    nfdopendialogu8args_t args = {};
    args.filterList = filters;
    args.filterCount = 1;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY && outPath) {
        st.pendingLoad = outPath;
        NFD_FreePathU8(outPath);
    }
}

// ═══════════════════════════════════════════════════════════════
//  GLFW drag-and-drop callback
// ═══════════════════════════════════════════════════════════════

static void DropCallback(GLFWwindow*, int count, const char* paths[]) {
    if (count > 0 && g_appState) {
        g_appState->pendingLoad = paths[0];
    }
}

// ═══════════════════════════════════════════════════════════════
//  Inheritance resolution
// ═══════════════════════════════════════════════════════════════

struct ResolvedPreset {
    std::string generator, binaryDir, installDir, toolchainFile, configurePreset, configuration;
    std::map<std::string, std::string> cacheVariables;
};

static void MergeDefaults(ResolvedPreset& dst, const ResolvedPreset& src) {
    if (dst.generator.empty())       dst.generator       = src.generator;
    if (dst.binaryDir.empty())       dst.binaryDir       = src.binaryDir;
    if (dst.installDir.empty())      dst.installDir      = src.installDir;
    if (dst.toolchainFile.empty())   dst.toolchainFile   = src.toolchainFile;
    if (dst.configurePreset.empty()) dst.configurePreset = src.configurePreset;
    if (dst.configuration.empty())   dst.configuration   = src.configuration;
    for (auto& [k, v] : src.cacheVariables)
        if (!dst.cacheVariables.count(k)) dst.cacheVariables[k] = v;
}

static ResolvedPreset Resolve(const std::string& name, const AppState& st,
                               std::set<std::string>& visited) {
    ResolvedPreset r;
    auto it = st.presets.find(name);
    if (it == st.presets.end() || visited.count(name)) return r;
    visited.insert(name);
    const Preset& p = it->second;

    r.generator       = p.generator;
    r.binaryDir       = p.binaryDir;
    r.installDir      = p.installDir;
    r.toolchainFile   = p.toolchainFile;
    r.configurePreset = p.configurePreset;
    r.configuration   = p.configuration;
    r.cacheVariables  = p.cacheVariables;

    for (auto& parentName : p.inherits) {
        ResolvedPreset parent = Resolve(parentName, st, visited);
        MergeDefaults(r, parent);
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
    out += "  Source       : " + p.sourceFile + "\n";
    if (!p.inherits.empty()) {
        out += "  Inherits     : ";
        for (size_t i = 0; i < p.inherits.size(); ++i) { if (i) out += ", "; out += p.inherits[i]; }
        out += "\n";
    }
    out += "\n## Resolved Fields\n";
    if (!r.generator.empty())       out += "  generator        : " + r.generator       + "\n";
    if (!r.binaryDir.empty())       out += "  binaryDir        : " + r.binaryDir       + "\n";
    if (!r.installDir.empty())      out += "  installDir       : " + r.installDir      + "\n";
    if (!r.toolchainFile.empty())   out += "  toolchainFile    : " + r.toolchainFile   + "\n";
    if (!r.configurePreset.empty()) out += "  configurePreset  : " + r.configurePreset + "\n";
    if (!r.configuration.empty())   out += "  configuration    : " + r.configuration   + "\n";

    if (!r.cacheVariables.empty()) {
        out += "\n## Resolved Cache Variables\n";
        size_t maxLen = 0;
        for (auto& [k, v] : r.cacheVariables) maxLen = std::max(maxLen, k.size());
        for (auto& [k, v] : r.cacheVariables)
            out += "  -D" + k + std::string(maxLen - k.size() + 1, ' ') + "= " + v + "\n";
    }

    out += "\n## Equivalent cmake flags\n  cmake";
    if (!r.generator.empty())     out += " -G \"" + r.generator + "\"";
    if (!r.toolchainFile.empty()) out += " -DCMAKE_TOOLCHAIN_FILE=\"" + r.toolchainFile + "\"";
    for (auto& [k, v] : r.cacheVariables) out += " -D" + k + "=\"" + v + "\"";
    if (!r.binaryDir.empty())     out += " -B \"" + r.binaryDir + "\"";
    out += " .\n";
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  Diff helpers
// ═══════════════════════════════════════════════════════════════

struct DiffLine {
    std::string text;
    int         status; // 0 = same, -1 = left only, +1 = right only, 2 = changed
};

static void BuildDiffLines(const std::string& leftName, const std::string& rightName,
                            const AppState& st,
                            std::vector<DiffLine>& leftLines,
                            std::vector<DiffLine>& rightLines) {
    leftLines.clear();
    rightLines.clear();

    auto litP = st.presets.find(leftName);
    auto ritP = st.presets.find(rightName);
    if (litP == st.presets.end() || ritP == st.presets.end()) return;

    std::set<std::string> lv, rv;
    ResolvedPreset lr = Resolve(leftName, st, lv);
    ResolvedPreset rr = Resolve(rightName, st, rv);

    // Collect all keys from both
    std::set<std::string> allKeys;
    for (auto& [k, _] : lr.cacheVariables) allKeys.insert(k);
    for (auto& [k, _] : rr.cacheVariables) allKeys.insert(k);

    // Header fields
    struct FieldPair { std::string label, leftVal, rightVal; };
    std::vector<FieldPair> fields;

    auto addField = [&](const char* label, const std::string& l, const std::string& r) {
        if (!l.empty() || !r.empty()) fields.push_back({label, l, r});
    };
    addField("Kind",           KindLabel(litP->second.kind), KindLabel(ritP->second.kind));
    addField("Hidden",         litP->second.hidden ? "true" : "false",
                                ritP->second.hidden ? "true" : "false");
    addField("Source",         litP->second.sourceFile, ritP->second.sourceFile);
    addField("generator",      lr.generator,       rr.generator);
    addField("binaryDir",      lr.binaryDir,       rr.binaryDir);
    addField("installDir",     lr.installDir,      rr.installDir);
    addField("toolchainFile",  lr.toolchainFile,   rr.toolchainFile);
    addField("configurePreset",lr.configurePreset, rr.configurePreset);
    addField("configuration",  lr.configuration,   rr.configuration);

    for (auto& fp : fields) {
        int status = (fp.leftVal == fp.rightVal) ? 0 : 2;
        leftLines.push_back({fp.label + ": " + fp.leftVal, status});
        rightLines.push_back({fp.label + ": " + fp.rightVal, status});
    }

    // Separator
    leftLines.push_back({"--- Cache Variables ---", 0});
    rightLines.push_back({"--- Cache Variables ---", 0});

    for (auto& k : allKeys) {
        bool inL = lr.cacheVariables.count(k) > 0;
        bool inR = rr.cacheVariables.count(k) > 0;
        std::string lval = inL ? lr.cacheVariables.at(k) : "";
        std::string rval = inR ? rr.cacheVariables.at(k) : "";

        std::string lineL = "-D" + k + " = " + lval;
        std::string lineR = "-D" + k + " = " + rval;

        if (inL && inR && lval == rval) {
            leftLines.push_back({lineL, 0});
            rightLines.push_back({lineR, 0});
        } else if (inL && inR) {
            leftLines.push_back({lineL, 2});
            rightLines.push_back({lineR, 2});
        } else if (inL && !inR) {
            leftLines.push_back({lineL, -1});
            rightLines.push_back({"", -1});
        } else {
            leftLines.push_back({"", 1});
            rightLines.push_back({lineR, 1});
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  Graph layout
// ═══════════════════════════════════════════════════════════════

static void BuildGraphLayout(AppState& st) {
    st.graphNodes.clear();
    if (st.presets.empty()) return;

    std::map<std::string, std::vector<std::string>> children;
    std::map<std::string, int> inDeg;
    for (auto& [name, p] : st.presets) {
        inDeg[name];
        for (auto& par : p.inherits) { children[par].push_back(name); inDeg[name]++; }
    }

    std::map<std::string, int> depth;
    std::queue<std::string> q;
    for (auto& [name, deg] : inDeg) {
        depth[name] = -1;
        if (deg == 0) { depth[name] = 0; q.push(name); }
    }
    int maxDepth = 0;
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        int d = depth[cur]; maxDepth = std::max(maxDepth, d);
        for (auto& ch : children[cur])
            if (depth[ch] < d + 1) { depth[ch] = d + 1; q.push(ch); }
    }
    for (auto& [name, d] : depth) if (d < 0) d = 0;

    std::map<int, std::vector<std::string>> layers;
    for (auto& [name, d] : depth) layers[d].push_back(name);
    for (auto& [d, names] : layers)
        std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
            auto& pa = st.presets[a]; auto& pb = st.presets[b];
            if (pa.kind != pb.kind) return (int)pa.kind < (int)pb.kind;
            return a < b;
        });

    const float nodeW = 220, nodeH = 44, layerGap = 90, nodeGap = 24;
    size_t maxWidth = 0;
    for (auto& [d, names] : layers) maxWidth = std::max(maxWidth, names.size());
    float totalMaxW = maxWidth * nodeW + (maxWidth > 0 ? (maxWidth - 1) * nodeGap : 0);

    for (auto& [d, names] : layers) {
        float totalW = names.size() * nodeW + (names.size() > 0 ? (names.size() - 1) * nodeGap : 0);
        float startX = (totalMaxW - totalW) * 0.5f + 60;
        float y = 60 + d * (nodeH + layerGap);
        for (size_t i = 0; i < names.size(); ++i) {
            GraphNode gn; gn.name = names[i];
            gn.pos = {startX + i * (nodeW + nodeGap), y};
            gn.size = {nodeW, nodeH};
            st.graphNodes[names[i]] = gn;
        }
    }
    st.graphLayoutDone = true;
}

// ═══════════════════════════════════════════════════════════════
//  GUI — Menu bar
// ═══════════════════════════════════════════════════════════════

static void DrawMenuBar(AppState& st) {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                OpenFileDialog(st);
            }
            if (ImGui::BeginMenu("Recent Files", !st.recentFiles.empty())) {
                for (size_t i = 0; i < st.recentFiles.size(); ++i) {
                    std::string label = st.recentFiles[i];
                    // Show just filename + parent for readability
                    fs::path p(label);
                    std::string shortLabel = p.parent_path().filename().string() + "/" + p.filename().string();
                    if (ImGui::MenuItem((shortLabel + "##recent" + std::to_string(i)).c_str())) {
                        st.pendingLoad = st.recentFiles[i];
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(label.c_str());
                        ImGui::EndTooltip();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear Recent")) {
                    st.recentFiles.clear();
                    SaveRecentFiles(st);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                // Will be handled by GLFW
                if (auto* win = glfwGetCurrentContext())
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

// ═══════════════════════════════════════════════════════════════
//  GUI — Presets tab
// ═══════════════════════════════════════════════════════════════

static void DrawPresetsTab(AppState& st) {
    float panelW = ImGui::GetContentRegionAvail().x * 0.30f;
    ImGui::BeginChild("tree_panel", {panelW, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    for (auto kind : {PresetKind::Configure, PresetKind::Build,
                      PresetKind::Test, PresetKind::Package, PresetKind::Workflow}) {
        auto it = st.tree.find(kind);
        if (it == st.tree.end() || it->second.empty()) continue;

        ImGui::PushStyleColor(ImGuiCol_Text, KindColor(kind));
        bool open = ImGui::TreeNodeEx(
            (std::string(KindLabel(kind)) + " Presets##cat" + std::to_string((int)kind)).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();

        if (open) {
            for (auto& name : it->second) {
                auto pit = st.presets.find(name);
                if (pit == st.presets.end()) continue;
                auto& p = pit->second;
                if (p.hidden && !st.showHidden) continue;

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (name == st.selected) flags |= ImGuiTreeNodeFlags_Selected;
                if (p.hidden) ImGui::PushStyleColor(ImGuiCol_Text, {0.5f, 0.5f, 0.5f, 1.0f});

                std::string label = (p.displayName.empty() ? name
                    : p.displayName + "  (" + name + ")") + "##" + name;
                ImGui::TreeNodeEx(label.c_str(), flags);
                if (ImGui::IsItemClicked()) {
                    st.selected = name;
                    st.resolvedText = BuildResolvedText(name, st);
                }
                if (p.hidden) ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("detail_panel", {0, 0}, ImGuiChildFlags_Borders);
    if (st.selected.empty()) {
        ImGui::TextDisabled("Select a preset from the tree.");
    } else {
        if (ImGui::Button("Copy to clipboard"))
            ImGui::SetClipboardText(st.resolvedText.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(st.resolvedText.c_str(),
                               st.resolvedText.c_str() + st.resolvedText.size());
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════
//  GUI — Graph tab
// ═══════════════════════════════════════════════════════════════

static void DrawBezier(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float thick) {
    float dy = (p1.y - p0.y) * 0.45f;
    dl->AddBezierCubic(p0, {p0.x, p0.y + dy}, {p1.x, p1.y - dy}, p1, col, thick);
}

static void DrawGraphTab(AppState& st) {
    if (!st.graphLayoutDone && !st.presets.empty()) BuildGraphLayout(st);

    if (ImGui::Button("Re-layout")) BuildGraphLayout(st);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Zoom", &st.graphZoom, 0.2f, 3.0f, "%.1fx");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) { st.graphScroll = {0, 0}; st.graphZoom = 1.0f; }
    ImGui::SameLine();
    ImGui::TextDisabled("   Pan: middle-drag | Zoom: scroll | Click: select | Drag: move");

    float detailW = st.selected.empty() ? 0.0f : 360.0f;
    float graphW  = ImGui::GetContentRegionAvail().x - detailW - (detailW > 0 ? 8.0f : 0.0f);

    ImGui::BeginChild("graph_canvas", {graphW, 0}, ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 cSize  = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io   = ImGui::GetIO();
    ImVec2 mouse  = io.MousePos;
    bool inCanvas = ImGui::IsWindowHovered();

    dl->AddRectFilled(origin, {origin.x + cSize.x, origin.y + cSize.y}, IM_COL32(22, 22, 28, 255));

    float gs = 50.0f * st.graphZoom;
    if (gs > 8.0f) {
        float ox = fmodf(st.graphScroll.x * st.graphZoom, gs);
        float oy = fmodf(st.graphScroll.y * st.graphZoom, gs);
        for (float x = ox; x < cSize.x; x += gs)
            dl->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + cSize.y}, IM_COL32(35, 35, 42, 255));
        for (float y = oy; y < cSize.y; y += gs)
            dl->AddLine({origin.x, origin.y + y}, {origin.x + cSize.x, origin.y + y}, IM_COL32(35, 35, 42, 255));
    }

    auto toScreen = [&](ImVec2 p) -> ImVec2 {
        return {origin.x + (p.x + st.graphScroll.x) * st.graphZoom,
                origin.y + (p.y + st.graphScroll.y) * st.graphZoom};
    };
    auto toCanvas = [&](ImVec2 s) -> ImVec2 {
        return {(s.x - origin.x) / st.graphZoom - st.graphScroll.x,
                (s.y - origin.y) / st.graphZoom - st.graphScroll.y};
    };

    if (inCanvas && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        st.graphScroll.x += io.MouseDelta.x / st.graphZoom;
        st.graphScroll.y += io.MouseDelta.y / st.graphZoom;
    }
    if (inCanvas && io.MouseWheel != 0.0f) {
        ImVec2 before = toCanvas(mouse);
        st.graphZoom = std::clamp(st.graphZoom + io.MouseWheel * 0.12f, 0.15f, 3.0f);
        ImVec2 after = toCanvas(mouse);
        st.graphScroll.x += after.x - before.x;
        st.graphScroll.y += after.y - before.y;
    }

    // Highlight chain
    std::set<std::string> hlSet;
    if (!st.selected.empty()) {
        std::queue<std::string> bfs;
        bfs.push(st.selected);
        while (!bfs.empty()) {
            auto cur = bfs.front(); bfs.pop();
            if (hlSet.count(cur)) continue;
            hlSet.insert(cur);
            auto it = st.presets.find(cur);
            if (it != st.presets.end())
                for (auto& p : it->second.inherits) bfs.push(p);
        }
        std::set<std::string> vis2 = {st.selected};
        std::queue<std::string> bfs2;
        bfs2.push(st.selected);
        while (!bfs2.empty()) {
            auto cur = bfs2.front(); bfs2.pop();
            hlSet.insert(cur);
            for (auto& [nm, p] : st.presets) {
                if (vis2.count(nm)) continue;
                for (auto& inh : p.inherits)
                    if (inh == cur) { vis2.insert(nm); bfs2.push(nm); break; }
            }
        }
    }

    // Edges
    for (auto& [name, p] : st.presets) {
        auto cIt = st.graphNodes.find(name);
        if (cIt == st.graphNodes.end()) continue;
        auto& cn = cIt->second;
        ImVec2 childTop = toScreen({cn.pos.x + cn.size.x * 0.5f, cn.pos.y});
        for (auto& parName : p.inherits) {
            auto pIt = st.graphNodes.find(parName);
            if (pIt == st.graphNodes.end()) continue;
            auto& pn = pIt->second;
            ImVec2 parBot = toScreen({pn.pos.x + pn.size.x * 0.5f, pn.pos.y + pn.size.y});
            bool chain = hlSet.count(name) && hlSet.count(parName);
            DrawBezier(dl, parBot, childTop,
                       chain ? IM_COL32(255,210,80,200) : IM_COL32(90,90,110,120),
                       chain ? 2.5f : 1.2f);
            // Arrow
            ImVec2 dir = {childTop.x - parBot.x, childTop.y - parBot.y};
            float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
            if (len > 1) {
                dir.x /= len; dir.y /= len;
                float as = 7 * st.graphZoom;
                ImVec2 ab = {childTop.x - dir.x*as, childTop.y - dir.y*as};
                ImVec2 pp = {-dir.y*as*0.45f, dir.x*as*0.45f};
                dl->AddTriangleFilled(childTop, {ab.x+pp.x,ab.y+pp.y}, {ab.x-pp.x,ab.y-pp.y},
                                      chain ? IM_COL32(255,210,80,200) : IM_COL32(90,90,110,120));
            }
        }
    }

    // Nodes
    std::string clickedNode;
    for (auto& [name, gn] : st.graphNodes) {
        ImVec2 sTL = toScreen(gn.pos);
        ImVec2 sBR = toScreen({gn.pos.x + gn.size.x, gn.pos.y + gn.size.y});
        bool hovered = inCanvas && mouse.x>=sTL.x && mouse.x<=sBR.x && mouse.y>=sTL.y && mouse.y<=sBR.y;

        auto pit = st.presets.find(name);
        PresetKind kind = pit != st.presets.end() ? pit->second.kind : PresetKind::Configure;
        bool isHidden = pit != st.presets.end() && pit->second.hidden;
        bool isSel = (name == st.selected);
        bool inChain = hlSet.count(name) > 0;
        float alpha = (!st.selected.empty() && !inChain) ? 0.30f : 1.0f;

        ImU32 kindCol = KindColor(kind);
        unsigned char kr=(kindCol>>0)&0xFF, kg=(kindCol>>8)&0xFF, kb=(kindCol>>16)&0xFF;

        ImU32 fill = isSel ? IM_COL32(55,55,75,(int)(255*alpha))
                   : hovered ? IM_COL32(45,45,60,(int)(255*alpha))
                   : IM_COL32(32,32,42,(int)(255*alpha));
        ImU32 border = isSel ? IM_COL32(255,210,80,(int)(255*alpha))
                             : IM_COL32(kr,kg,kb,(int)(200*alpha));
        float rounding = 6*st.graphZoom;

        dl->AddRectFilled({sTL.x+2,sTL.y+2},{sBR.x+2,sBR.y+2}, IM_COL32(0,0,0,(int)(60*alpha)), rounding);
        dl->AddRectFilled(sTL, sBR, fill, rounding);
        dl->AddRect(sTL, sBR, border, rounding, 0, isSel ? 2.5f : 1.5f);

        float barW = 5*st.graphZoom;
        dl->AddRectFilled(sTL, {sTL.x+barW, sBR.y}, IM_COL32(kr,kg,kb,(int)(255*alpha)),
                          rounding, ImDrawFlags_RoundCornersLeft);

        float fs = std::max(10.f, 13.f*st.graphZoom);
        float tx = sTL.x + barW + 6*st.graphZoom;
        float ty = sTL.y + (sBR.y - sTL.y - fs)*0.5f;
        ImU32 tcol = isHidden ? IM_COL32(110,110,110,(int)(255*alpha))
                              : IM_COL32(215,215,225,(int)(255*alpha));
        std::string lbl = (pit!=st.presets.end() && !pit->second.displayName.empty())
                          ? pit->second.displayName : name;
        dl->AddText(nullptr, fs, {tx, ty}, tcol, lbl.c_str());

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImColor(kindCol), "[%s]", KindLabel(kind));
            ImGui::SameLine(); ImGui::Text(" %s", name.c_str());
            if (pit != st.presets.end()) {
                if (!pit->second.displayName.empty()) ImGui::TextDisabled("%s", pit->second.displayName.c_str());
                if (pit->second.hidden) ImGui::TextDisabled("(hidden)");
                ImGui::TextDisabled("Source: %s", pit->second.sourceFile.c_str());
                if (!pit->second.inherits.empty()) {
                    ImGui::Text("Inherits:");
                    for (auto& inh : pit->second.inherits) ImGui::BulletText("%s", inh.c_str());
                }
            }
            ImGui::EndTooltip();
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            clickedNode = name;
            if (st.graphDragging.empty()) st.graphDragging = name;
        }
    }

    if (!st.graphDragging.empty()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto it = st.graphNodes.find(st.graphDragging);
            if (it != st.graphNodes.end()) {
                it->second.pos.x += io.MouseDelta.x / st.graphZoom;
                it->second.pos.y += io.MouseDelta.y / st.graphZoom;
            }
        } else st.graphDragging.clear();
    }

    if (!clickedNode.empty()) {
        st.selected = clickedNode;
        st.resolvedText = BuildResolvedText(clickedNode, st);
    }
    if (inCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && clickedNode.empty() && st.graphDragging.empty()) {
        st.selected.clear(); st.resolvedText.clear();
    }

    // Legend
    {
        ImVec2 lp = {origin.x+12, origin.y+cSize.y-26};
        for (auto k : {PresetKind::Configure,PresetKind::Build,PresetKind::Test,
                       PresetKind::Package,PresetKind::Workflow}) {
            dl->AddRectFilled(lp, {lp.x+10,lp.y+10}, KindColor(k), 2);
            dl->AddText({lp.x+14,lp.y-2}, IM_COL32(170,170,180,255), KindLabel(k));
            lp.x += ImGui::CalcTextSize(KindLabel(k)).x + 28;
        }
    }
    ImGui::EndChild();

    if (!st.selected.empty()) {
        ImGui::SameLine();
        ImGui::BeginChild("graph_detail", {detailW, 0}, ImGuiChildFlags_Borders);
        if (ImGui::Button("Copy##gd")) ImGui::SetClipboardText(st.resolvedText.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(st.resolvedText.c_str(), st.resolvedText.c_str()+st.resolvedText.size());
        ImGui::EndChild();
    }
}

// ═══════════════════════════════════════════════════════════════
//  GUI — Diff tab
// ═══════════════════════════════════════════════════════════════

static void DrawDiffTab(AppState& st) {
    if (st.presetNames.empty()) {
        ImGui::TextDisabled("Load a preset file first.");
        return;
    }

    // Combo boxes for left and right
    ImGui::Text("Left:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    const char* leftPreview = (st.diffLeft >= 0 && st.diffLeft < (int)st.presetNames.size())
                              ? st.presetNames[st.diffLeft].c_str() : "<select preset>";
    if (ImGui::BeginCombo("##diffleft", leftPreview)) {
        for (int i = 0; i < (int)st.presetNames.size(); ++i) {
            bool sel = (i == st.diffLeft);
            if (ImGui::Selectable((st.presetNames[i] + "##dl" + std::to_string(i)).c_str(), sel))
                st.diffLeft = i;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Text("  Right:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    const char* rightPreview = (st.diffRight >= 0 && st.diffRight < (int)st.presetNames.size())
                               ? st.presetNames[st.diffRight].c_str() : "<select preset>";
    if (ImGui::BeginCombo("##diffright", rightPreview)) {
        for (int i = 0; i < (int)st.presetNames.size(); ++i) {
            bool sel = (i == st.diffRight);
            if (ImGui::Selectable((st.presetNames[i] + "##dr" + std::to_string(i)).c_str(), sel))
                st.diffRight = i;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Swap")) { std::swap(st.diffLeft, st.diffRight); }

    if (st.diffLeft < 0 || st.diffRight < 0 ||
        st.diffLeft >= (int)st.presetNames.size() ||
        st.diffRight >= (int)st.presetNames.size()) {
        ImGui::TextDisabled("Select two presets to compare.");
        return;
    }

    if (st.diffLeft == st.diffRight) {
        ImGui::TextColored({1, 0.8f, 0.3f, 1}, "Both sides are the same preset.");
    }

    ImGui::Separator();

    std::vector<DiffLine> leftLines, rightLines;
    BuildDiffLines(st.presetNames[st.diffLeft], st.presetNames[st.diffRight], st, leftLines, rightLines);

    // Summary
    int added = 0, removed = 0, changed = 0, same = 0;
    for (size_t i = 0; i < leftLines.size(); ++i) {
        switch (leftLines[i].status) {
            case 0: same++; break;
            case -1: removed++; break;
            case 1: added++; break;
            case 2: changed++; break;
        }
    }
    ImGui::TextColored({0.5f, 0.5f, 0.5f, 1.0f}, "%d same", same);
    ImGui::SameLine();
    ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, "  %d added", added);
    ImGui::SameLine();
    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "  %d removed", removed);
    ImGui::SameLine();
    ImGui::TextColored({1.0f, 0.8f, 0.3f, 1.0f}, "  %d changed", changed);

    // Side by side panels
    float halfW = ImGui::GetContentRegionAvail().x * 0.5f - 4;

    // Left header
    ImGui::BeginChild("diff_left", {halfW, 0}, ImGuiChildFlags_Borders);
    ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "%s", st.presetNames[st.diffLeft].c_str());
    ImGui::Separator();

    for (size_t i = 0; i < leftLines.size(); ++i) {
        auto& ln = leftLines[i];
        ImVec4 col;
        switch (ln.status) {
            case 0:  col = {0.7f, 0.7f, 0.7f, 1.0f}; break;
            case -1: col = {1.0f, 0.4f, 0.4f, 1.0f}; break;
            case 1:  col = {0.4f, 0.4f, 0.4f, 0.5f}; break;
            case 2:  col = {1.0f, 0.8f, 0.3f, 1.0f}; break;
            default: col = {0.7f, 0.7f, 0.7f, 1.0f};
        }
        if (ln.text.empty()) {
            ImGui::TextColored(col, " ");
        } else {
            ImGui::TextColored(col, "%s", ln.text.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right
    ImGui::BeginChild("diff_right", {halfW, 0}, ImGuiChildFlags_Borders);
    ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "%s", st.presetNames[st.diffRight].c_str());
    ImGui::Separator();

    for (size_t i = 0; i < rightLines.size(); ++i) {
        auto& ln = rightLines[i];
        ImVec4 col;
        switch (ln.status) {
            case 0:  col = {0.7f, 0.7f, 0.7f, 1.0f}; break;
            case 1:  col = {0.4f, 1.0f, 0.4f, 1.0f}; break;
            case -1: col = {0.4f, 0.4f, 0.4f, 0.5f}; break;
            case 2:  col = {1.0f, 0.8f, 0.3f, 1.0f}; break;
            default: col = {0.7f, 0.7f, 0.7f, 1.0f};
        }
        if (ln.text.empty()) {
            ImGui::TextColored(col, " ");
        } else {
            ImGui::TextColored(col, "%s", ln.text.c_str());
        }
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════
//  Main GUI frame
// ═══════════════════════════════════════════════════════════════

static void DrawUI(AppState& st) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("TreeMake", nullptr,
                 ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

    DrawMenuBar(st);

    // Keyboard shortcut: Ctrl+O
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        OpenFileDialog(st);

    // Top bar
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    bool enter = ImGui::InputTextWithHint("##path", "Path to CMakePresets.json (or drag-and-drop a file)",
                                           st.pathBuf, sizeof(st.pathBuf),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load", {70, 0}) || enter) LoadFile(st.pathBuf, st);

    if (!st.errorMsg.empty())
        ImGui::TextColored({1, 0.3f, 0.3f, 1}, "%s", st.errorMsg.c_str());
    if (!st.filePath.empty())
        ImGui::TextDisabled("Loaded: %s  (version %d, %zu presets)",
                            st.filePath.c_str(), st.version, st.presets.size());

    ImGui::Checkbox("Show hidden", &st.showHidden);
    ImGui::Separator();

    // Tabs
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Presets"))           { DrawPresetsTab(st); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Inheritance Graph")) { DrawGraphTab(st);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Diff"))              { DrawDiffTab(st);    ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════
//  Entry point
// ═══════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    if (!glfwInit()) { fprintf(stderr, "Failed to init GLFW\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1400, 800, "TreeMake", nullptr, nullptr);
    if (!window) { fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Set window icon from embedded pixel data
    GLFWimage icon;
    icon.width  = ICON_WIDTH;
    icon.height = ICON_HEIGHT;
    icon.pixels = (unsigned char*)ICON_RGBA;
    glfwSetWindowIcon(window, 1, &icon);

    // NFD init
    NFD_Init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding  = 4; style.GrabRounding = 4; style.TabRounding = 4;
    style.WindowRounding = 0; style.FramePadding = {8, 4}; style.ItemSpacing = {8, 6};
    style.Colors[ImGuiCol_Tab]         = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.28f, 0.28f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_TabHovered]  = ImVec4(0.32f, 0.32f, 0.45f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    AppState st;
    g_appState = &st;

    // Set up drag-and-drop callback
    glfwSetDropCallback(window, DropCallback);

    // Load recent files
    LoadRecentFiles(st);

    // CLI arg
    if (argc > 1) {
        snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", argv[1]);
        LoadFile(argv[1], st);
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Handle deferred file load (from drag-drop or dialog)
        if (!st.pendingLoad.empty()) {
            LoadFile(st.pendingLoad, st);
            st.pendingLoad.clear();
        }

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
