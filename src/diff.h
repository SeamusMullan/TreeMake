#pragma once

#include "types.h"

#include <string>
#include <vector>

void BuildDiffLines(const std::string& leftName, const std::string& rightName,
                    const AppState& st,
                    std::vector<DiffLine>& leftLines,
                    std::vector<DiffLine>& rightLines);
