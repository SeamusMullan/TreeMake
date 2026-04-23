#include "preset_resolve.h"

#include <algorithm>

void MergeDefaults(ResolvedPreset& dst, const ResolvedPreset& src) {
    if (dst.generator.empty())       dst.generator       = src.generator;
    if (dst.binaryDir.empty())       dst.binaryDir       = src.binaryDir;
    if (dst.installDir.empty())      dst.installDir      = src.installDir;
    if (dst.toolchainFile.empty())   dst.toolchainFile   = src.toolchainFile;
    if (dst.configurePreset.empty()) dst.configurePreset = src.configurePreset;
    if (dst.configuration.empty())   dst.configuration   = src.configuration;
    for (auto& [k, v] : src.cacheVariables)
        if (!dst.cacheVariables.count(k)) dst.cacheVariables[k] = v;
}

ResolvedPreset Resolve(const std::string& name, const AppState& st,
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

std::string BuildResolvedText(const std::string& name, const AppState& st) {
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
