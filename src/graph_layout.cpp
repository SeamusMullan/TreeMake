#include "graph_layout.h"

#include <algorithm>
#include <queue>

void BuildGraphLayout(AppState& st) {
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
