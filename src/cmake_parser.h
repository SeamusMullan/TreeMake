#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class CMakeTargetType {
    Executable, StaticLibrary, SharedLibrary,
    InterfaceLibrary, ObjectLibrary, Unknown
};

struct CMakeTarget {
    std::string              name;
    CMakeTargetType          type = CMakeTargetType::Unknown;
    std::vector<std::string> sources;
    std::vector<std::string> linkLibraries;
    std::vector<std::string> includeDirectories;
    std::string              definedInFile;
    int                      definedAtLine = 0;
};

struct CMakeOption {
    std::string name;
    std::string description;
    std::string defaultValue;
    std::string definedInFile;
};

struct CMakeCacheVar {
    std::string name;
    std::string value;
    std::string type;
    std::string docstring;
    std::string definedInFile;
};

struct CMakeFetchContentDep {
    std::string name;
    std::string gitRepository;
    std::string gitTag;
    bool        gitShallow = false;
    std::string url;
    std::string definedInFile;
};

struct CMakeFindPackageDep {
    std::string              name;
    std::string              version;
    bool                     required = false;
    std::vector<std::string> components;
    std::string              definedInFile;
};

struct CMakeDirectory {
    std::string              relativePath;
    fs::path                 absolutePath;
    std::vector<std::string> subdirectories;
    std::vector<std::string> targetNames;
};

struct CMakeProject {
    std::string              projectName;
    std::string              cmakeMinVersion;
    std::vector<std::string> languages;
    fs::path                 rootDir;

    std::map<std::string, CMakeTarget>     targets;
    std::vector<CMakeOption>               options;
    std::vector<CMakeCacheVar>             cacheVars;
    std::vector<CMakeFetchContentDep>      fetchContentDeps;
    std::vector<CMakeFindPackageDep>       findPackageDeps;
    std::map<std::string, CMakeDirectory>  directories;

    std::string errorLog;
};

bool ParseCMakeProject(const fs::path& rootDir, CMakeProject& project);
