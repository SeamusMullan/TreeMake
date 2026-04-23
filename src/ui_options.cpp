#include "ui_options.h"

#include "imgui.h"

// ═══════════════════════════════════════════════════════════════
//  Options & Cache tab
// ═══════════════════════════════════════════════════════════════

void DrawOptionsTab(AppState& st) {
    if (!st.projectLoaded) {
        ImGui::TextDisabled("Open a project directory first.");
        return;
    }

    static char filterBuf[256] = {};
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##opt_filter", "Filter...", filterBuf, sizeof(filterBuf));
    ImGui::Separator();
    std::string filter(filterBuf);

    // ── option() calls ──
    if (ImGui::CollapsingHeader("CMake Options (option())", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (st.cmakeProject.options.empty()) {
            ImGui::TextDisabled("  No option() calls found.");
        } else {
            if (ImGui::BeginTable("options_table", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Name", 0, 2.0f);
                ImGui::TableSetupColumn("Default", 0, 0.5f);
                ImGui::TableSetupColumn("Description", 0, 3.0f);
                ImGui::TableSetupColumn("File", 0, 1.5f);
                ImGui::TableHeadersRow();

                for (auto& opt : st.cmakeProject.options) {
                    if (!filter.empty() &&
                        opt.name.find(filter) == std::string::npos &&
                        opt.description.find(filter) == std::string::npos)
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(opt.name.c_str());
                    ImGui::TableNextColumn();
                    ImVec4 col = (opt.defaultValue == "ON")
                        ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                        : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                    ImGui::TextColored(col, "%s", opt.defaultValue.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", opt.description.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", opt.definedInFile.c_str());
                }
                ImGui::EndTable();
            }
        }
    }

    // ── set(... CACHE ...) ──
    if (ImGui::CollapsingHeader("Cache Variables (set(CACHE))", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (st.cmakeProject.cacheVars.empty()) {
            ImGui::TextDisabled("  No set(CACHE) calls found.");
        } else {
            if (ImGui::BeginTable("cache_table", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Description");
                ImGui::TableSetupColumn("File");
                ImGui::TableHeadersRow();

                for (auto& cv : st.cmakeProject.cacheVars) {
                    if (!filter.empty() && cv.name.find(filter) == std::string::npos)
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(cv.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(cv.value.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", cv.type.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", cv.docstring.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", cv.definedInFile.c_str());
                }
                ImGui::EndTable();
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  Dependencies tab
// ═══════════════════════════════════════════════════════════════

void DrawDepsTab(AppState& st) {
    if (!st.projectLoaded) {
        ImGui::TextDisabled("Open a project directory first.");
        return;
    }

    // ── FetchContent ──
    if (ImGui::CollapsingHeader("FetchContent Dependencies", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (st.cmakeProject.fetchContentDeps.empty()) {
            ImGui::TextDisabled("  No FetchContent_Declare() calls found.");
        } else {
            for (auto& dep : st.cmakeProject.fetchContentDeps) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 200, 255, 255));
                bool open = ImGui::TreeNodeEx((dep.name + "##fc").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor();

                if (open) {
                    if (!dep.gitRepository.empty()) {
                        ImGui::BulletText("Repository: %s", dep.gitRepository.c_str());
                        if (!dep.gitTag.empty())
                            ImGui::BulletText("Tag: %s", dep.gitTag.c_str());
                        if (dep.gitShallow)
                            ImGui::BulletText("Shallow: true");
                    } else if (!dep.url.empty()) {
                        ImGui::BulletText("URL: %s", dep.url.c_str());
                    }
                    ImGui::BulletText("Defined in: %s", dep.definedInFile.c_str());
                    ImGui::TreePop();
                }
            }
        }
    }

    ImGui::Separator();

    // ── find_package ──
    if (ImGui::CollapsingHeader("find_package Dependencies", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (st.cmakeProject.findPackageDeps.empty()) {
            ImGui::TextDisabled("  No find_package() calls found.");
        } else {
            if (ImGui::BeginTable("findpkg_table", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Package");
                ImGui::TableSetupColumn("Version");
                ImGui::TableSetupColumn("Required");
                ImGui::TableSetupColumn("Components");
                ImGui::TableHeadersRow();

                for (auto& dep : st.cmakeProject.findPackageDeps) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(dep.name.c_str());
                    ImGui::TableNextColumn();
                    if (!dep.version.empty())
                        ImGui::TextUnformatted(dep.version.c_str());
                    else
                        ImGui::TextDisabled("any");
                    ImGui::TableNextColumn();
                    if (dep.required)
                        ImGui::TextColored({1.0f, 0.6f, 0.3f, 1.0f}, "REQUIRED");
                    else
                        ImGui::TextDisabled("optional");
                    ImGui::TableNextColumn();
                    if (!dep.components.empty()) {
                        std::string comps;
                        for (size_t i = 0; i < dep.components.size(); ++i) {
                            if (i) comps += ", ";
                            comps += dep.components[i];
                        }
                        ImGui::TextUnformatted(comps.c_str());
                    }
                }
                ImGui::EndTable();
            }
        }
    }
}
