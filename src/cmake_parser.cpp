#include "cmake_parser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

// ═══════════════════════════════════════════════════════════════
//  Utilities
// ═══════════════════════════════════════════════════════════════

static std::string ToLower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

static std::string StripQuotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

static std::vector<std::string> SplitCMakeArgs(const std::string& argStr) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    int parenDepth = 0;
    int genExprDepth = 0;

    for (size_t i = 0; i < argStr.size(); ++i) {
        char c = argStr[i];

        if (c == '\\' && i + 1 < argStr.size()) {
            cur += c;
            cur += argStr[++i];
            continue;
        }

        if (c == '"' && genExprDepth == 0) {
            inQuote = !inQuote;
            cur += c;
            continue;
        }

        if (!inQuote) {
            if (c == '$' && i + 1 < argStr.size() && argStr[i + 1] == '<') {
                genExprDepth++;
                cur += c;
                continue;
            }
            if (c == '>' && genExprDepth > 0) {
                genExprDepth--;
                cur += c;
                continue;
            }
            if (c == '(' && genExprDepth == 0) { parenDepth++; cur += c; continue; }
            if (c == ')' && genExprDepth == 0 && parenDepth > 0) { parenDepth--; cur += c; continue; }

            if (genExprDepth == 0 && parenDepth == 0 &&
                (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';')) {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
                continue;
            }
        }

        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  Command extraction from file text
// ═══════════════════════════════════════════════════════════════

struct CMakeCommand {
    std::string name;
    std::string argString;
    int         line = 0;
};

static std::string StripComments(const std::string& line) {
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inQuote = !inQuote;
        if (!inQuote && line[i] == '#')
            return line.substr(0, i);
    }
    return line;
}

static std::vector<CMakeCommand> ExtractCommands(const std::string& fileContent) {
    std::vector<CMakeCommand> commands;

    std::istringstream stream(fileContent);
    std::string line;
    std::string accumulated;
    int parenDepth = 0;
    int cmdStartLine = 0;
    int lineNum = 0;

    while (std::getline(stream, line)) {
        lineNum++;
        line = StripComments(line);

        if (parenDepth == 0 && accumulated.empty()) {
            size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
            if (firstNonSpace == std::string::npos) continue;
            cmdStartLine = lineNum;
        }

        accumulated += line + "\n";

        bool inQuote = false;
        for (char c : line) {
            if (c == '"') inQuote = !inQuote;
            if (!inQuote) {
                if (c == '(') parenDepth++;
                if (c == ')') parenDepth--;
            }
        }

        if (parenDepth <= 0) {
            parenDepth = 0;
            static const std::regex cmdRe(R"(^\s*(\w+)\s*\(([\s\S]*)\)\s*$)");
            std::smatch m;
            if (std::regex_match(accumulated, m, cmdRe)) {
                CMakeCommand cmd;
                cmd.name = m[1].str();
                cmd.argString = m[2].str();
                cmd.line = cmdStartLine;
                commands.push_back(std::move(cmd));
            }
            accumulated.clear();
        }
    }
    return commands;
}

// ═══════════════════════════════════════════════════════════════
//  Command handlers
// ═══════════════════════════════════════════════════════════════

static void HandleProject(const std::vector<std::string>& args, CMakeProject& proj) {
    if (args.empty()) return;
    proj.projectName = StripQuotes(args[0]);

    bool inLangs = false;
    bool inVersion = false;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);

        if (upper == "LANGUAGES") { inLangs = true; inVersion = false; continue; }
        if (upper == "VERSION") { inVersion = true; inLangs = false; continue; }
        if (upper == "DESCRIPTION" || upper == "HOMEPAGE_URL") {
            inLangs = false; inVersion = false; continue;
        }

        if (inVersion) {
            proj.cmakeMinVersion = StripQuotes(args[i]);
            inVersion = false;
        } else if (inLangs) {
            proj.languages.push_back(StripQuotes(args[i]));
        }
    }

    if (proj.languages.empty()) {
        proj.languages.push_back("C");
        proj.languages.push_back("CXX");
    }
}

static void HandleCMakeMinimumRequired(const std::vector<std::string>& args, CMakeProject& proj) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (ToLower(args[i]) == "version" && i + 1 < args.size()) {
            std::string ver = StripQuotes(args[i + 1]);
            size_t dots = ver.find("...");
            if (dots != std::string::npos) ver = ver.substr(0, dots);
            if (proj.cmakeMinVersion.empty() || proj.cmakeMinVersion < ver)
                proj.cmakeMinVersion = ver;
            break;
        }
    }
}

static void HandleAddExecutable(const std::vector<std::string>& args,
                                 const std::string& relDir, CMakeProject& proj, int line) {
    if (args.empty()) return;
    std::string name = StripQuotes(args[0]);

    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (upper == "IMPORTED" || upper == "ALIAS") return;
    }

    CMakeTarget tgt;
    tgt.name = name;
    tgt.type = CMakeTargetType::Executable;
    tgt.definedInFile = relDir;
    tgt.definedAtLine = line;

    static const std::set<std::string> skipKw = {
        "WIN32", "MACOSX_BUNDLE", "EXCLUDE_FROM_ALL"
    };
    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (skipKw.count(upper)) continue;
        tgt.sources.push_back(StripQuotes(args[i]));
    }

    proj.targets[name] = std::move(tgt);

    auto& dir = proj.directories[relDir];
    dir.targetNames.push_back(name);
}

static void HandleAddLibrary(const std::vector<std::string>& args,
                              const std::string& relDir, CMakeProject& proj, int line) {
    if (args.empty()) return;
    std::string name = StripQuotes(args[0]);

    CMakeTargetType type = CMakeTargetType::Unknown;
    size_t srcStart = 1;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (upper == "IMPORTED" || upper == "ALIAS") return;
        if (upper == "STATIC")    { type = CMakeTargetType::StaticLibrary; srcStart = i + 1; break; }
        if (upper == "SHARED" || upper == "MODULE") { type = CMakeTargetType::SharedLibrary; srcStart = i + 1; break; }
        if (upper == "INTERFACE") { type = CMakeTargetType::InterfaceLibrary; srcStart = i + 1; break; }
        if (upper == "OBJECT")   { type = CMakeTargetType::ObjectLibrary; srcStart = i + 1; break; }
    }

    CMakeTarget tgt;
    tgt.name = name;
    tgt.type = type;
    tgt.definedInFile = relDir;
    tgt.definedAtLine = line;

    static const std::set<std::string> skipKw = {"EXCLUDE_FROM_ALL"};
    for (size_t i = srcStart; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (skipKw.count(upper)) continue;
        tgt.sources.push_back(StripQuotes(args[i]));
    }

    proj.targets[name] = std::move(tgt);

    auto& dir = proj.directories[relDir];
    dir.targetNames.push_back(name);
}

static void HandleOption(const std::vector<std::string>& args,
                          const std::string& relDir, CMakeProject& proj) {
    if (args.empty()) return;
    CMakeOption opt;
    opt.name = StripQuotes(args[0]);
    if (args.size() > 1) opt.description = StripQuotes(args[1]);
    opt.defaultValue = (args.size() > 2) ? StripQuotes(args[2]) : "OFF";
    opt.definedInFile = relDir;
    proj.options.push_back(std::move(opt));
}

static void HandleSet(const std::vector<std::string>& args,
                       const std::string& relDir, CMakeProject& proj) {
    if (args.size() < 2) return;

    bool hasCache = false;
    size_t cacheIdx = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (ToLower(args[i]) == "cache") { hasCache = true; cacheIdx = i; break; }
    }
    if (!hasCache) return;

    CMakeCacheVar cv;
    cv.name = StripQuotes(args[0]);

    if (cacheIdx > 1) {
        std::string val;
        for (size_t i = 1; i < cacheIdx; ++i) {
            if (!val.empty()) val += ";";
            val += StripQuotes(args[i]);
        }
        cv.value = val;
    }

    if (cacheIdx + 1 < args.size()) cv.type = StripQuotes(args[cacheIdx + 1]);
    if (cacheIdx + 2 < args.size()) cv.docstring = StripQuotes(args[cacheIdx + 2]);
    cv.definedInFile = relDir;

    proj.cacheVars.push_back(std::move(cv));
}

static void HandleFindPackage(const std::vector<std::string>& args,
                               const std::string& relDir, CMakeProject& proj) {
    if (args.empty()) return;
    CMakeFindPackageDep dep;
    dep.name = StripQuotes(args[0]);
    dep.definedInFile = relDir;

    static const std::regex versionRe(R"(\d+(\.\d+)*)");
    bool inComponents = false;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);

        if (upper == "REQUIRED") { dep.required = true; inComponents = false; continue; }
        if (upper == "COMPONENTS" || upper == "OPTIONAL_COMPONENTS") { inComponents = true; continue; }
        if (upper == "CONFIG" || upper == "MODULE" || upper == "NO_MODULE" ||
            upper == "QUIET" || upper == "NO_POLICY_SCOPE" || upper == "GLOBAL") {
            inComponents = false; continue;
        }
        if (upper == "PATHS" || upper == "HINTS" || upper == "NAMES" ||
            upper == "CONFIGS" || upper == "PATH_SUFFIXES") {
            inComponents = false; continue;
        }

        if (inComponents) {
            dep.components.push_back(StripQuotes(args[i]));
        } else if (dep.version.empty() && std::regex_match(args[i], versionRe)) {
            dep.version = args[i];
        }
    }

    proj.findPackageDeps.push_back(std::move(dep));
}

static void HandleTargetLinkLibraries(const std::vector<std::string>& args, CMakeProject& proj) {
    if (args.size() < 2) return;
    std::string target = StripQuotes(args[0]);

    auto it = proj.targets.find(target);
    if (it == proj.targets.end()) return;

    static const std::set<std::string> visKw = {
        "PUBLIC", "PRIVATE", "INTERFACE"
    };
    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (visKw.count(upper)) continue;
        std::string lib = StripQuotes(args[i]);
        if (!lib.empty())
            it->second.linkLibraries.push_back(lib);
    }
}

static void HandleTargetIncludeDirectories(const std::vector<std::string>& args, CMakeProject& proj) {
    if (args.size() < 2) return;
    std::string target = StripQuotes(args[0]);

    auto it = proj.targets.find(target);
    if (it == proj.targets.end()) return;

    static const std::set<std::string> visKw = {
        "PUBLIC", "PRIVATE", "INTERFACE", "SYSTEM", "BEFORE", "AFTER"
    };
    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (visKw.count(upper)) continue;
        std::string dir = StripQuotes(args[i]);
        if (!dir.empty())
            it->second.includeDirectories.push_back(dir);
    }
}

static void HandleFetchContentDeclare(const std::vector<std::string>& args,
                                       const std::string& relDir, CMakeProject& proj) {
    if (args.empty()) return;
    CMakeFetchContentDep dep;
    dep.name = StripQuotes(args[0]);
    dep.definedInFile = relDir;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string upper = args[i];
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);

        if (upper == "GIT_REPOSITORY" && i + 1 < args.size()) {
            dep.gitRepository = StripQuotes(args[++i]);
        } else if (upper == "GIT_TAG" && i + 1 < args.size()) {
            dep.gitTag = StripQuotes(args[++i]);
        } else if (upper == "GIT_SHALLOW" && i + 1 < args.size()) {
            std::string val = ToLower(StripQuotes(args[++i]));
            dep.gitShallow = (val == "true" || val == "on" || val == "1" || val == "yes");
        } else if (upper == "URL" && i + 1 < args.size()) {
            dep.url = StripQuotes(args[++i]);
        }
    }

    proj.fetchContentDeps.push_back(std::move(dep));
}

// ═══════════════════════════════════════════════════════════════
//  File and project parsing
// ═══════════════════════════════════════════════════════════════

static void ParseCMakeListsFile(const fs::path& filePath, const std::string& relativeDir,
                                 CMakeProject& project, std::set<std::string>& visited) {
    std::error_code ec;
    fs::path canonical = fs::canonical(filePath, ec);
    if (ec) canonical = fs::absolute(filePath);
    std::string key = canonical.string();

    if (visited.count(key)) return;
    visited.insert(key);

    if (!fs::exists(canonical)) {
        project.errorLog += (project.errorLog.empty() ? "" : "\n")
                          + std::string("File not found: ") + filePath.string();
        return;
    }

    std::ifstream f(canonical);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    auto& dir = project.directories[relativeDir];
    dir.relativePath = relativeDir;
    dir.absolutePath = canonical.parent_path();

    auto commands = ExtractCommands(content);

    for (auto& cmd : commands) {
        auto args = SplitCMakeArgs(cmd.argString);
        std::string lower = ToLower(cmd.name);

        if (lower == "project") {
            HandleProject(args, project);
        } else if (lower == "cmake_minimum_required") {
            HandleCMakeMinimumRequired(args, project);
        } else if (lower == "add_executable") {
            HandleAddExecutable(args, relativeDir, project, cmd.line);
        } else if (lower == "add_library") {
            HandleAddLibrary(args, relativeDir, project, cmd.line);
        } else if (lower == "option") {
            HandleOption(args, relativeDir, project);
        } else if (lower == "set") {
            HandleSet(args, relativeDir, project);
        } else if (lower == "find_package") {
            HandleFindPackage(args, relativeDir, project);
        } else if (lower == "target_link_libraries") {
            HandleTargetLinkLibraries(args, project);
        } else if (lower == "target_include_directories") {
            HandleTargetIncludeDirectories(args, project);
        } else if (lower == "fetchcontent_declare") {
            HandleFetchContentDeclare(args, relativeDir, project);
        } else if (lower == "add_subdirectory") {
            if (!args.empty()) {
                std::string subName = StripQuotes(args[0]);
                dir.subdirectories.push_back(subName);

                std::string childRel = (relativeDir == ".") ? subName : relativeDir + "/" + subName;
                fs::path childCML = canonical.parent_path() / subName / "CMakeLists.txt";
                if (fs::exists(childCML))
                    ParseCMakeListsFile(childCML, childRel, project, visited);
            }
        }
    }
}

bool ParseCMakeProject(const fs::path& rootDir, CMakeProject& project) {
    project = CMakeProject{};
    project.rootDir = rootDir;

    fs::path rootCML = rootDir / "CMakeLists.txt";
    if (!fs::exists(rootCML)) {
        project.errorLog = "No CMakeLists.txt found in " + rootDir.string();
        return false;
    }

    std::set<std::string> visited;
    ParseCMakeListsFile(rootCML, ".", project, visited);
    return true;
}
