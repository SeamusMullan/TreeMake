#include "preset_loader.h"
#include "json_helpers.h"
#include "recent_files.h"
#include "cmake_parser.h"

#include <algorithm>
#include <fstream>

void ParsePresetsFromArray(const json& arr, PresetKind kind,
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

void LoadPresetFile(const std::string& path, AppState& st,
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

bool LoadFile(const std::string& path, AppState& st) {
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

    for (auto& [name, _] : st.presets) st.presetNames.push_back(name);
    std::sort(st.presetNames.begin(), st.presetNames.end());

    AddToRecent(st, path);
    return st.errorMsg.empty();
}

bool LoadProjectDirectory(const std::string& dirPath, AppState& st) {
    st.cmakeProject = CMakeProject{};
    st.projectLoaded = false;
    st.projectRootPath.clear();
    st.selectedTarget.clear();
    st.selectedDirectory.clear();
    st.depGraphNodes.clear();
    st.depGraphLayoutDone = false;
    st.errorMsg.clear();

    fs::path root(dirPath);
    if (!fs::is_directory(root)) {
        st.errorMsg = "Not a directory: " + dirPath;
        return false;
    }

    if (!ParseCMakeProject(root, st.cmakeProject)) {
        st.errorMsg = st.cmakeProject.errorLog;
        return false;
    }
    st.projectLoaded = true;
    st.projectRootPath = dirPath;

    fs::path presetsFile = root / "CMakePresets.json";
    if (fs::exists(presetsFile))
        LoadFile(presetsFile.string(), st);

    snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", dirPath.c_str());
    AddToRecent(st, dirPath);
    return true;
}
