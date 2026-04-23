#include "json_helpers.h"

std::vector<std::string> GetInherits(const json& j) {
    std::vector<std::string> out;
    if (!j.contains("inherits")) return out;
    auto& v = j["inherits"];
    if (v.is_string())     out.push_back(v.get<std::string>());
    else if (v.is_array()) for (auto& e : v) out.push_back(e.get<std::string>());
    return out;
}

std::map<std::string, std::string> GetCacheVars(const json& j) {
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

std::string JStr(const json& j, const char* key) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return {};
}
