#pragma once

#include "types.h"

fs::path GetRecentFilePath();
void LoadRecentFiles(AppState& st);
void SaveRecentFiles(const AppState& st);
void AddToRecent(AppState& st, const std::string& filePath);

void LoadSettings(AppState& st);
void SaveSettings(const AppState& st);
