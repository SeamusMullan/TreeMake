#pragma once

#include "types.h"

#include <set>
#include <string>

void MergeDefaults(ResolvedPreset& dst, const ResolvedPreset& src);
ResolvedPreset Resolve(const std::string& name, const AppState& st,
                       std::set<std::string>& visited);
std::string BuildResolvedText(const std::string& name, const AppState& st);
