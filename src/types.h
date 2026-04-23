#pragma once

#include "imgui.h"
#include <nlohmann/json.hpp>

#include "cmake_parser.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

enum class PresetKind { Configure, Build, Test, Package, Workflow };

const char* KindLabel(PresetKind k);
ImU32 KindColor(PresetKind k);

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

struct ResolvedPreset {
    std::string generator, binaryDir, installDir, toolchainFile, configurePreset, configuration;
    std::map<std::string, std::string> cacheVariables;
};

struct GraphNode {
    std::string name;
    ImVec2      pos  = {0, 0};
    ImVec2      size = {200, 40};
};

struct DiffLine {
    std::string text;
    int         status; // 0 = same, -1 = left only, +1 = right only, 2 = changed
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
    std::vector<std::string>             presetNames;

    // Recent files
    std::vector<std::string>             recentFiles;

    // Pending file load (from drag-drop or dialog, deferred to main loop)
    std::string                          pendingLoad;
    std::string                          pendingDirLoad;

    // UI scaling
    float                                uiScale = 1.0f;
    bool                                 needFontRebuild = false;

    // CMake project state
    CMakeProject                         cmakeProject;
    bool                                 projectLoaded = false;
    std::string                          projectRootPath;
    std::string                          selectedTarget;
    std::string                          selectedDirectory;

    // Target dependency graph
    std::map<std::string, GraphNode>     depGraphNodes;
    ImVec2                               depGraphScroll = {0, 0};
    float                                depGraphZoom = 1.0f;
    bool                                 depGraphLayoutDone = false;
    std::string                          depGraphDragging;
};
