#include "ui_draw.h"
#include "preset_resolve.h"
#include "preset_loader.h"
#include "recent_files.h"
#include "diff.h"
#include "graph_layout.h"
#include "file_dialog.h"
#include "ui_project.h"
#include "dep_graph.h"
#include "ui_options.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <queue>
#include <set>

// ═══════════════════════════════════════════════════════════════
//  UI scaling
// ═══════════════════════════════════════════════════════════════

void ApplyUIScale(AppState& st) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig cfg;
    cfg.SizePixels = std::round(13.0f * st.uiScale);
    io.Fonts->AddFontDefault(&cfg);
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding  = 4; style.GrabRounding = 4; style.TabRounding = 4;
    style.WindowRounding = 0; style.FramePadding = {8, 4}; style.ItemSpacing = {8, 6};
    style.Colors[ImGuiCol_Tab]         = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.28f, 0.28f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_TabHovered]  = ImVec4(0.32f, 0.32f, 0.45f, 1.0f);
    style.ScaleAllSizes(st.uiScale);

    st.needFontRebuild = false;
}

// ═══════════════════════════════════════════════════════════════
//  Bezier helper
// ═══════════════════════════════════════════════════════════════

static void DrawBezier(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float thick) {
    float dy = (p1.y - p0.y) * 0.45f;
    dl->AddBezierCubic(p0, {p0.x, p0.y + dy}, {p1.x, p1.y - dy}, p1, col, thick);
}

// ═══════════════════════════════════════════════════════════════
//  Menu bar
// ═══════════════════════════════════════════════════════════════

static void DrawMenuBar(AppState& st) {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File...", "Ctrl+O")) {
                OpenFileDialog(st);
            }
            if (ImGui::MenuItem("Open Directory...", "Ctrl+Shift+O")) {
                OpenDirectoryDialog(st);
            }
            if (ImGui::BeginMenu("Recent Files", !st.recentFiles.empty())) {
                for (size_t i = 0; i < st.recentFiles.size(); ++i) {
                    std::string label = st.recentFiles[i];
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
                if (auto* win = glfwGetCurrentContext())
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::SeparatorText("UI Scale");
            static const float kScales[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f};
            for (float s : kScales) {
                char label[16];
                snprintf(label, sizeof(label), "%d%%", (int)(s * 100));
                bool selected = (std::abs(st.uiScale - s) < 0.01f);
                if (ImGui::MenuItem(label, nullptr, selected)) {
                    if (!selected) {
                        st.uiScale = s;
                        st.needFontRebuild = true;
                        SaveSettings(st);
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Presets tab
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
//  Graph tab
// ═══════════════════════════════════════════════════════════════

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
//  Diff tab
// ═══════════════════════════════════════════════════════════════

static void DrawDiffTab(AppState& st) {
    if (st.presetNames.empty()) {
        ImGui::TextDisabled("Load a preset file first.");
        return;
    }

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

    float halfW = ImGui::GetContentRegionAvail().x * 0.5f - 4;

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
//  Main UI frame
// ═══════════════════════════════════════════════════════════════

void DrawUI(AppState& st) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("TreeMake", nullptr,
                 ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

    DrawMenuBar(st);

    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O))
        OpenDirectoryDialog(st);
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        OpenFileDialog(st);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    bool enter = ImGui::InputTextWithHint("##path",
        "Path to CMakePresets.json or project directory (drag-and-drop supported)",
        st.pathBuf, sizeof(st.pathBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load", {70, 0}) || enter) {
        st.pendingLoad = st.pathBuf;
    }

    if (!st.errorMsg.empty())
        ImGui::TextColored({1, 0.3f, 0.3f, 1}, "%s", st.errorMsg.c_str());
    if (!st.filePath.empty())
        ImGui::TextDisabled("Loaded: %s  (version %d, %zu presets)",
                            st.filePath.c_str(), st.version, st.presets.size());
    if (st.projectLoaded)
        ImGui::TextDisabled("Project: %s  (%zu targets, %zu options, %zu deps)",
            st.cmakeProject.projectName.c_str(),
            st.cmakeProject.targets.size(),
            st.cmakeProject.options.size(),
            st.cmakeProject.fetchContentDeps.size() + st.cmakeProject.findPackageDeps.size());

    ImGui::Checkbox("Show hidden", &st.showHidden);
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainTabs")) {
        if (!st.presets.empty()) {
            if (ImGui::BeginTabItem("Presets"))           { DrawPresetsTab(st); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Inheritance Graph")) { DrawGraphTab(st);   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Diff"))              { DrawDiffTab(st);    ImGui::EndTabItem(); }
        }
        if (st.projectLoaded) {
            if (ImGui::BeginTabItem("Project"))         { DrawProjectTab(st);  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Targets"))         { DrawTargetsTab(st);  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Dep Graph"))       { DrawDepGraphTab(st); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Options & Cache")) { DrawOptionsTab(st);  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Dependencies"))    { DrawDepsTab(st);     ImGui::EndTabItem(); }
        }
        if (st.presets.empty() && !st.projectLoaded) {
            if (ImGui::BeginTabItem("Welcome")) {
                ImGui::TextDisabled("Open a CMakePresets.json file or a project directory to get started.");
                ImGui::TextDisabled("Ctrl+O: Open file  |  Ctrl+Shift+O: Open directory  |  Drag-and-drop supported");
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
