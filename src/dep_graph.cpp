#include "dep_graph.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>

// ═══════════════════════════════════════════════════════════════
//  Color helpers (same as ui_project.cpp — keep local)
// ═══════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════
//  Layout
// ═══════════════════════════════════════════════════════════════

void BuildTargetGraphLayout(AppState& st) {
    st.depGraphNodes.clear();
    if (st.cmakeProject.targets.empty()) return;

    std::set<std::string> projectTargets;
    for (auto& [name, _] : st.cmakeProject.targets) projectTargets.insert(name);

    // Build in-degree from link deps (only internal targets)
    std::map<std::string, std::vector<std::string>> dependsOn;
    std::map<std::string, std::vector<std::string>> dependedBy;
    std::map<std::string, int> inDeg;

    for (auto& [name, tgt] : st.cmakeProject.targets) {
        inDeg[name] = 0;
        for (auto& lib : tgt.linkLibraries) {
            if (projectTargets.count(lib)) {
                dependsOn[name].push_back(lib);
                dependedBy[lib].push_back(name);
            }
        }
    }
    for (auto& [name, deps] : dependsOn) inDeg[name] = (int)deps.size();

    // Topological layering (depth = how many deps deep)
    std::map<std::string, int> depth;
    std::queue<std::string> q;
    for (auto& [name, deg] : inDeg) {
        depth[name] = -1;
        if (deg == 0) { depth[name] = 0; q.push(name); }
    }

    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        int d = depth[cur];
        for (auto& child : dependedBy[cur]) {
            if (depth[child] < d + 1) {
                depth[child] = d + 1;
                q.push(child);
            }
        }
    }
    for (auto& [name, d] : depth) if (d < 0) d = 0;

    // Group by depth
    std::map<int, std::vector<std::string>> layers;
    for (auto& [name, d] : depth) layers[d].push_back(name);
    for (auto& [d, names] : layers)
        std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
            auto& ta = st.cmakeProject.targets[a];
            auto& tb = st.cmakeProject.targets[b];
            if (ta.type != tb.type) return (int)ta.type < (int)tb.type;
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
            GraphNode gn;
            gn.name = names[i];
            gn.pos = {startX + i * (nodeW + nodeGap), y};
            gn.size = {nodeW, nodeH};
            st.depGraphNodes[names[i]] = gn;
        }
    }
    st.depGraphLayoutDone = true;
}

// ═══════════════════════════════════════════════════════════════
//  Bezier helper
// ═══════════════════════════════════════════════════════════════

static void DrawBezier(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float thick) {
    float dy = (p1.y - p0.y) * 0.45f;
    dl->AddBezierCubic(p0, {p0.x, p0.y + dy}, {p1.x, p1.y - dy}, p1, col, thick);
}

// ═══════════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════════

void DrawDepGraphTab(AppState& st) {
    if (!st.projectLoaded) {
        ImGui::TextDisabled("Open a project directory first.");
        return;
    }

    if (!st.depGraphLayoutDone && !st.cmakeProject.targets.empty())
        BuildTargetGraphLayout(st);

    if (ImGui::Button("Re-layout")) BuildTargetGraphLayout(st);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Zoom##dep", &st.depGraphZoom, 0.2f, 3.0f, "%.1fx");
    ImGui::SameLine();
    if (ImGui::Button("Fit##dep")) { st.depGraphScroll = {0, 0}; st.depGraphZoom = 1.0f; }
    ImGui::SameLine();
    ImGui::TextDisabled("   Pan: middle-drag | Zoom: scroll | Click: select | Drag: move");

    float detailW = st.selectedTarget.empty() ? 0.0f : 360.0f;
    float graphW  = ImGui::GetContentRegionAvail().x - detailW - (detailW > 0 ? 8.0f : 0.0f);

    ImGui::BeginChild("dep_canvas", {graphW, 0}, ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 cSize  = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io   = ImGui::GetIO();
    ImVec2 mouse  = io.MousePos;
    bool inCanvas = ImGui::IsWindowHovered();

    dl->AddRectFilled(origin, {origin.x + cSize.x, origin.y + cSize.y}, IM_COL32(22, 22, 28, 255));

    // Grid
    float gs = 50.0f * st.depGraphZoom;
    if (gs > 8.0f) {
        float ox = fmodf(st.depGraphScroll.x * st.depGraphZoom, gs);
        float oy = fmodf(st.depGraphScroll.y * st.depGraphZoom, gs);
        for (float x = ox; x < cSize.x; x += gs)
            dl->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + cSize.y}, IM_COL32(35, 35, 42, 255));
        for (float y = oy; y < cSize.y; y += gs)
            dl->AddLine({origin.x, origin.y + y}, {origin.x + cSize.x, origin.y + y}, IM_COL32(35, 35, 42, 255));
    }

    auto toScreen = [&](ImVec2 p) -> ImVec2 {
        return {origin.x + (p.x + st.depGraphScroll.x) * st.depGraphZoom,
                origin.y + (p.y + st.depGraphScroll.y) * st.depGraphZoom};
    };
    auto toCanvas = [&](ImVec2 s) -> ImVec2 {
        return {(s.x - origin.x) / st.depGraphZoom - st.depGraphScroll.x,
                (s.y - origin.y) / st.depGraphZoom - st.depGraphScroll.y};
    };

    if (inCanvas && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        st.depGraphScroll.x += io.MouseDelta.x / st.depGraphZoom;
        st.depGraphScroll.y += io.MouseDelta.y / st.depGraphZoom;
    }
    if (inCanvas && io.MouseWheel != 0.0f) {
        ImVec2 before = toCanvas(mouse);
        st.depGraphZoom = std::clamp(st.depGraphZoom + io.MouseWheel * 0.12f, 0.15f, 3.0f);
        ImVec2 after = toCanvas(mouse);
        st.depGraphScroll.x += after.x - before.x;
        st.depGraphScroll.y += after.y - before.y;
    }

    // Build project target set for edge drawing
    std::set<std::string> projectTargets;
    for (auto& [name, _] : st.cmakeProject.targets) projectTargets.insert(name);

    // Highlight chain for selected target
    std::set<std::string> hlSet;
    if (!st.selectedTarget.empty()) {
        // Walk up (deps) and down (dependents)
        std::queue<std::string> bfs;
        bfs.push(st.selectedTarget);
        while (!bfs.empty()) {
            auto cur = bfs.front(); bfs.pop();
            if (hlSet.count(cur)) continue;
            hlSet.insert(cur);
            auto it = st.cmakeProject.targets.find(cur);
            if (it != st.cmakeProject.targets.end())
                for (auto& lib : it->second.linkLibraries)
                    if (projectTargets.count(lib)) bfs.push(lib);
        }
        std::set<std::string> vis2 = {st.selectedTarget};
        std::queue<std::string> bfs2;
        bfs2.push(st.selectedTarget);
        while (!bfs2.empty()) {
            auto cur = bfs2.front(); bfs2.pop();
            hlSet.insert(cur);
            for (auto& [nm, tgt] : st.cmakeProject.targets) {
                if (vis2.count(nm)) continue;
                for (auto& lib : tgt.linkLibraries)
                    if (lib == cur && projectTargets.count(nm)) {
                        vis2.insert(nm); bfs2.push(nm); break;
                    }
            }
        }
    }

    // Edges: target -> its link libraries
    for (auto& [name, tgt] : st.cmakeProject.targets) {
        auto cIt = st.depGraphNodes.find(name);
        if (cIt == st.depGraphNodes.end()) continue;
        auto& cn = cIt->second;
        ImVec2 childBot = toScreen({cn.pos.x + cn.size.x * 0.5f, cn.pos.y + cn.size.y});
        for (auto& lib : tgt.linkLibraries) {
            auto pIt = st.depGraphNodes.find(lib);
            if (pIt == st.depGraphNodes.end()) continue;
            auto& pn = pIt->second;
            ImVec2 parTop = toScreen({pn.pos.x + pn.size.x * 0.5f, pn.pos.y});
            bool chain = hlSet.count(name) && hlSet.count(lib);
            DrawBezier(dl, childBot, parTop,
                       chain ? IM_COL32(255,210,80,200) : IM_COL32(90,90,110,120),
                       chain ? 2.5f : 1.2f);
            // Arrow
            ImVec2 dir = {parTop.x - childBot.x, parTop.y - childBot.y};
            float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
            if (len > 1) {
                dir.x /= len; dir.y /= len;
                float as = 7 * st.depGraphZoom;
                ImVec2 ab = {parTop.x - dir.x*as, parTop.y - dir.y*as};
                ImVec2 pp = {-dir.y*as*0.45f, dir.x*as*0.45f};
                dl->AddTriangleFilled(parTop, {ab.x+pp.x,ab.y+pp.y}, {ab.x-pp.x,ab.y-pp.y},
                                      chain ? IM_COL32(255,210,80,200) : IM_COL32(90,90,110,120));
            }
        }
    }

    // Nodes
    std::string clickedNode;
    for (auto& [name, gn] : st.depGraphNodes) {
        ImVec2 sTL = toScreen(gn.pos);
        ImVec2 sBR = toScreen({gn.pos.x + gn.size.x, gn.pos.y + gn.size.y});
        bool hovered = inCanvas && mouse.x>=sTL.x && mouse.x<=sBR.x && mouse.y>=sTL.y && mouse.y<=sBR.y;

        auto tit = st.cmakeProject.targets.find(name);
        CMakeTargetType ttype = tit != st.cmakeProject.targets.end() ? tit->second.type : CMakeTargetType::Unknown;
        bool isSel = (name == st.selectedTarget);
        bool inChain = hlSet.count(name) > 0;
        float alpha = (!st.selectedTarget.empty() && !inChain) ? 0.30f : 1.0f;

        ImU32 kindCol = TargetTypeColor(ttype);
        unsigned char kr=(kindCol>>0)&0xFF, kg=(kindCol>>8)&0xFF, kb=(kindCol>>16)&0xFF;

        ImU32 fill = isSel ? IM_COL32(55,55,75,(int)(255*alpha))
                   : hovered ? IM_COL32(45,45,60,(int)(255*alpha))
                   : IM_COL32(32,32,42,(int)(255*alpha));
        ImU32 border = isSel ? IM_COL32(255,210,80,(int)(255*alpha))
                             : IM_COL32(kr,kg,kb,(int)(200*alpha));
        float rounding = 6*st.depGraphZoom;

        dl->AddRectFilled({sTL.x+2,sTL.y+2},{sBR.x+2,sBR.y+2}, IM_COL32(0,0,0,(int)(60*alpha)), rounding);
        dl->AddRectFilled(sTL, sBR, fill, rounding);
        dl->AddRect(sTL, sBR, border, rounding, 0, isSel ? 2.5f : 1.5f);

        float barW = 5*st.depGraphZoom;
        dl->AddRectFilled(sTL, {sTL.x+barW, sBR.y}, IM_COL32(kr,kg,kb,(int)(255*alpha)),
                          rounding, ImDrawFlags_RoundCornersLeft);

        float fs = std::max(10.f, 13.f*st.depGraphZoom);
        float tx = sTL.x + barW + 6*st.depGraphZoom;
        float ty = sTL.y + (sBR.y - sTL.y - fs)*0.5f;
        ImU32 tcol = IM_COL32(215,215,225,(int)(255*alpha));
        dl->AddText(nullptr, fs, {tx, ty}, tcol, name.c_str());

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImColor(kindCol), "[%s]", TargetTypeLabel(ttype));
            ImGui::SameLine(); ImGui::Text(" %s", name.c_str());
            if (tit != st.cmakeProject.targets.end()) {
                ImGui::TextDisabled("Defined in: %s", tit->second.definedInFile.c_str());
                if (!tit->second.linkLibraries.empty()) {
                    ImGui::Text("Links:");
                    for (auto& lib : tit->second.linkLibraries)
                        ImGui::BulletText("%s", lib.c_str());
                }
            }
            ImGui::EndTooltip();
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            clickedNode = name;
            if (st.depGraphDragging.empty()) st.depGraphDragging = name;
        }
    }

    if (!st.depGraphDragging.empty()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto it = st.depGraphNodes.find(st.depGraphDragging);
            if (it != st.depGraphNodes.end()) {
                it->second.pos.x += io.MouseDelta.x / st.depGraphZoom;
                it->second.pos.y += io.MouseDelta.y / st.depGraphZoom;
            }
        } else st.depGraphDragging.clear();
    }

    if (!clickedNode.empty()) {
        st.selectedTarget = clickedNode;
        st.selectedDirectory.clear();
    }
    if (inCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && clickedNode.empty() && st.depGraphDragging.empty()) {
        st.selectedTarget.clear();
    }

    // Legend
    {
        ImVec2 lp = {origin.x+12, origin.y+cSize.y-26};
        struct LI { CMakeTargetType t; const char* label; };
        LI items[] = {
            {CMakeTargetType::Executable, "Exe"},
            {CMakeTargetType::StaticLibrary, "Static"},
            {CMakeTargetType::SharedLibrary, "Shared"},
            {CMakeTargetType::InterfaceLibrary, "Interface"},
            {CMakeTargetType::ObjectLibrary, "Object"},
        };
        for (auto& item : items) {
            dl->AddRectFilled(lp, {lp.x+10,lp.y+10}, TargetTypeColor(item.t), 2);
            dl->AddText({lp.x+14,lp.y-2}, IM_COL32(170,170,180,255), item.label);
            lp.x += ImGui::CalcTextSize(item.label).x + 28;
        }
    }
    ImGui::EndChild();

    // Detail side panel
    if (!st.selectedTarget.empty()) {
        ImGui::SameLine();
        ImGui::BeginChild("dep_detail", {detailW, 0}, ImGuiChildFlags_Borders);
        auto tit = st.cmakeProject.targets.find(st.selectedTarget);
        if (tit != st.cmakeProject.targets.end()) {
            auto& tgt = tit->second;
            ImGui::PushStyleColor(ImGuiCol_Text, TargetTypeColor(tgt.type));
            ImGui::Text("%s", tgt.name.c_str());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s | %s:%d", TargetTypeLabel(tgt.type),
                                tgt.definedInFile.c_str(), tgt.definedAtLine);
            ImGui::Separator();
            if (!tgt.linkLibraries.empty()) {
                ImGui::Text("Links to:");
                for (auto& lib : tgt.linkLibraries)
                    ImGui::BulletText("%s", lib.c_str());
            }
            if (!tgt.sources.empty()) {
                ImGui::Spacing();
                ImGui::Text("Sources:");
                for (auto& s : tgt.sources)
                    ImGui::BulletText("%s", s.c_str());
            }
        }
        ImGui::EndChild();
    }
}
