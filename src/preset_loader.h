#pragma once

#include "types.h"

#include <set>
#include <string>

void ParsePresetsFromArray(const json& arr, PresetKind kind,
                           const std::string& sourceFile, AppState& st);
void LoadPresetFile(const std::string& path, AppState& st,
                    std::set<std::string>& visitedFiles);
bool LoadFile(const std::string& path, AppState& st);
bool LoadProjectDirectory(const std::string& dirPath, AppState& st);
