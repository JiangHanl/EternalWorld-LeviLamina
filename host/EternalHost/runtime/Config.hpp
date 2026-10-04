#pragma once
#include "Host.hpp"
namespace eternal::host {
bool parseConfig(std::string_view json, std::vector<ModuleConfig>& out, std::string& error);
bool loadConfig(const std::filesystem::path& file, std::vector<ModuleConfig>& out, std::string& error);
} // namespace eternal::host
