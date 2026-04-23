#pragma once

#include "types.h"

#include <map>
#include <string>
#include <vector>

std::vector<std::string> GetInherits(const json& j);
std::map<std::string, std::string> GetCacheVars(const json& j);
std::string JStr(const json& j, const char* key);
