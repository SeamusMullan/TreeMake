#include "diff.h"
#include "preset_resolve.h"

#include <set>

void BuildDiffLines(const std::string& leftName, const std::string& rightName,
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

    std::set<std::string> allKeys;
    for (auto& [k, _] : lr.cacheVariables) allKeys.insert(k);
    for (auto& [k, _] : rr.cacheVariables) allKeys.insert(k);

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
