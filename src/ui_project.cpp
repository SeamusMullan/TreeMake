#include "ui_project.h"

#include "imgui.h"

// ═══════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════

static const char* TargetTypeLabel(CMakeTargetType t) {
    switch (t) {
        case CMakeTargetType::Executable:       return "Executable";
        case CMakeTargetType::StaticLibrary:    return "Static Library";
        case CMakeTargetType::SharedLibrary:    return "Shared Library";
        case CMakeTargetType::InterfaceLibrary: return "Interface Library";
        case CMakeTargetType::ObjectLibrary:    return "Object Library";
        default:                                return "Unknown";
    }
}

static const char* TargetTypeIcon(CMakeTargetType t) {
    switch (t) {
        case CMakeTargetType::Executable:       return "[EXE]";
        case CMakeTargetType::StaticLibrary:    return "[LIB]";
        case CMakeTargetType::SharedLibrary:    return "[SO]";
        case CMakeTargetType::InterfaceLibrary: return "[IFC]";
        case CMakeTargetType::ObjectLibrary:    return "[OBJ]";
        default:                                return "[?]";
    }
}

static ImU32 TargetTypeColor(CMakeTargetType t) {
    switch (t) {
        case CMakeTargetType::Executable:       return IM_COL32(100, 200, 255, 255);
        case CMakeTargetType::StaticLibrary:    return IM_COL32(100, 220, 130, 255);
        case CMakeTargetType::SharedLibrary:    return IM_COL32(220, 180, 100, 255);
        case CMakeTargetType::InterfaceLibrary: return IM_COL32(180, 130, 220, 255);
        case CMakeTargetType::ObjectLibrary:    return IM_COL32(220, 120, 120, 255);
        default:                                return IM_COL32(180, 180, 180, 255);
    }
}

// ═══════════════════════════════════════════════════════════════
//  Target detail panel (shared between Project + Targets tabs)
// ═══════════════════════════════════════════════════════════════

static void DrawTargetDetail(AppState& st, const std::string& targetName) {
    auto it = st.cmakeProject.targets.find(targetName);
    if (it == st.cmakeProject.targets.end()) {
        ImGui::TextDisabled("Target not found.");
        return;
    }
    auto& tgt = it->second;

    ImGui::PushStyleColor(ImGuiCol_Text, TargetTypeColor(tgt.type));
    ImGui::Text("%s %s", TargetTypeIcon(tgt.type), tgt.name.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Type: %s", TargetTypeLabel(tgt.type));
    ImGui::TextDisabled("Defined in: %s (line %d)", tgt.definedInFile.c_str(), tgt.definedAtLine);
    ImGui::Separator();

    if (!tgt.sources.empty()) {
        if (ImGui::CollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& src : tgt.sources)
                ImGui::BulletText("%s", src.c_str());
        }
    }

    if (!tgt.linkLibraries.empty()) {
        if (ImGui::CollapsingHeader("Link Libraries", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& lib : tgt.linkLibraries) {
                bool isProjectTarget = st.cmakeProject.targets.count(lib) > 0;
                if (isProjectTarget) {
                    ImGui::PushStyleColor(ImGuiCol_Text, TargetTypeColor(st.cmakeProject.targets[lib].type));
                    if (ImGui::SmallButton(lib.c_str())) {
                        st.selectedTarget = lib;
                        st.selectedDirectory.clear();
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::BulletText("%s", lib.c_str());
                }
            }
        }
    }

    if (!tgt.includeDirectories.empty()) {
        if (ImGui::CollapsingHeader("Include Directories", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& dir : tgt.includeDirectories)
                ImGui::BulletText("%s", dir.c_str());
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  Directory tree (recursive)
// ═══════════════════════════════════════════════════════════════

static void DrawDirectoryNode(AppState& st, const std::string& relPath) {
    auto it = st.cmakeProject.directories.find(relPath);
    if (it == st.cmakeProject.directories.end()) return;
    auto& dir = it->second;

    std::string label = (relPath == ".")
        ? (st.cmakeProject.projectName.empty() ? "." : st.cmakeProject.projectName)
        : fs::path(relPath).filename().string();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    if (dir.subdirectories.empty() && dir.targetNames.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (relPath == st.selectedDirectory)
        flags |= ImGuiTreeNodeFlags_Selected;

    bool open = ImGui::TreeNodeEx((label + "##dir_" + relPath).c_str(), flags);
    if (ImGui::IsItemClicked()) {
        st.selectedDirectory = relPath;
        st.selectedTarget.clear();
    }

    if (open) {
        for (auto& tgtName : dir.targetNames) {
            auto tgtIt = st.cmakeProject.targets.find(tgtName);
            if (tgtIt == st.cmakeProject.targets.end()) continue;
            auto& tgt = tgtIt->second;

            ImGui::PushStyleColor(ImGuiCol_Text, TargetTypeColor(tgt.type));
            ImGuiTreeNodeFlags tflags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (tgtName == st.selectedTarget) tflags |= ImGuiTreeNodeFlags_Selected;

            std::string tLabel = std::string(TargetTypeIcon(tgt.type)) + " " + tgtName + "##tgt_" + tgtName;
            ImGui::TreeNodeEx(tLabel.c_str(), tflags);
            if (ImGui::IsItemClicked()) {
                st.selectedTarget = tgtName;
                st.selectedDirectory.clear();
            }
            ImGui::PopStyleColor();
        }

        for (auto& subDir : dir.subdirectories) {
            std::string childPath = (relPath == ".") ? subDir : relPath + "/" + subDir;
            DrawDirectoryNode(st, childPath);
        }
        ImGui::TreePop();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Directory detail
// ═══════════════════════════════════════════════════════════════

static void DrawDirectoryDetail(AppState& st, const std::string& relPath) {
    auto it = st.cmakeProject.directories.find(relPath);
    if (it == st.cmakeProject.directories.end()) {
        ImGui::TextDisabled("Directory not found.");
        return;
    }
    auto& dir = it->second;

    ImGui::Text("Directory: %s", relPath.c_str());
    ImGui::TextDisabled("Path: %s", dir.absolutePath.string().c_str());
    ImGui::Separator();

    if (!dir.targetNames.empty()) {
        ImGui::Text("Targets (%zu):", dir.targetNames.size());
        for (auto& tgtName : dir.targetNames) {
            auto tgtIt = st.cmakeProject.targets.find(tgtName);
            if (tgtIt == st.cmakeProject.targets.end()) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, TargetTypeColor(tgtIt->second.type));
            if (ImGui::SmallButton((std::string(TargetTypeIcon(tgtIt->second.type)) + " " + tgtName).c_str())) {
                st.selectedTarget = tgtName;
                st.selectedDirectory.clear();
            }
            ImGui::PopStyleColor();
        }
    }

    if (!dir.subdirectories.empty()) {
        ImGui::Spacing();
        ImGui::Text("Subdirectories (%zu):", dir.subdirectories.size());
        for (auto& sub : dir.subdirectories)
            ImGui::BulletText("%s", sub.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════
//  Project Tab
// ═══════════════════════════════════════════════════════════════

void DrawProjectTab(AppState& st) {
    if (!st.projectLoaded) {
        ImGui::TextDisabled("Open a project directory to see the project tree.");
        return;
    }

    float panelW = ImGui::GetContentRegionAvail().x * 0.30f;
    ImGui::BeginChild("project_tree_panel", {panelW, 0},
                       ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    DrawDirectoryNode(st, ".");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("project_detail_panel", {0, 0}, ImGuiChildFlags_Borders);
    if (!st.selectedTarget.empty()) {
        DrawTargetDetail(st, st.selectedTarget);
    } else if (!st.selectedDirectory.empty()) {
        DrawDirectoryDetail(st, st.selectedDirectory);
    } else {
        ImGui::TextDisabled("Select a target or directory from the tree.");
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════
//  Targets Tab (flat list)
// ═══════════════════════════════════════════════════════════════

void DrawTargetsTab(AppState& st) {
    if (!st.projectLoaded) {
        ImGui::TextDisabled("Open a project directory first.");
        return;
    }

    static char filterBuf[256] = {};
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##tgt_filter", "Filter targets...", filterBuf, sizeof(filterBuf));
    ImGui::Separator();

    float panelW = ImGui::GetContentRegionAvail().x * 0.30f;
    ImGui::BeginChild("targets_list", {panelW, 0},
                       ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    std::string filter(filterBuf);
    for (auto& [name, tgt] : st.cmakeProject.targets) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (name == st.selectedTarget) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushStyleColor(ImGuiCol_Text, TargetTypeColor(tgt.type));
        std::string label = std::string(TargetTypeIcon(tgt.type)) + " " + name + "##tgtlist_" + name;
        ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked()) {
            st.selectedTarget = name;
            st.selectedDirectory.clear();
        }
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("target_detail", {0, 0}, ImGuiChildFlags_Borders);
    if (!st.selectedTarget.empty())
        DrawTargetDetail(st, st.selectedTarget);
    else
        ImGui::TextDisabled("Select a target.");
    ImGui::EndChild();
}
