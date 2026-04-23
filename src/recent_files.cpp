#include "recent_files.h"

#include <algorithm>
#include <cmath>
#include <fstream>

fs::path GetRecentFilePath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) return fs::path(appdata) / "TreeMake" / "recent.txt";
    return fs::path(".") / ".treemake_recent";
#else
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / ".config" / "treemake" / "recent.txt";
    return fs::path(".") / ".treemake_recent";
#endif
}

void LoadRecentFiles(AppState& st) {
    st.recentFiles.clear();
    auto path = GetRecentFilePath();
    if (!fs::exists(path)) return;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && st.recentFiles.size() < 15)
            st.recentFiles.push_back(line);
    }
}

void SaveRecentFiles(const AppState& st) {
    auto path = GetRecentFilePath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    for (auto& p : st.recentFiles) f << p << "\n";
}

void AddToRecent(AppState& st, const std::string& filePath) {
    std::error_code ec;
    std::string canonical = fs::canonical(filePath, ec).string();
    if (ec) canonical = fs::absolute(filePath).string();

    st.recentFiles.erase(
        std::remove(st.recentFiles.begin(), st.recentFiles.end(), canonical),
        st.recentFiles.end());
    st.recentFiles.insert(st.recentFiles.begin(), canonical);
    if (st.recentFiles.size() > 15) st.recentFiles.resize(15);
    SaveRecentFiles(st);
}

static constexpr float kUIScaleMin = 0.75f;
static constexpr float kUIScaleMax = 2.5f;

static fs::path GetSettingsPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) return fs::path(appdata) / "TreeMake" / "settings.txt";
    return fs::path(".") / ".treemake_settings";
#else
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / ".config" / "treemake" / "settings.txt";
    return fs::path(".") / ".treemake_settings";
#endif
}

void LoadSettings(AppState& st) {
    auto path = GetSettingsPath();
    std::ifstream f(path);
    if (!f) return;
    std::string key;
    while (f >> key) {
        if (key == "uiScale") {
            float val;
            if (f >> val) st.uiScale = std::clamp(val, kUIScaleMin, kUIScaleMax);
        }
    }
}

void SaveSettings(const AppState& st) {
    auto path = GetSettingsPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    if (f) f << "uiScale " << st.uiScale << "\n";
}
